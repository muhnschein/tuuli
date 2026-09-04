/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_ENGINE_H
#define TUULI_ENGINE_H

/*
 * Tuuli::Engine -- the seam between the Qt/QML world and the web engine.
 *
 * Spec 4.1: "Design the C++ shim behind an interface that could later be
 * swapped for an IPC proxy without touching QML."  Nothing above this
 * header (views, models, QML) knows what servo_capi is.  Two
 * implementations exist:
 *
 *   ServoEngine  -- servo_capi over libservo.so (the product)
 *   MockEngine   -- in-process fake used by unit tests and for UI iteration
 *                   on hosts and in the emulator without an engine build
 *
 * Threading contract (spec 4.2):
 *   - Every method on Engine and WebViewHandle is called on the GUI thread
 *     EXCEPT WebViewHandle::paint() and Engine::initializeOnRenderThread() /
 *     Engine::shutdownOnRenderThread(), which run on the Qt render thread
 *     with the scene-graph GL context current.
 *   - Engine::renderLock() serialises the render thread's paint against
 *     GUI-thread engine calls.  Callers of paint() hold it; Engine
 *     implementations take it inside every GUI-thread entry point.
 *   - WebViewClient callbacks are always delivered on the GUI thread.
 */

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace Tuuli {

enum class LoadStatus { Started, HeadParsed, Complete };

enum class TouchPhase { Down, Move, Up, Cancel };

enum class InputType {
    None, Text, Url, Email, Number, Password, Tel, Search,
    Date, Time, DateTime, Month, Week, Color
};

enum class PermissionKind {
    Geolocation, Notifications, Camera, Microphone, PersistentStorage,
    Midi, Bluetooth, ClipboardRead, ClipboardWrite
};

enum class CompositionState { Start, Update, End };

enum class EditingAction { Copy, Cut, Paste, SelectAll };

enum class SiteDataKind : unsigned {
    Cookies = 1u << 0,
    LocalStorage = 1u << 1,
    SessionStorage = 1u << 2,
    HttpCache = 1u << 3,
    All = 0xFFFFu
};

enum class MediaSessionEvent { Metadata, Playing, Paused, None, Position };

struct ProxyConfig {
    QString http;      // "host:port", empty = direct
    QString https;
    QStringList noProxy;
    QUrl pacUrl;
    bool isDirect() const { return http.isEmpty() && https.isEmpty() && pacUrl.isEmpty(); }
    bool operator==(const ProxyConfig& o) const
    {
        return http == o.http && https == o.https && noProxy == o.noProxy && pacUrl == o.pacUrl;
    }
    bool operator!=(const ProxyConfig& o) const { return !(*this == o); }
};

struct EngineConfig {
    QString userAgent;                 // empty = engine default for platform
    bool mobilePlatform = true;        // spec 5.4
    QString certificatePath;           // spec 8.1
    QString dataDir;                   // cookies, storage
    QString cacheDir;
    ProxyConfig proxy;
    QStringList prefs;                 // "name=value"
    bool hardwareVideoDecode = true;   // spec 8.2
    int layoutThreads = 0;
};

struct ContextMenuInfo {
    QPointF cssPos;
    QUrl linkUrl;
    QUrl imageUrl;
    QString selectedText;
    bool editable = false;
};

struct MediaSessionInfo {
    MediaSessionEvent event = MediaSessionEvent::None;
    QString title, artist, album;
    double positionSeconds = 0;
    double durationSeconds = 0;
};

/* One-shot async request objects.  Exactly one of allow()/deny() must be
 * called; the object deletes itself afterwards. */
class PermissionRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString origin READ origin CONSTANT)
    Q_PROPERTY(int kind READ kindValue CONSTANT)
    Q_PROPERTY(QString kindName READ kindName CONSTANT)
public:
    PermissionRequest(PermissionKind kind, const QString& origin, QObject* parent = nullptr)
        : QObject(parent), m_kind(kind), m_origin(origin) {}
    PermissionKind kind() const { return m_kind; }
    int kindValue() const { return static_cast<int>(m_kind); }
    QString kindName() const;
    QString origin() const { return m_origin; }
    Q_INVOKABLE void allow() { if (!m_answered) { m_answered = true; onAllow(); deleteLater(); } }
    Q_INVOKABLE void deny() { if (!m_answered) { m_answered = true; onDeny(); deleteLater(); } }
    bool answered() const { return m_answered; }
    static QString kindName(PermissionKind kind);
