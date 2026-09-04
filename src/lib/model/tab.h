/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TAB_H
#define TUULI_TAB_H

/*
 * One browser tab.  Owns (optionally) an engine webview and receives its
 * callbacks as the WebViewClient.  A tab can exist without a webview: tabs
 * restored from the session are materialised lazily, and least-recently
 * used webviews are dropped to stay inside the memory budget (spec 11).
 */

#include "engine/engine.h"

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QUrl>

namespace Tuuli {

class Tab : public QObject, public WebViewClient
{
    Q_OBJECT
    Q_PROPERTY(int tabId READ tabId CONSTANT)
    Q_PROPERTY(QUrl url READ url NOTIFY urlChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString displayTitle READ displayTitle NOTIFY titleChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY historyChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY historyChanged)
    Q_PROPERTY(bool isPrivate READ isPrivate CONSTANT)
    Q_PROPERTY(bool hasFavicon READ hasFavicon NOTIFY faviconChanged)
    Q_PROPERTY(QUrl faviconSource READ faviconSource NOTIFY faviconChanged)
    Q_PROPERTY(bool hasThumbnail READ hasThumbnail NOTIFY thumbnailChanged)
    Q_PROPERTY(QUrl thumbnailSource READ thumbnailSource NOTIFY thumbnailChanged)
    Q_PROPERTY(bool desktopMode READ desktopMode WRITE setDesktopMode NOTIFY desktopModeChanged)
    Q_PROPERTY(bool hasWebView READ hasWebView NOTIFY hasWebViewChanged)
    Q_PROPERTY(QPointF scrollOffset READ scrollOffset NOTIFY viewportChanged)
    Q_PROPERTY(qreal pinchZoom READ pinchZoom NOTIFY viewportChanged)
    Q_PROPERTY(QSizeF contentSize READ contentSize NOTIFY viewportChanged)
    Q_PROPERTY(qint64 lastActive READ lastActive NOTIFY lastActiveChanged)

public:
    Tab(int id, bool isPrivate, QObject* parent = nullptr);
    ~Tab();

    int tabId() const { return m_id; }
    QUrl url() const { return m_url; }
    QString title() const { return m_title; }
    QString displayTitle() const;
    bool loading() const { return m_loading; }
    bool canGoBack() const { return m_canGoBack; }
    bool canGoForward() const { return m_canGoForward; }
    bool isPrivate() const { return m_private; }
    bool hasFavicon() const { return !m_favicon.isNull(); }
    QUrl faviconSource() const;
    QImage favicon() const { return m_favicon; }
    bool hasThumbnail() const { return !m_thumbnail.isNull(); }
    QUrl thumbnailSource() const;
    QImage thumbnail() const { return m_thumbnail; }
    bool desktopMode() const { return m_desktopMode; }
    bool hasWebView() const { return m_handle != nullptr; }
    QPointF scrollOffset() const { return m_scroll; }
    qreal pinchZoom() const { return m_pinchZoom; }
    QSizeF contentSize() const { return m_contentSize; }
    qint64 lastActive() const { return m_lastActive; }

    WebViewHandle* handle() const { return m_handle; }

    /* Session plumbing (TabModel). */
    void setRequestedUrl(const QUrl& url);
    QUrl requestedUrl() const { return m_requestedUrl; }
    void setRestoredState(const QString& title, const QPointF& scroll, qreal zoom, bool desktopMode);
    void attachWebView(WebViewHandle* handle);
    void detachWebView();
    void setThumbnail(const QImage& image);
    void touchLastActive();

    /* User actions. */
    Q_INVOKABLE void load(const QUrl& url);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void findInPage(const QString& text, bool caseSensitive = false);
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();
    Q_INVOKABLE void clearFind();
    void setDesktopMode(bool on);
    void setUserStylesheet(const QString& id, const QString& css);
    void removeUserStylesheet(const QString& id);

    /* WebViewClient, GUI thread. */
    void onUrlChanged(const QUrl& url) override;
    void onTitleChanged(const QString& title) override;
    void onLoadStatusChanged(LoadStatus status) override;
    void onFaviconChanged(const QImage& icon) override;
    void onHistoryChanged(bool canGoBack, bool canGoForward) override;
    void onFrameReady() override;
    void onViewportChanged(const QPointF& cssScroll, qreal pinchZoom, const QSizeF& cssContentSize) override;
    void onImeShowRequested(InputType type, const QString& text, bool multiline, const QRectF& cssRect) override;
    void onImeHideRequested() override;
    void onImeSelectionChanged(const QString& text, int cursor, int anchor) override;
    void onPermissionRequested(PermissionRequest* request) override;
    void onDialogRequested(SimpleDialogRequest* request) override;
    void onContextMenuRequested(const ContextMenuInfo& info) override;
    void onDownloadRequested(DownloadRequest* request) override;
    void onMediaSessionEvent(const MediaSessionInfo& info) override;
    void onNotificationRequested(const QString& title, const QString& body, const QUrl& icon) override;
    void onNewWebViewRequested(const QUrl& url) override;
    void onClosed() override;

signals:
    void urlChangedSignal(const QUrl& url);
    void titleChangedSignal(const QString& title);
    void urlChanged();
    void titleChanged();
    void loadingChanged();
    void historyChanged();
    void faviconChanged();
    void thumbnailChanged();
    void desktopModeChanged();
    void hasWebViewChanged();
    void viewportChanged();
    void lastActiveChanged();
    void loadFinished();
    void frameReadySignal();
    void imeShow(int inputType, const QString& text, bool multiline, const QRectF& cssRect);
    void imeHide();
    void imeSelection(const QString& text, int cursor, int anchor);
    void permissionRequest(Tuuli::PermissionRequest* request);
    void dialogRequest(Tuuli::SimpleDialogRequest* request);
    void contextMenu(const QPointF& cssPos, const QUrl& linkUrl, const QUrl& imageUrl,
                     const QString& selectedText, bool editable);
    void downloadRequest(Tuuli::DownloadRequest* request);
    void mediaSession(const Tuuli::MediaSessionInfo& info);
    void notification(const QString& title, const QString& body, const QUrl& icon);
    void newTabRequested(const QUrl& url, bool isPrivate);
    void closeRequested();
    void navigationCommitted(const QUrl& url, const QString& title, bool isPrivate);

private:
    void applyDesktopMode();

    int m_id;
    bool m_private;
    QUrl m_url;
    QUrl m_requestedUrl;
    bool m_committed = false;   // m_url came from the engine, not a request
    QString m_title;
    bool m_loading = false;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
    QImage m_favicon;
    int m_faviconRevision = 0;
    QImage m_thumbnail;
    int m_thumbnailRevision = 0;
    bool m_desktopMode = false;
    QPointF m_scroll;
    qreal m_pinchZoom = 1.0;
    QSizeF m_contentSize;
    qint64 m_lastActive = 0;
    WebViewHandle* m_handle = nullptr;
    QString m_pendingFind;
};

} // namespace Tuuli

#endif
