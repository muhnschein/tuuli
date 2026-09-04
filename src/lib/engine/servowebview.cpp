/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "servowebview.h"
#include "servoengine.h"

#include <servo_capi.h>

#include <QMutexLocker>
#include <QVector>

namespace Tuuli {

/* ---- Request wrappers ------------------------------------------------- */

namespace {

class ServoPermissionRequest : public PermissionRequest
{
public:
    ServoPermissionRequest(::ServoPermissionRequest* req)
        : PermissionRequest(static_cast<PermissionKind>(servo_permission_request_kind(req)),
                            QString::fromUtf8(servo_permission_request_origin(req))), m_req(req) {}
protected:
    void onAllow() override { servo_permission_request_allow(m_req); }
    void onDeny() override { servo_permission_request_deny(m_req); }
private:
    ::ServoPermissionRequest* m_req;
};

class ServoSimpleDialog : public SimpleDialogRequest
{
public:
    ServoSimpleDialog(::ServoSimpleDialog* d)
        : SimpleDialogRequest(static_cast<Kind>(servo_simple_dialog_kind(d)),
                              QString::fromUtf8(servo_simple_dialog_message(d)),
                              QString::fromUtf8(servo_simple_dialog_default_value(d))), m_d(d) {}
protected:
    void onAccept(const QString& value) override { servo_simple_dialog_accept(m_d, value.toUtf8().constData()); }
    void onDismiss() override { servo_simple_dialog_dismiss(m_d); }
private:
    ::ServoSimpleDialog* m_d;
};

} // namespace

class ServoDownloadRequest : public DownloadRequest
{
public:
    ServoDownloadRequest(::ServoDownload* d, const QUrl& url, const QString& name, const QString& mime, qint64 total)
        : DownloadRequest(url, name, mime, total), m_d(d) {}
    ::ServoDownload* raw() const { return m_d; }
protected:
    void onAccept(const QString& path) override { servo_download_accept(m_d, path.toUtf8().constData()); }
    void onReject() override { servo_download_reject(m_d); }
    void onCancel() override { servo_download_cancel(m_d); }
private:
    ::ServoDownload* m_d;
};

/* ---- C trampolines ---------------------------------------------------- */

#define TUULI_SELF(ud) static_cast<Tuuli::ServoWebView*>(ud)
#define TUULI_STR(s) QString::fromUtf8((s) ? (s) : "")

struct ServoWebView::Callbacks
{
    static void urlChanged(void* ud, const char* url)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onUrlChanged", Qt::QueuedConnection, Q_ARG(QString, TUULI_STR(url)));
    }
    static void titleChanged(void* ud, const char* title)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onTitleChanged", Qt::QueuedConnection, Q_ARG(QString, TUULI_STR(title)));
    }
    static void loadStatus(void* ud, ServoLoadStatus status)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onLoadStatus", Qt::QueuedConnection, Q_ARG(int, static_cast<int>(status)));
    }
    static void favicon(void* ud, const uint8_t* rgba, uint32_t w, uint32_t h)
    {
        QImage img;
        if (rgba && w > 0 && h > 0)
            img = QImage(rgba, static_cast<int>(w), static_cast<int>(h), static_cast<int>(w) * 4,
                         QImage::Format_RGBA8888).copy();
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onFavicon", Qt::QueuedConnection, Q_ARG(QImage, img));
    }
    static void history(void* ud, const char* const*, size_t count, size_t current)
    {
        const bool back = count > 0 && current > 0;
        const bool fwd = count > 0 && current + 1 < count;
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onHistory", Qt::QueuedConnection, Q_ARG(bool, back), Q_ARG(bool, fwd));
    }
    static void frameReady(void* ud)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onFrameReady", Qt::QueuedConnection);
    }
    static void statusText(void*, const char*) {}
    static void cursor(void*, const char*) {}
    static void fullscreen(void*, bool) {}
    static void viewport(void* ud, float x, float y, float zoom, float cw, float ch)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onViewport", Qt::QueuedConnection,
                                  Q_ARG(double, x), Q_ARG(double, y), Q_ARG(double, zoom),
                                  Q_ARG(double, cw), Q_ARG(double, ch));
    }
    static void showIme(void* ud, ServoInputType type, const char* text, bool multiline, ServoRect rect)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onShowIme", Qt::QueuedConnection,
                                  Q_ARG(int, static_cast<int>(type)), Q_ARG(QString, TUULI_STR(text)),
                                  Q_ARG(bool, multiline),
                                  Q_ARG(QRectF, QRectF(rect.x, rect.y, rect.width, rect.height)));
    }
    static void hideIme(void* ud)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onHideIme", Qt::QueuedConnection);
    }
    static void imeSelection(void* ud, const char* text, uint32_t cursor, uint32_t anchor)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onImeSelection", Qt::QueuedConnection,
                                  Q_ARG(QString, TUULI_STR(text)), Q_ARG(int, static_cast<int>(cursor)),
                                  Q_ARG(int, static_cast<int>(anchor)));
    }
    static void permission(void* ud, ::ServoPermissionRequest* req)
    {
        QObject* wrapper = new ServoPermissionRequest(req);
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onPermission", Qt::QueuedConnection, Q_ARG(QObject*, wrapper));
    }
    static void navigation(void*, ::ServoNavigationRequest* req)
    {
        // Tuuli does not filter navigations (no network-level blocking, spec 9.3).
        servo_navigation_request_allow(req);
    }
    static void dialog(void* ud, ::ServoSimpleDialog* d)
    {
        QObject* wrapper = new ServoSimpleDialog(d);
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onDialog", Qt::QueuedConnection, Q_ARG(QObject*, wrapper));
    }
    static void contextMenu(void* ud, float x, float y, const char* link, const char* image,
                            const char* selected, bool editable)
    {
        ContextMenuInfo info;
        info.cssPos = QPointF(x, y);
        info.linkUrl = QUrl(TUULI_STR(link));
        info.imageUrl = QUrl(TUULI_STR(image));
        info.selectedText = TUULI_STR(selected);
        info.editable = editable;
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onContextMenu", Qt::QueuedConnection,
                                  Q_ARG(Tuuli::ContextMenuInfo, info));
    }
    static void downloadRequested(void* ud, ::ServoDownload* d, const char* url, const char* name,
                                  const char* mime, int64_t total)
    {
        ServoDownloadRequest* req = new ServoDownloadRequest(d, QUrl(TUULI_STR(url)), TUULI_STR(name),
                                                             TUULI_STR(mime), static_cast<qint64>(total));
        TUULI_SELF(ud)->registerDownload(d, req);
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onDownloadRequested", Qt::QueuedConnection,
                                  Q_ARG(QObject*, req));
    }
    static void downloadProgress(void* ud, ::ServoDownload* d, int64_t received, int64_t total)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onDownloadProgress", Qt::QueuedConnection,
                                  Q_ARG(void*, d), Q_ARG(qint64, static_cast<qint64>(received)),
                                  Q_ARG(qint64, static_cast<qint64>(total)));
    }
    static void downloadFinished(void* ud, ::ServoDownload* d, bool ok, const char* error)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onDownloadFinished", Qt::QueuedConnection,
                                  Q_ARG(void*, d), Q_ARG(bool, ok), Q_ARG(QString, TUULI_STR(error)));
    }
    static void mediaSession(void* ud, ServoMediaSessionEvent ev, const char* title, const char* artist,
                             const char* album, double pos, double dur)
    {
        MediaSessionInfo info;
        info.event = static_cast<MediaSessionEvent>(ev);
        info.title = TUULI_STR(title);
        info.artist = TUULI_STR(artist);
        info.album = TUULI_STR(album);
        info.positionSeconds = pos;
        info.durationSeconds = dur;
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onMediaSession", Qt::QueuedConnection,
                                  Q_ARG(Tuuli::MediaSessionInfo, info));
    }
    static void notification(void* ud, const char* title, const char* body, const char* icon)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onNotification", Qt::QueuedConnection,
                                  Q_ARG(QString, TUULI_STR(title)), Q_ARG(QString, TUULI_STR(body)),
                                  Q_ARG(QString, TUULI_STR(icon)));
    }
    static void closed(void* ud)
    {
        QMetaObject::invokeMethod(TUULI_SELF(ud), "onClosed", Qt::QueuedConnection);
    }

    static ServoWebViewCallbacks table(ServoWebView* self)
    {
        ServoWebViewCallbacks cb;
        memset(&cb, 0, sizeof(cb));
        cb.user_data = self;
        cb.url_changed = &urlChanged;
        cb.title_changed = &titleChanged;
        cb.load_status_changed = &loadStatus;
        cb.favicon_changed = &favicon;
        cb.history_changed = &history;
        cb.new_frame_ready = &frameReady;
        cb.status_text_changed = &statusText;
        cb.cursor_changed = &cursor;
        cb.fullscreen_state_changed = &fullscreen;
        cb.viewport_changed = &viewport;
        cb.show_ime = &showIme;
        cb.hide_ime = &hideIme;
        cb.ime_selection_changed = &imeSelection;
        cb.request_permission = &permission;
        cb.request_navigation = &navigation;
        cb.show_simple_dialog = &dialog;
        cb.show_context_menu = &contextMenu;
        cb.download_requested = &downloadRequested;
        cb.download_progress = &downloadProgress;
        cb.download_finished = &downloadFinished;
        cb.media_session_event = &mediaSession;
        cb.notification_requested = &notification;
        cb.closed = &closed;
        return cb;
    }
};