protected:
    virtual void onAllow() = 0;
    virtual void onDeny() = 0;
private:
    PermissionKind m_kind;
    QString m_origin;
    bool m_answered = false;
};

class SimpleDialogRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int kind READ kind CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(QString defaultValue READ defaultValue CONSTANT)
public:
    enum Kind { Alert, Confirm, Prompt };
    Q_ENUM(Kind)
    SimpleDialogRequest(Kind kind, const QString& message, const QString& defaultValue, QObject* parent = nullptr)
        : QObject(parent), m_kind(kind), m_message(message), m_default(defaultValue) {}
    int kind() const { return m_kind; }
    QString message() const { return m_message; }
    QString defaultValue() const { return m_default; }
    Q_INVOKABLE void accept(const QString& value = QString()) { if (!m_done) { m_done = true; onAccept(value); deleteLater(); } }
    Q_INVOKABLE void dismiss() { if (!m_done) { m_done = true; onDismiss(); deleteLater(); } }
protected:
    virtual void onAccept(const QString& value) = 0;
    virtual void onDismiss() = 0;
private:
    Kind m_kind;
    QString m_message, m_default;
    bool m_done = false;
};

class DownloadRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url CONSTANT)
    Q_PROPERTY(QString suggestedName READ suggestedName CONSTANT)
    Q_PROPERTY(QString mimeType READ mimeType CONSTANT)
    Q_PROPERTY(qint64 totalBytes READ totalBytes CONSTANT)
public:
    DownloadRequest(const QUrl& url, const QString& name, const QString& mime, qint64 total, QObject* parent = nullptr)
        : QObject(parent), m_url(url), m_name(name), m_mime(mime), m_total(total) {}
    QUrl url() const { return m_url; }
    QString suggestedName() const { return m_name; }
    QString mimeType() const { return m_mime; }
    qint64 totalBytes() const { return m_total; }
    Q_INVOKABLE void accept(const QString& destinationPath) { if (!m_done) { m_done = true; onAccept(destinationPath); } }
    Q_INVOKABLE void reject() { if (!m_done) { m_done = true; onReject(); deleteLater(); } }
    Q_INVOKABLE void cancel() { onCancel(); }
signals:
    void progress(qint64 received, qint64 total);
    void finished(bool ok, const QString& error);
protected:
    virtual void onAccept(const QString& destinationPath) = 0;
    virtual void onReject() = 0;
    virtual void onCancel() = 0;
private:
    QUrl m_url;
    QString m_name, m_mime;
    qint64 m_total;
    bool m_done = false;
};

/* Receives engine events for one webview.  All calls on the GUI thread. */
class WebViewClient
{
public:
    virtual ~WebViewClient() {}
    virtual void onUrlChanged(const QUrl& url) = 0;
    virtual void onTitleChanged(const QString& title) = 0;
    virtual void onLoadStatusChanged(LoadStatus status) = 0;
    virtual void onFaviconChanged(const QImage& icon) = 0;
    virtual void onHistoryChanged(bool canGoBack, bool canGoForward) = 0;
    virtual void onFrameReady() = 0;
    virtual void onViewportChanged(const QPointF& cssScroll, qreal pinchZoom, const QSizeF& cssContentSize) = 0;
    virtual void onImeShowRequested(InputType type, const QString& text, bool multiline, const QRectF& cssRect) = 0;
    virtual void onImeHideRequested() = 0;
    virtual void onImeSelectionChanged(const QString& text, int cursor, int anchor) = 0;
    virtual void onPermissionRequested(PermissionRequest* request) = 0;
    virtual void onDialogRequested(SimpleDialogRequest* request) = 0;
    virtual void onContextMenuRequested(const ContextMenuInfo& info) = 0;
    virtual void onDownloadRequested(DownloadRequest* request) = 0;
    virtual void onMediaSessionEvent(const MediaSessionInfo& info) = 0;
    virtual void onNotificationRequested(const QString& title, const QString& body, const QUrl& icon) = 0;
    virtual void onNewWebViewRequested(const QUrl& url) = 0;
    virtual void onClosed() = 0;
};

/* One engine webview.  GUI thread unless noted. */
class WebViewHandle : public QObject
{
    Q_OBJECT
public:
    explicit WebViewHandle(QObject* parent = nullptr) : QObject(parent) {}

