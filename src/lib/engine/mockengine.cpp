/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "mockengine.h"

#include <QCoreApplication>
#include <QTimer>

namespace Tuuli {

namespace {

class MockPermissionRequest : public PermissionRequest
{
public:
    MockPermissionRequest(PermissionKind kind, const QString& origin, bool* out)
        : PermissionRequest(kind, origin), m_out(out) {}
protected:
    void onAllow() override { if (m_out) *m_out = true; }
    void onDeny() override { if (m_out) *m_out = false; }
private:
    bool* m_out;
};

class MockDownloadRequest : public DownloadRequest
{
public:
    using DownloadRequest::DownloadRequest;
    QString destination;
    bool rejected = false;
    bool cancelled = false;
protected:
    void onAccept(const QString& path) override
    {
        destination = path;
        QTimer::singleShot(0, this, [this]() {
            emit progress(totalBytes() > 0 ? totalBytes() / 2 : 512, totalBytes());
            emit progress(totalBytes() > 0 ? totalBytes() : 1024, totalBytes());
            emit finished(true, QString());
            deleteLater();
        });
    }
    void onReject() override { rejected = true; }
    void onCancel() override { cancelled = true; }
};

} // namespace

MockWebView::MockWebView(MockEngine* engine, WebViewClient* client, bool isPrivate, qreal dpr, const QSize& size)
    : WebViewHandle(nullptr), m_engine(engine), m_client(client), m_private(isPrivate), m_dpr(dpr), m_size(size)
{
    m_viewport = QRect(QPoint(0, 0), size);
}

void MockWebView::load(const QUrl& url)
{
    ++loadCount;
    m_url = url;
    // Truncate forward history.
    if (m_historyIndex >= 0 && m_historyIndex < m_history.size() - 1)
        m_history.resize(m_historyIndex + 1);
    m_history.append(url);
    m_historyIndex = m_history.size() - 1;
    if (m_client) {
        m_client->onLoadStatusChanged(LoadStatus::Started);
        m_client->onUrlChanged(url);
    }
    if (synchronousLoads)
        completeLoad();
    else
        QTimer::singleShot(0, this, &MockWebView::completeLoad);
}

void MockWebView::completeLoad()
{
    if (!m_client || m_closed)
        return;
    m_client->onTitleChanged(m_url.host().isEmpty() ? m_url.toString() : m_url.host());
    m_client->onHistoryChanged(m_historyIndex > 0, m_historyIndex < m_history.size() - 1);
    m_client->onLoadStatusChanged(LoadStatus::Complete);
    m_client->onFrameReady();
}

void MockWebView::reload()
{
    ++reloadCount;
    if (m_client) {
        m_client->onLoadStatusChanged(LoadStatus::Started);
        if (synchronousLoads)
            completeLoad();
        else
            QTimer::singleShot(0, this, &MockWebView::completeLoad);
    }
}

void MockWebView::stop()
{
    if (m_client)
        m_client->onLoadStatusChanged(LoadStatus::Complete);
}

void MockWebView::goBack()
{
    if (m_historyIndex <= 0)
        return;
    --m_historyIndex;
    m_url = m_history.at(m_historyIndex);
    if (m_client) {
        m_client->onUrlChanged(m_url);
        m_client->onHistoryChanged(m_historyIndex > 0, m_historyIndex < m_history.size() - 1);
    }
}

void MockWebView::goForward()
{
    if (m_historyIndex >= m_history.size() - 1)
        return;
    ++m_historyIndex;
    m_url = m_history.at(m_historyIndex);
    if (m_client) {
        m_client->onUrlChanged(m_url);
        m_client->onHistoryChanged(m_historyIndex > 0, m_historyIndex < m_history.size() - 1);
    }
}

void MockWebView::imeComposition(CompositionState state, const QString& text)
{
    Q_UNUSED(state);
    compositions.append(text);
}

void MockWebView::requestContextMenu(const QPointF& css)
{
    contextMenuRequestedAt = css;
    if (m_client) {
        ContextMenuInfo info;
        info.cssPos = css;
        m_client->onContextMenuRequested(info);
    }
}

QImage MockWebView::capture()
{
    QImage img(m_size.isEmpty() ? QSize(8, 8) : m_size / 8, QImage::Format_RGB32);
    img.fill(0xff2b2b2b);
    return img;
}

void MockWebView::close()
{
    m_closed = true;
    m_client = nullptr;
}

void MockWebView::simulateNavigation(const QUrl& url, const QString& title)
{
    m_url = url;
    if (!m_client) return;
    m_client->onUrlChanged(url);
    m_client->onTitleChanged(title);
    m_client->onLoadStatusChanged(LoadStatus::Complete);
}
void MockWebView::simulateFavicon(const QImage& icon) { if (m_client) m_client->onFaviconChanged(icon); }
void MockWebView::simulateHistory(bool back, bool forward) { if (m_client) m_client->onHistoryChanged(back, forward); }
void MockWebView::simulateImeShow(InputType type, const QString& text, bool multiline, const QRectF& rect)
{
    if (m_client) m_client->onImeShowRequested(type, text, multiline, rect);
}
void MockWebView::simulateImeHide() { if (m_client) m_client->onImeHideRequested(); }
void MockWebView::simulatePermission(PermissionKind kind, const QString& origin, bool* allowed)
{
    if (m_client) m_client->onPermissionRequested(new MockPermissionRequest(kind, origin, allowed));
}
void MockWebView::simulateDownload(const QUrl& url, const QString& name, const QString& mime, qint64 total)
{
    if (m_client) m_client->onDownloadRequested(new MockDownloadRequest(url, name, mime, total));
}
void MockWebView::simulateContextMenu(const ContextMenuInfo& info) { if (m_client) m_client->onContextMenuRequested(info); }
void MockWebView::simulateViewport(const QPointF& scroll, qreal zoom, const QSizeF& content) { if (m_client) m_client->onViewportChanged(scroll, zoom, content); }

/* ---- MockEngine ------------------------------------------------------- */

MockEngine::MockEngine(QObject* parent)
    : Engine(parent), m_initialized(0)
{
}

bool MockEngine::initializeOnRenderThread(RenderingContext* ctx)
{
    Q_UNUSED(ctx);
    if (m_initialized.fetchAndStoreOrdered(1) == 0)
        QMetaObject::invokeMethod(this, "initialized", Qt::QueuedConnection);
    return true;
}

void MockEngine::initializeForTests()
{
    if (m_initialized.fetchAndStoreOrdered(1) == 0)
        emit initialized();
}

void MockEngine::shutdownOnRenderThread()
{
    if (m_initialized.fetchAndStoreOrdered(0) != 0)
        QMetaObject::invokeMethod(this, "renderContextLost", Qt::QueuedConnection);
}

WebViewHandle* MockEngine::createWebView(WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx)
{
    if (!isInitialized())
        return nullptr;
    ++createdCount;
    MockWebView* wv = new MockWebView(this, client, isPrivate, dpr, devicePx);
    webViews.append(wv);
    connect(wv, &QObject::destroyed, this, [this, wv]() { webViews.removeAll(wv); });
    return wv;
}

} // namespace Tuuli