/* ---- ServoWebView ----------------------------------------------------- */

static void registerMetaTypesOnce()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    qRegisterMetaType<Tuuli::ContextMenuInfo>("Tuuli::ContextMenuInfo");
    qRegisterMetaType<Tuuli::MediaSessionInfo>("Tuuli::MediaSessionInfo");
    qRegisterMetaType<Tuuli::WebViewHandle*>("Tuuli::WebViewHandle*");
}

ServoWebView::ServoWebView(ServoEngine* engine, WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx)
    : WebViewHandle(nullptr), m_engine(engine), m_client(client), m_private(isPrivate)
{
    registerMetaTypesOnce();
    const ServoWebViewCallbacks cb = Callbacks::table(this);
    ServoSize size;
    size.width = static_cast<uint32_t>(qMax(1, devicePx.width()));
    size.height = static_cast<uint32_t>(qMax(1, devicePx.height()));
    m_wv = servo_webview_new(engine->instance(), engine->renderingContext(), &cb, isPrivate,
                             static_cast<float>(dpr), size);
}

ServoWebView::ServoWebView(ServoEngine* engine, ::ServoWebView* parent, bool isPrivate, AuxiliaryTag)
    : WebViewHandle(nullptr), m_engine(engine), m_client(nullptr), m_private(isPrivate)
{
    registerMetaTypesOnce();
    const ServoWebViewCallbacks cb = Callbacks::table(this);
    m_wv = servo_webview_new_auxiliary(engine->instance(), parent, &cb);
}