    virtual bool isPrivate() const = 0;
    /* Re-targets callbacks, e.g. when a tab adopts an engine-created view. */
    virtual void setClient(WebViewClient* client) = 0;

    virtual void load(const QUrl& url) = 0;
    virtual void reload() = 0;
    virtual void stop() = 0;
    virtual void goBack() = 0;
    virtual void goForward() = 0;

    virtual void setVisible(bool visible) = 0;
    virtual void setFocused(bool focused) = 0;
    virtual void setSize(const QSize& devicePx) = 0;
    virtual void setViewportRect(const QRect& devicePx) = 0;
    virtual void setDevicePixelRatio(qreal dpr) = 0;
    virtual void setPinchZoom(qreal zoom) = 0;
    virtual void setPageZoom(qreal zoom) = 0;
    virtual void scrollTo(const QPointF& css) = 0;

    virtual void touch(TouchPhase phase, int id, const QPointF& css) = 0;
    virtual void key(bool down, const QString& key, Qt::KeyboardModifiers mods) = 0;
    virtual void imeComposition(CompositionState state, const QString& text) = 0;
    virtual void imeDismissed() = 0;
    virtual void editingAction(EditingAction action) = 0;

    virtual void requestContextMenu(const QPointF& css) = 0;

    virtual void find(const QString& text, bool caseSensitive) = 0;
    virtual void findNext(bool forward) = 0;
    virtual void findClear() = 0;

    virtual void addUserStylesheet(const QString& id, const QString& css) = 0;
    virtual void removeUserStylesheet(const QString& id) = 0;
    virtual void setUserAgentOverride(const QString& ua) = 0;
    virtual void evaluateJavaScript(const QString& script) = 0;
    virtual QImage capture() = 0;

    /* Render thread.  Engine::renderLock() is held by the caller. Returns
     * true if content was painted into the currently bound FBO. */
    virtual bool paint() = 0;

    virtual void close() = 0;
};

/* Embedder-owned rendering context (spec 5.2).  Implemented by
 * QtRenderingContext over the scene-graph QOpenGLContext.  Only ever
 * touched on the render thread. */
class RenderingContext
{
public:
    virtual ~RenderingContext() {}
    virtual QSize size() const = 0;               // current FBO size, device px
    virtual unsigned framebufferObject() const = 0;
    virtual void* procAddress(const char* name) = 0;
    virtual bool makeCurrent() = 0;               // no-op on Qt: already current
    virtual int glMajorVersion() const = 0;
    virtual int glMinorVersion() const = 0;
    virtual bool isGles() const = 0;
};

class Engine : public QObject
{
    Q_OBJECT
public:
    explicit Engine(QObject* parent = nullptr) : QObject(parent) {}

    virtual QString name() const = 0;
    virtual QString versionString() const = 0;

    /* GUI thread.  Stores config; nothing is created yet. */
    virtual void configure(const EngineConfig& config) = 0;
    virtual EngineConfig config() const = 0;

    /* Render thread, GL current.  Idempotent. Emits initialized() (queued). */
    virtual bool initializeOnRenderThread(RenderingContext* ctx) = 0;
    virtual void shutdownOnRenderThread() = 0;
    virtual bool isInitialized() const = 0;

    /* GUI thread. */
    virtual WebViewHandle* createWebView(WebViewClient* client, bool isPrivate,
                                         qreal dpr, const QSize& devicePx) = 0;
    virtual void spinEventLoop() = 0;
    virtual void setPref(const QString& name, const QString& value) = 0;
    virtual void setProxy(const ProxyConfig& proxy) = 0;
    virtual void clearSiteData(const QString& origin, unsigned kinds) = 0;

    QMutex* renderLock() { return &m_renderLock; }

signals:
    void initialized();
    void shutDown();
    void crashed(const QString& reason, const QString& backtrace);
    /* The engine was torn down with the GL context (spec 5.2) and will be
     * recreated on the next render; tabs must be re-created from the
     * session store. */
    void renderContextLost();

private:
    QMutex m_renderLock;
};

} // namespace Tuuli

Q_DECLARE_METATYPE(Tuuli::LoadStatus)
Q_DECLARE_METATYPE(Tuuli::InputType)
Q_DECLARE_METATYPE(Tuuli::ContextMenuInfo)
Q_DECLARE_METATYPE(Tuuli::MediaSessionInfo)

#endif
