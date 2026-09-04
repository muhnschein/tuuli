/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_MOCKENGINE_H
#define TUULI_MOCKENGINE_H

/*
 * In-process fake engine.  Used by the unit tests and by
 * -DTUULI_ENGINE=mock builds so the Silica chrome can be iterated on the
 * host, in the emulator, or on a device without a libservo build.  It
 * "loads" pages by echoing the URL back as title, paints nothing (the
 * renderer draws a placeholder) and records every input it receives.
 */

#include "engine.h"

#include <QAtomicInt>
#include <QHash>
#include <QVector>

namespace Tuuli {

class MockEngine;

class MockWebView : public WebViewHandle
{
    Q_OBJECT
public:
    struct Touch { TouchPhase phase; int id; QPointF css; };
    struct Key { bool down; QString key; };

    MockWebView(MockEngine* engine, WebViewClient* client, bool isPrivate, qreal dpr, const QSize& size);

    bool isPrivate() const override { return m_private; }
    void setClient(WebViewClient* client) override { m_client = client; }
    void load(const QUrl& url) override;
    void reload() override;
    void stop() override;
    void goBack() override;
    void goForward() override;
    void setVisible(bool visible) override { m_visible = visible; }
    void setFocused(bool focused) override { m_focused = focused; }
    void setSize(const QSize& devicePx) override { m_size = devicePx; }
    void setViewportRect(const QRect& devicePx) override { m_viewport = devicePx; }
    void setDevicePixelRatio(qreal dpr) override { m_dpr = dpr; }
    void setPinchZoom(qreal zoom) override { m_pinchZoom = zoom; }
    void setPageZoom(qreal zoom) override { m_pageZoom = zoom; }
    void scrollTo(const QPointF& css) override { m_scroll = css; }
    void touch(TouchPhase phase, int id, const QPointF& css) override { touches.append({ phase, id, css }); }
    void key(bool down, const QString& key, Qt::KeyboardModifiers) override { keys.append({ down, key }); }
    void imeComposition(CompositionState state, const QString& text) override;
    void imeDismissed() override { ++imeDismissCount; }
    void editingAction(EditingAction action) override { actions.append(action); }
    void requestContextMenu(const QPointF& css) override;
    void find(const QString& text, bool caseSensitive) override { Q_UNUSED(caseSensitive); findText = text; }
    void findNext(bool forward) override { Q_UNUSED(forward); ++findNextCount; }
    void findClear() override { findText.clear(); }
    void addUserStylesheet(const QString& id, const QString& css) override { stylesheets.insert(id, css); }
    void removeUserStylesheet(const QString& id) override { stylesheets.remove(id); }
    void setUserAgentOverride(const QString& ua) override { userAgent = ua; }
    void evaluateJavaScript(const QString& script) override { scripts.append(script); }
    QImage capture() override;
    bool paint() override { ++paintCount; return false; }
    void close() override;

    /* Test helpers: push engine events into the client. */
    void simulateNavigation(const QUrl& url, const QString& title);
    void simulateFavicon(const QImage& icon);
    void simulateHistory(bool back, bool forward);
    void simulateImeShow(InputType type, const QString& text, bool multiline, const QRectF& rect);
    void simulateImeHide();
    void simulatePermission(PermissionKind kind, const QString& origin, bool* allowed);
    void simulateDownload(const QUrl& url, const QString& name, const QString& mime, qint64 total);
    void simulateContextMenu(const ContextMenuInfo& info);
    void simulateViewport(const QPointF& scroll, qreal zoom, const QSizeF& content = QSizeF());

    WebViewClient* client() const { return m_client; }
    QUrl url() const { return m_url; }
    QSize size() const { return m_size; }
    QRect viewport() const { return m_viewport; }
    qreal dpr() const { return m_dpr; }
    qreal pinchZoom() const { return m_pinchZoom; }
    QPointF scroll() const { return m_scroll; }
    bool visible() const { return m_visible; }
    bool focused() const { return m_focused; }
    bool closed() const { return m_closed; }

    QVector<Touch> touches;
    QVector<Key> keys;
    QVector<QString> compositions;
    QVector<EditingAction> actions;
    QVector<QString> scripts;
    QHash<QString, QString> stylesheets;
    QString userAgent;
    QString findText;
    int findNextCount = 0;
    int imeDismissCount = 0;
    int paintCount = 0;
    int loadCount = 0;
    int reloadCount = 0;
    QPointF contextMenuRequestedAt;

    /* When true, load() completes synchronously; tests set it. */
    bool synchronousLoads = false;

private:
    void completeLoad();

    MockEngine* m_engine;
    WebViewClient* m_client;
    bool m_private;
    qreal m_dpr;
    QSize m_size;
    QRect m_viewport;
    QUrl m_url;
    QVector<QUrl> m_history;
    int m_historyIndex = -1;
    qreal m_pinchZoom = 1.0;
    qreal m_pageZoom = 1.0;
    QPointF m_scroll;
    bool m_visible = false;
    bool m_focused = false;
    bool m_closed = false;
};

class MockEngine : public Engine
{
    Q_OBJECT
public:
    explicit MockEngine(QObject* parent = nullptr);

    QString name() const override { return QStringLiteral("mock"); }
    QString versionString() const override { return QStringLiteral("0.0.0-mock"); }
    void configure(const EngineConfig& config) override { m_config = config; }
    EngineConfig config() const override { return m_config; }
    bool initializeOnRenderThread(RenderingContext* ctx) override;
    void shutdownOnRenderThread() override;
    bool isInitialized() const override { return m_initialized.load() != 0; }
    WebViewHandle* createWebView(WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx) override;
    void spinEventLoop() override { ++spinCount; }
    void setPref(const QString& name, const QString& value) override { prefs.insert(name, value); }
    void setProxy(const ProxyConfig& proxy) override { m_config.proxy = proxy; ++proxyUpdates; }
    void clearSiteData(const QString& origin, unsigned kinds) override { clearedOrigins.append(origin); clearedKinds |= kinds; }

    /* Tests: initialise without a render thread. */
    void initializeForTests();

    QVector<MockWebView*> webViews;
    QHash<QString, QString> prefs;
    QStringList clearedOrigins;
    unsigned clearedKinds = 0;
    int spinCount = 0;
    int proxyUpdates = 0;
    int createdCount = 0;

private:
    EngineConfig m_config;
    QAtomicInt m_initialized;
};

} // namespace Tuuli

#endif
