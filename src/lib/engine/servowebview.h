/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_SERVOWEBVIEW_H
#define TUULI_SERVOWEBVIEW_H

#include "engine.h"

#include <QHash>
#include <QImage>

struct ServoWebView;
struct ServoDownload;

namespace Tuuli {

class ServoEngine;
class ServoDownloadRequest;

class ServoWebView : public WebViewHandle
{
    Q_OBJECT
public:
    ServoWebView(ServoEngine* engine, WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx);
    /* Creates an auxiliary webview (window.open) with `parent` as opener,
     * installing this wrapper's callbacks at creation. */
    struct AuxiliaryTag {};
    ServoWebView(ServoEngine* engine, ::ServoWebView* parent, bool isPrivate, AuxiliaryTag);
    ~ServoWebView();

    void setClient(WebViewClient* client) override;
    WebViewClient* client() const { return m_client; }

    bool isPrivate() const override { return m_private; }
    void load(const QUrl& url) override;
    void reload() override;
    void stop() override;
    void goBack() override;
    void goForward() override;
    void setVisible(bool visible) override;
    void setFocused(bool focused) override;
    void setSize(const QSize& devicePx) override;
    void setViewportRect(const QRect& devicePx) override;
    void setDevicePixelRatio(qreal dpr) override;
    void setPinchZoom(qreal zoom) override;
    void setPageZoom(qreal zoom) override;
    void scrollTo(const QPointF& css) override;
    void touch(TouchPhase phase, int id, const QPointF& css) override;
    void key(bool down, const QString& key, Qt::KeyboardModifiers mods) override;
    void imeComposition(CompositionState state, const QString& text) override;
    void imeDismissed() override;
    void editingAction(EditingAction action) override;
    void requestContextMenu(const QPointF& css) override;
    void find(const QString& text, bool caseSensitive) override;
    void findNext(bool forward) override;
    void findClear() override;
    void addUserStylesheet(const QString& id, const QString& css) override;
    void removeUserStylesheet(const QString& id) override;
    void setUserAgentOverride(const QString& ua) override;
    void evaluateJavaScript(const QString& script) override;
    QImage capture() override;
    bool paint() override;
    void close() override;

    ::ServoWebView* raw() const { return m_wv; }

    /* Queued targets for the C trampolines (GUI thread). */
    Q_INVOKABLE void onUrlChanged(const QString& url);
    Q_INVOKABLE void onTitleChanged(const QString& title);
    Q_INVOKABLE void onLoadStatus(int status);
    Q_INVOKABLE void onFavicon(const QImage& icon);
    Q_INVOKABLE void onHistory(bool back, bool forward);
    Q_INVOKABLE void onFrameReady();
    Q_INVOKABLE void onViewport(double x, double y, double zoom, double cw, double ch);
    Q_INVOKABLE void onShowIme(int type, const QString& text, bool multiline, const QRectF& rect);
    Q_INVOKABLE void onHideIme();
    Q_INVOKABLE void onImeSelection(const QString& text, int cursor, int anchor);
    Q_INVOKABLE void onPermission(QObject* request);
    Q_INVOKABLE void onDialog(QObject* request);
    Q_INVOKABLE void onContextMenu(const Tuuli::ContextMenuInfo& info);
    Q_INVOKABLE void onDownloadRequested(QObject* request);
    Q_INVOKABLE void onDownloadProgress(void* download, qint64 received, qint64 total);
    Q_INVOKABLE void onDownloadFinished(void* download, bool ok, const QString& error);
    Q_INVOKABLE void onMediaSession(const Tuuli::MediaSessionInfo& info);
    Q_INVOKABLE void onNotification(const QString& title, const QString& body, const QString& icon);
    Q_INVOKABLE void onClosed();

    void registerDownload(::ServoDownload* d, ServoDownloadRequest* r) { m_downloads.insert(d, r); }

private:
    struct Callbacks;
    friend struct Callbacks;
    void replayCached();

    ServoEngine* m_engine;
    WebViewClient* m_client;
    ::ServoWebView* m_wv = nullptr;
    bool m_private;
    bool m_closed = false;
    QUrl m_cachedUrl;
    QString m_cachedTitle;
    QHash<::ServoDownload*, ServoDownloadRequest*> m_downloads;
};

} // namespace Tuuli

#endif
