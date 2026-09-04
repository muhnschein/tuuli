/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "tab.h"
#include "prefs/useragent.h"
#include "tuuli_global.h"

#include <QDateTime>

namespace Tuuli {

Tab::Tab(int id, bool isPrivate, QObject* parent)
    : QObject(parent), m_id(id), m_private(isPrivate)
{
    m_lastActive = QDateTime::currentMSecsSinceEpoch();
}

Tab::~Tab()
{
    detachWebView();
}

QString Tab::displayTitle() const
{
    if (!m_title.isEmpty())
        return m_title;
    const QUrl u = m_url.isEmpty() ? m_requestedUrl : m_url;
    if (u.isEmpty())
        return QString();
    QString host = u.host();
    return host.isEmpty() ? u.toString() : host;
}

QUrl Tab::faviconSource() const
{
    if (m_favicon.isNull())
        return QUrl();
    return QUrl(QStringLiteral("image://tuuli/favicon/%1/%2").arg(m_id).arg(m_faviconRevision));
}

QUrl Tab::thumbnailSource() const
{
    if (m_thumbnail.isNull())
        return QUrl();
    return QUrl(QStringLiteral("image://tuuli/thumbnail/%1/%2").arg(m_id).arg(m_thumbnailRevision));
}

void Tab::setRequestedUrl(const QUrl& url)
{
    m_requestedUrl = url;
    if (m_url.isEmpty()) {
        m_url = url;
        emit urlChanged();
    }
}

void Tab::setRestoredState(const QString& title, const QPointF& scroll, qreal zoom, bool desktopMode)
{
    m_title = title;
    m_scroll = scroll;
    m_pinchZoom = zoom > 0 ? zoom : 1.0;
    m_desktopMode = desktopMode;
    emit titleChanged();
    emit viewportChanged();
    emit desktopModeChanged();
}

void Tab::attachWebView(WebViewHandle* handle)
{
    if (m_handle == handle)
        return;
    detachWebView();
    m_handle = handle;
    if (m_handle) {
        m_handle->setParent(this);
        applyDesktopMode();
        const QUrl target = m_requestedUrl.isEmpty() ? m_url : m_requestedUrl;
        if (!target.isEmpty())
            m_handle->load(target);
        if (!m_scroll.isNull() || m_pinchZoom != 1.0) {
            // Restored viewport; applied once the page has content.
            connect(this, &Tab::loadFinished, this, [this]() {
                if (!m_handle)
                    return;
                if (m_pinchZoom != 1.0)
                    m_handle->setPinchZoom(m_pinchZoom);
                if (!m_scroll.isNull())
                    m_handle->scrollTo(m_scroll);
            }, Qt::UniqueConnection);
        }
    }
    emit hasWebViewChanged();
}

void Tab::detachWebView()
{
    if (!m_handle)
        return;
    WebViewHandle* h = m_handle;
    m_handle = nullptr;
    h->close();
    h->deleteLater();
    // Remember where we were so re-materialising restores it.
    m_requestedUrl = m_url;
    m_committed = false;
    if (m_loading) {
        m_loading = false;
        emit loadingChanged();
    }
    emit hasWebViewChanged();
}

void Tab::setThumbnail(const QImage& image)
{
    m_thumbnail = image;
    ++m_thumbnailRevision;
    emit thumbnailChanged();
}

void Tab::touchLastActive()
{
    m_lastActive = QDateTime::currentMSecsSinceEpoch();
    emit lastActiveChanged();
}

void Tab::load(const QUrl& url)
{
    if (url.isEmpty())
        return;
    m_requestedUrl = url;
    m_committed = false;
    if (m_handle) {
        m_handle->load(url);
    } else {
        m_url = url;
        m_title.clear();
        emit urlChanged();
        emit titleChanged();
    }
}

void Tab::reload() { if (m_handle) m_handle->reload(); }
void Tab::stop() { if (m_handle) m_handle->stop(); }
void Tab::goBack() { if (m_handle) m_handle->goBack(); }
void Tab::goForward() { if (m_handle) m_handle->goForward(); }

void Tab::findInPage(const QString& text, bool caseSensitive)
{
    m_pendingFind = text;
    if (m_handle)
        m_handle->find(text, caseSensitive);
}
void Tab::findNext() { if (m_handle) m_handle->findNext(true); }
void Tab::findPrevious() { if (m_handle) m_handle->findNext(false); }
void Tab::clearFind()
{
    m_pendingFind.clear();
    if (m_handle)
        m_handle->findClear();
}

void Tab::setDesktopMode(bool on)
{
    if (m_desktopMode == on)
        return;
    m_desktopMode = on;
    applyDesktopMode();
    emit desktopModeChanged();
    if (m_handle)
        m_handle->reload();
}

void Tab::applyDesktopMode()
{
    if (!m_handle)
        return;
    if (m_desktopMode)
        m_handle->setUserAgentOverride(UserAgent::desktop(QString(), QStringLiteral(TUULI_VERSION_STRING)));
    else
        m_handle->setUserAgentOverride(QString());
}

void Tab::setUserStylesheet(const QString& id, const QString& css)
{
    if (m_handle)
        m_handle->addUserStylesheet(id, css);
}

void Tab::removeUserStylesheet(const QString& id)
{
    if (m_handle)
        m_handle->removeUserStylesheet(id);
}

/* ---- WebViewClient ---------------------------------------------------- */

void Tab::onUrlChanged(const QUrl& url)
{
    // A requested URL is shown before the engine confirms it; the engine's
    // first report of that same URL is still a navigation (history, filters).
    const bool changedForUi = (m_url != url);
    const bool navigation = changedForUi || !m_committed;
    m_url = url;
    m_committed = true;
    m_requestedUrl.clear();
    if (changedForUi)
        emit urlChanged();
    if (navigation) {
        emit urlChangedSignal(url);
        emit navigationCommitted(m_url, m_title, m_private);
    }
}

void Tab::onTitleChanged(const QString& title)
{
    if (m_title == title)
        return;
    m_title = title;
    emit titleChanged();
    emit titleChangedSignal(title);
    emit navigationCommitted(m_url, m_title, m_private);
}

void Tab::onLoadStatusChanged(LoadStatus status)
{
    const bool loading = status != LoadStatus::Complete;
    if (loading != m_loading) {
        m_loading = loading;
        emit loadingChanged();
    }
    if (status == LoadStatus::Complete)
        emit loadFinished();
}

void Tab::onFaviconChanged(const QImage& icon)
{
    m_favicon = icon;
    ++m_faviconRevision;
    emit faviconChanged();
}

void Tab::onHistoryChanged(bool canGoBack, bool canGoForward)
{
    if (m_canGoBack == canGoBack && m_canGoForward == canGoForward)
        return;
    m_canGoBack = canGoBack;
    m_canGoForward = canGoForward;
    emit historyChanged();
}

void Tab::onFrameReady()
{
    emit frameReadySignal();
}

void Tab::onViewportChanged(const QPointF& cssScroll, qreal pinchZoom, const QSizeF& cssContentSize)
{
    m_scroll = cssScroll;
    m_pinchZoom = pinchZoom;
    m_contentSize = cssContentSize;
    emit viewportChanged();
}

void Tab::onImeShowRequested(InputType type, const QString& text, bool multiline, const QRectF& cssRect)
{
    emit imeShow(static_cast<int>(type), text, multiline, cssRect);
}

void Tab::onImeHideRequested() { emit imeHide(); }

void Tab::onImeSelectionChanged(const QString& text, int cursor, int anchor)
{
    emit imeSelection(text, cursor, anchor);
}

void Tab::onPermissionRequested(PermissionRequest* request) { emit permissionRequest(request); }
void Tab::onDialogRequested(SimpleDialogRequest* request) { emit dialogRequest(request); }

void Tab::onContextMenuRequested(const ContextMenuInfo& info)
{
    emit contextMenu(info.cssPos, info.linkUrl, info.imageUrl, info.selectedText, info.editable);
}

void Tab::onDownloadRequested(DownloadRequest* request) { emit downloadRequest(request); }
void Tab::onMediaSessionEvent(const MediaSessionInfo& info) { emit mediaSession(info); }

void Tab::onNotificationRequested(const QString& title, const QString& body, const QUrl& icon)
{
    emit notification(title, body, icon);
}

void Tab::onNewWebViewRequested(const QUrl& url) { emit newTabRequested(url, m_private); }
void Tab::onClosed() { emit closeRequested(); }

} // namespace Tuuli