ServoWebView::~ServoWebView()
{
    close();
}

void ServoWebView::setClient(WebViewClient* client)
{
    m_client = client;
    replayCached();
}

void ServoWebView::replayCached()
{
    if (!m_client)
        return;
    if (!m_cachedUrl.isEmpty())
        m_client->onUrlChanged(m_cachedUrl);
    if (!m_cachedTitle.isEmpty())
        m_client->onTitleChanged(m_cachedTitle);
}

#define TUULI_GUARD() if (m_closed || !m_wv) return; QMutexLocker lock(m_engine->renderLock())

void ServoWebView::load(const QUrl& url) { TUULI_GUARD(); servo_webview_load(m_wv, url.toEncoded().constData()); }
void ServoWebView::reload() { TUULI_GUARD(); servo_webview_reload(m_wv); }
void ServoWebView::stop() { TUULI_GUARD(); servo_webview_stop(m_wv); }
void ServoWebView::goBack() { TUULI_GUARD(); servo_webview_go_back(m_wv, 1); }
void ServoWebView::goForward() { TUULI_GUARD(); servo_webview_go_forward(m_wv, 1); }
void ServoWebView::setVisible(bool visible)
{
    TUULI_GUARD();
    if (visible) servo_webview_show(m_wv, true); else servo_webview_hide(m_wv);
}
void ServoWebView::setFocused(bool focused)
{
    TUULI_GUARD();
    if (focused) servo_webview_focus(m_wv); else servo_webview_blur(m_wv);
}
void ServoWebView::setSize(const QSize& devicePx)
{
    TUULI_GUARD();
    ServoSize s;
    s.width = static_cast<uint32_t>(qMax(1, devicePx.width()));
    s.height = static_cast<uint32_t>(qMax(1, devicePx.height()));
    servo_webview_resize(m_wv, s);
}
void ServoWebView::setViewportRect(const QRect& r)
{
    TUULI_GUARD();
    servo_webview_set_viewport_rect(m_wv, static_cast<uint32_t>(qMax(0, r.x())), static_cast<uint32_t>(qMax(0, r.y())),
                                    static_cast<uint32_t>(qMax(1, r.width())), static_cast<uint32_t>(qMax(1, r.height())));
}
void ServoWebView::setDevicePixelRatio(qreal dpr) { TUULI_GUARD(); servo_webview_set_hidpi_scale_factor(m_wv, static_cast<float>(dpr)); }
void ServoWebView::setPinchZoom(qreal zoom) { TUULI_GUARD(); servo_webview_set_pinch_zoom(m_wv, static_cast<float>(zoom)); }
void ServoWebView::setPageZoom(qreal zoom) { TUULI_GUARD(); servo_webview_set_page_zoom(m_wv, static_cast<float>(zoom)); }
void ServoWebView::scrollTo(const QPointF& css) { TUULI_GUARD(); servo_webview_scroll_to(m_wv, static_cast<float>(css.x()), static_cast<float>(css.y())); }

