/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "servoengine.h"
#include "servowebview.h"

#include <servo_capi.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMutexLocker>

namespace Tuuli {

const QEvent::Type ServoWakeEventType = static_cast<QEvent::Type>(QEvent::registerEventType());

/* ---- Rendering-context vtable over Tuuli::RenderingContext ------------ */

namespace {

bool rcMakeCurrent(void* ud) { return static_cast<RenderingContext*>(ud)->makeCurrent(); }
void rcSwapBuffers(void*) {}
void rcSize(void* ud, ServoSize* out)
{
    const QSize s = static_cast<RenderingContext*>(ud)->size();
    out->width = static_cast<uint32_t>(qMax(0, s.width()));
    out->height = static_cast<uint32_t>(qMax(0, s.height()));
}
void rcResize(void*, ServoSize) {}
uint32_t rcFbo(void* ud) { return static_cast<RenderingContext*>(ud)->framebufferObject(); }
void* rcProc(void* ud, const char* name) { return static_cast<RenderingContext*>(ud)->procAddress(name); }
void rcPrepare(void*) {}
void rcPresent(void*) {}

} // namespace

/* ---- Instance callbacks ---------------------------------------------- */

struct ServoEngine::Callbacks
{
    static void wakeUp(void* ud)
    {
        // Any thread.  Coalesce: one queued wake at a time.
        ServoEngine* self = static_cast<ServoEngine*>(ud);
        if (self->m_wakePending.testAndSetOrdered(0, 1))
            QCoreApplication::postEvent(self, new QEvent(ServoWakeEventType));
    }

    static void panic(void* ud, const char* reason, const char* backtrace)
    {
        ServoEngine* self = static_cast<ServoEngine*>(ud);
        QMetaObject::invokeMethod(self, "crashed", Qt::QueuedConnection,
                                  Q_ARG(QString, QString::fromUtf8(reason ? reason : "")),
                                  Q_ARG(QString, QString::fromUtf8(backtrace ? backtrace : "")));
    }

    static ::ServoWebView* createNewWebView(void* ud, ::ServoWebView* parent)
    {
        ServoEngine* self = static_cast<ServoEngine*>(ud);
        const bool priv = parent ? servo_webview_is_private(parent) : false;
        ServoWebView* wrapper = new ServoWebView(self, parent, priv, ServoWebView::AuxiliaryTag());
        if (!wrapper->raw()) {
            delete wrapper;
            return nullptr;
        }
        self->adoptAuxiliaryWebView(wrapper);
        return wrapper->raw();
    }

    static const char* clipboardGet(void* ud)
    {
        ServoEngine* self = static_cast<ServoEngine*>(ud);
        QClipboard* cb = QGuiApplication::clipboard();
        self->m_clipboardScratch = cb ? cb->text().toUtf8() : QByteArray();
        return self->m_clipboardScratch.constData();
    }