void ServoWebView::touch(TouchPhase phase, int id, const QPointF& css)
{
    TUULI_GUARD();
    ServoTouchEventType t = SERVO_TOUCH_CANCEL;
    switch (phase) {
    case TouchPhase::Down: t = SERVO_TOUCH_DOWN; break;
    case TouchPhase::Move: t = SERVO_TOUCH_MOVE; break;
    case TouchPhase::Up: t = SERVO_TOUCH_UP; break;
    case TouchPhase::Cancel: t = SERVO_TOUCH_CANCEL; break;
    }
    servo_webview_touch(m_wv, t, id, static_cast<float>(css.x()), static_cast<float>(css.y()));
}

void ServoWebView::key(bool down, const QString& key, Qt::KeyboardModifiers mods)
{
    TUULI_GUARD();
    uint32_t m = 0;
    if (mods & Qt::ShiftModifier) m |= 1u << 0;
    if (mods & Qt::ControlModifier) m |= 1u << 1;
    if (mods & Qt::AltModifier) m |= 1u << 2;
    if (mods & Qt::MetaModifier) m |= 1u << 3;
    servo_webview_key(m_wv, down ? SERVO_KEY_DOWN : SERVO_KEY_UP, key.toUtf8().constData(), m);
}

void ServoWebView::imeComposition(CompositionState state, const QString& text)
{
    TUULI_GUARD();
    ServoCompositionState s = SERVO_COMPOSITION_END;
    switch (state) {
    case CompositionState::Start: s = SERVO_COMPOSITION_START; break;
    case CompositionState::Update: s = SERVO_COMPOSITION_UPDATE; break;
    case CompositionState::End: s = SERVO_COMPOSITION_END; break;
    }
    servo_webview_ime_composition(m_wv, s, text.toUtf8().constData());
}

void ServoWebView::imeDismissed() { TUULI_GUARD(); servo_webview_ime_dismissed(m_wv); }

void ServoWebView::editingAction(EditingAction action)
{
    TUULI_GUARD();
    ServoEditingAction a = SERVO_EDIT_COPY;
    switch (action) {
    case EditingAction::Copy: a = SERVO_EDIT_COPY; break;
    case EditingAction::Cut: a = SERVO_EDIT_CUT; break;
    case EditingAction::Paste: a = SERVO_EDIT_PASTE; break;
    case EditingAction::SelectAll: a = SERVO_EDIT_SELECT_ALL; break;
    }
    servo_webview_editing_action(m_wv, a);
}

void ServoWebView::requestContextMenu(const QPointF& css)
{
    TUULI_GUARD();
    servo_webview_request_context_menu(m_wv, static_cast<float>(css.x()), static_cast<float>(css.y()));
}

void ServoWebView::find(const QString& text, bool caseSensitive) { TUULI_GUARD(); servo_webview_find(m_wv, text.toUtf8().constData(), caseSensitive); }
void ServoWebView::findNext(bool forward) { TUULI_GUARD(); servo_webview_find_next(m_wv, forward); }
void ServoWebView::findClear() { TUULI_GUARD(); servo_webview_find_clear(m_wv); }
void ServoWebView::addUserStylesheet(const QString& id, const QString& css)
{
    TUULI_GUARD();
    servo_webview_add_user_stylesheet(m_wv, id.toUtf8().constData(), css.toUtf8().constData());
}
void ServoWebView::removeUserStylesheet(const QString& id) { TUULI_GUARD(); servo_webview_remove_user_stylesheet(m_wv, id.toUtf8().constData()); }
void ServoWebView::setUserAgentOverride(const QString& ua)
{
    TUULI_GUARD();
    servo_webview_set_user_agent(m_wv, ua.isEmpty() ? nullptr : ua.toUtf8().constData());
}
void ServoWebView::evaluateJavaScript(const QString& script)
{
    TUULI_GUARD();
    servo_webview_evaluate_javascript(m_wv, script.toUtf8().constData(), nullptr, nullptr);
}