    static void clipboardSet(void*, const char* text)
    {
        if (QClipboard* cb = QGuiApplication::clipboard())
            cb->setText(QString::fromUtf8(text ? text : ""));
    }
};

/* ---- ServoEngine ------------------------------------------------------ */

ServoEngine::ServoEngine(QObject* parent)
    : Engine(parent), m_initialized(0), m_wakePending(0)
{
}

ServoEngine::~ServoEngine()
{
    // shutdownOnRenderThread() must already have run; if not, we cannot
    // safely touch GL here.
    if (m_servo)
        qWarning("ServoEngine destroyed while still initialised; leaking GL state");
}

QString ServoEngine::versionString() const
{
    return QString::fromUtf8(servo_version_string());
}

void ServoEngine::configure(const EngineConfig& config)
{
    m_config = config;
}

bool ServoEngine::initializeOnRenderThread(RenderingContext* ctx)
{
    if (isInitialized())
        return true;
    if (servo_capi_version_check(SERVO_CAPI_VERSION_MAJOR, SERVO_CAPI_VERSION_MINOR) != 0) {
        qCritical("libservo ABI mismatch: header %d.%d, library %s",
                  SERVO_CAPI_VERSION_MAJOR, SERVO_CAPI_VERSION_MINOR, servo_version_string());
        return false;
    }

    m_qtCtx = ctx;
    static ServoRenderingContextVTable vtable;
    vtable.user_data = ctx;
    vtable.make_current = rcMakeCurrent;
    vtable.swap_buffers = rcSwapBuffers;
    vtable.size = rcSize;
    vtable.resize = rcResize;
    vtable.framebuffer_object = rcFbo;
    vtable.get_proc_address = rcProc;
    vtable.prepare_for_rendering = rcPrepare;
    vtable.present = rcPresent;
    m_ctx = servo_rendering_context_new_external(&vtable, ctx->isGles() ? SERVO_GL_API_GLES : SERVO_GL_API_GL,
                                                 static_cast<uint32_t>(ctx->glMajorVersion()),
                                                 static_cast<uint32_t>(ctx->glMinorVersion()));
    if (!m_ctx)
        return false;

    // Keep every string alive for the duration of servo_init.
    m_configStrings.clear();
    m_prefPointers.clear();
    auto keep = [this](const QString& s) -> const char* {
        if (s.isEmpty())
            return nullptr;
        m_configStrings.append(s.toUtf8());
        return m_configStrings.last().constData();
    };

    ServoInstanceConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.user_agent = keep(m_config.userAgent);
    cfg.ua_platform = m_config.mobilePlatform ? SERVO_UA_PLATFORM_MOBILE_LINUX : SERVO_UA_PLATFORM_DESKTOP;
    cfg.certificate_path = keep(m_config.certificatePath);
    cfg.config_dir = keep(m_config.dataDir);
    cfg.cache_dir = keep(m_config.cacheDir);
    ServoProxyConfig proxy;
    proxy.http = keep(m_config.proxy.http);
    proxy.https = keep(m_config.proxy.https);
    proxy.no_proxy = keep(m_config.proxy.noProxy.join(QLatin1Char(',')));
    proxy.pac_url = keep(m_config.proxy.pacUrl.toString());
    cfg.proxy = m_config.proxy.isDirect() ? nullptr : &proxy;
    for (const QString& p : m_config.prefs)
        m_prefPointers.append(keep(p));
    cfg.prefs = m_prefPointers.isEmpty() ? nullptr : m_prefPointers.constData();
    cfg.prefs_count = static_cast<size_t>(m_prefPointers.size());
    cfg.enable_hardware_video_decode = m_config.hardwareVideoDecode;
    cfg.gst_plugin_path = nullptr;
    cfg.layout_threads = static_cast<uint32_t>(qMax(0, m_config.layoutThreads));

    ServoInstanceCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.user_data = this;
    cb.wake_up = &Callbacks::wakeUp;
    cb.panic = &Callbacks::panic;
    cb.request_create_new_webview = &Callbacks::createNewWebView;
    cb.clipboard_get_text = &Callbacks::clipboardGet;
    cb.clipboard_set_text = &Callbacks::clipboardSet;

    m_servo = servo_init(&cfg, m_ctx, &cb);
    if (!m_servo) {
        servo_rendering_context_free(m_ctx);
        m_ctx = nullptr;
        return false;
    }
    m_initialized.storeRelease(1);
    QMetaObject::invokeMethod(this, "initialized", Qt::QueuedConnection);
    return true;
}

void ServoEngine::shutdownOnRenderThread()
{
    if (!isInitialized())
        return;
    QMutexLocker lock(renderLock());
    m_initialized.storeRelease(0);
    servo_deinit(m_servo);
    m_servo = nullptr;
    servo_rendering_context_free(m_ctx);
    m_ctx = nullptr;
    m_qtCtx = nullptr;
    QMetaObject::invokeMethod(this, "renderContextLost", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "shutDown", Qt::QueuedConnection);
}

WebViewHandle* ServoEngine::createWebView(WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx)
{
    if (!isInitialized())
        return nullptr;
    QMutexLocker lock(renderLock());
    ServoWebView* wv = new ServoWebView(this, client, isPrivate, dpr, devicePx);
    if (!wv->raw()) {
        delete wv;
        return nullptr;
    }
    return wv;
}

void ServoEngine::adoptAuxiliaryWebView(ServoWebView* wv)
{
    QMetaObject::invokeMethod(this, "auxiliaryWebViewCreated", Qt::QueuedConnection,
                              Q_ARG(Tuuli::WebViewHandle*, wv));
}

bool ServoEngine::event(QEvent* e)
{
    if (e->type() == ServoWakeEventType) {
        m_wakePending.storeRelease(0);
        spinEventLoop();
        return true;
    }
    return Engine::event(e);
}

void ServoEngine::spinEventLoop()
{
    if (!isInitialized())
        return;
    QMutexLocker lock(renderLock());
    if (!servo_spin_event_loop(m_servo)) {
        // Servo shut itself down; treat as a crash for the UI.
        m_initialized.storeRelease(0);
        emit crashed(QStringLiteral("engine event loop exited"), QString());
    }
}

void ServoEngine::setPref(const QString& name, const QString& value)
{
    if (!isInitialized())
        return;
    QMutexLocker lock(renderLock());
    servo_set_pref(m_servo, name.toUtf8().constData(), value.toUtf8().constData());
}

void ServoEngine::setProxy(const ProxyConfig& proxyCfg)
{
    m_config.proxy = proxyCfg;
    if (!isInitialized())
        return;
    QMutexLocker lock(renderLock());
    const QByteArray http = proxyCfg.http.toUtf8();
    const QByteArray https = proxyCfg.https.toUtf8();
    const QByteArray noProxy = proxyCfg.noProxy.join(QLatin1Char(',')).toUtf8();
    const QByteArray pac = proxyCfg.pacUrl.toString().toUtf8();
    ServoProxyConfig p;
    p.http = http.isEmpty() ? nullptr : http.constData();
    p.https = https.isEmpty() ? nullptr : https.constData();
    p.no_proxy = noProxy.isEmpty() ? nullptr : noProxy.constData();
    p.pac_url = pac.isEmpty() ? nullptr : pac.constData();
    servo_set_proxy(m_servo, proxyCfg.isDirect() ? nullptr : &p);
}

void ServoEngine::clearSiteData(const QString& origin, unsigned kinds)
{
    if (!isInitialized())
        return;
    QMutexLocker lock(renderLock());
    const QByteArray o = origin.toUtf8();
    servo_clear_site_data(m_servo, o.isEmpty() ? nullptr : o.constData(), kinds);
}

} // namespace Tuuli