QImage ServoWebView::capture()
{
    if (m_closed || !m_wv)
        return QImage();
    QMutexLocker lock(m_engine->renderLock());
    uint32_t w = 0, h = 0;
    uint8_t* px = servo_webview_capture(m_wv, &w, &h);
    if (!px || w == 0 || h == 0)
        return QImage();
    QImage img(px, static_cast<int>(w), static_cast<int>(h), static_cast<int>(w) * 4, QImage::Format_RGBA8888);
    QImage copy = img.copy();
    servo_pixels_free(px);
    return copy;
}

bool ServoWebView::paint()
{
    // Render thread; caller holds the render lock.
    if (m_closed || !m_wv)
        return false;
    return servo_webview_paint(m_wv);
}

void ServoWebView::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_client = nullptr;
    if (m_wv && m_engine && m_engine->isInitialized()) {
        QMutexLocker lock(m_engine->renderLock());
        servo_webview_close(m_wv);
    }
    m_wv = nullptr;
}

/* ---- Queued slots (GUI thread) ---------------------------------------- */

void ServoWebView::onUrlChanged(const QString& url)
{
    m_cachedUrl = QUrl(url);
    if (m_client) m_client->onUrlChanged(m_cachedUrl);
}
void ServoWebView::onTitleChanged(const QString& title)
{
    m_cachedTitle = title;
    if (m_client) m_client->onTitleChanged(title);
}
void ServoWebView::onLoadStatus(int status)
{
    if (m_client) m_client->onLoadStatusChanged(static_cast<LoadStatus>(status));
}
void ServoWebView::onFavicon(const QImage& icon) { if (m_client) m_client->onFaviconChanged(icon); }
void ServoWebView::onHistory(bool back, bool forward) { if (m_client) m_client->onHistoryChanged(back, forward); }
void ServoWebView::onFrameReady() { if (m_client) m_client->onFrameReady(); }
void ServoWebView::onViewport(double x, double y, double zoom, double cw, double ch)
{
    if (m_client) m_client->onViewportChanged(QPointF(x, y), zoom, QSizeF(cw, ch));
}
void ServoWebView::onShowIme(int type, const QString& text, bool multiline, const QRectF& rect)
{
    if (m_client) m_client->onImeShowRequested(static_cast<InputType>(type), text, multiline, rect);
}
void ServoWebView::onHideIme() { if (m_client) m_client->onImeHideRequested(); }
void ServoWebView::onImeSelection(const QString& text, int cursor, int anchor)
{
    if (m_client) m_client->onImeSelectionChanged(text, cursor, anchor);
}
void ServoWebView::onPermission(QObject* request)
{
    PermissionRequest* r = static_cast<PermissionRequest*>(request);
    if (m_client) m_client->onPermissionRequested(r); else r->deny();
}
void ServoWebView::onDialog(QObject* request)
{
    SimpleDialogRequest* r = static_cast<SimpleDialogRequest*>(request);
    if (m_client) m_client->onDialogRequested(r); else r->dismiss();
}
void ServoWebView::onContextMenu(const ContextMenuInfo& info) { if (m_client) m_client->onContextMenuRequested(info); }
void ServoWebView::onDownloadRequested(QObject* request)
{
    DownloadRequest* r = static_cast<DownloadRequest*>(request);
    if (m_client) m_client->onDownloadRequested(r); else r->reject();
}
void ServoWebView::onDownloadProgress(void* download, qint64 received, qint64 total)
{
    if (ServoDownloadRequest* r = m_downloads.value(static_cast<::ServoDownload*>(download)))
        emit r->progress(received, total);
}
void ServoWebView::onDownloadFinished(void* download, bool ok, const QString& error)
{
    ::ServoDownload* d = static_cast<::ServoDownload*>(download);
    if (ServoDownloadRequest* r = m_downloads.take(d)) {
        emit r->finished(ok, error);
        r->deleteLater();
    }
}
void ServoWebView::onMediaSession(const MediaSessionInfo& info) { if (m_client) m_client->onMediaSessionEvent(info); }
void ServoWebView::onNotification(const QString& title, const QString& body, const QString& icon)
{
    if (m_client) m_client->onNotificationRequested(title, body, QUrl(icon));
}
void ServoWebView::onClosed()
{
    m_wv = nullptr;
    m_closed = true;
    if (m_client) m_client->onClosed();
}

} // namespace Tuuli
