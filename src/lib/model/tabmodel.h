/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TABMODEL_H
#define TUULI_TABMODEL_H

#include "engine/engine.h"
#include "sessionstore.h"
#include "tab.h"

#include <QAbstractListModel>
#include <QSize>
#include <QVector>

namespace Tuuli {

/* All open tabs, private and not, in user order.  Owns the Tab objects and
 * decides which of them hold a live engine webview. */
class TabModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(Tuuli::Tab* currentTab READ currentTab NOTIFY currentTabChanged)
    Q_PROPERTY(int privateCount READ privateCount NOTIFY countChanged)
    Q_PROPERTY(int maxLiveWebViews READ maxLiveWebViews WRITE setMaxLiveWebViews NOTIFY maxLiveWebViewsChanged)

public:
    enum Roles {
        TabRole = Qt::UserRole + 1,
        UrlRole,
        TitleRole,
        PrivateRole,
        LoadingRole,
        FaviconRole,
        ThumbnailRole,
        ActiveRole,
        TabIdRole
    };

    explicit TabModel(Engine* engine, QObject* parent = nullptr);
    ~TabModel();

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_tabs.size(); }
    int privateCount() const;
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    Tab* currentTab() const;
    int maxLiveWebViews() const { return m_maxLive; }
    void setMaxLiveWebViews(int n);

    /* Viewport geometry every new webview is created with; the view item
     * keeps this current. */
    void setViewportGeometry(const QSize& devicePx, qreal dpr);
    QSize viewportSize() const { return m_viewportSize; }
    qreal devicePixelRatio() const { return m_dpr; }

    Q_INVOKABLE Tuuli::Tab* newTab(const QUrl& url, bool isPrivate = false, bool activate = true);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void closeTabById(int tabId);
    Q_INVOKABLE void closeAll();
    Q_INVOKABLE void closeAllPrivate();
    Q_INVOKABLE void moveTab(int from, int to);
    Q_INVOKABLE void activate(int index) { setCurrentIndex(index); }
    Q_INVOKABLE Tuuli::Tab* tabAt(int index) const;
    Q_INVOKABLE int indexOf(Tuuli::Tab* tab) const;
    Q_INVOKABLE int indexOfId(int tabId) const;

    /* Materialise a webview for `tab` if it has none, dropping least
     * recently used ones over the budget. Requires an initialised engine. */
    bool ensureWebView(Tab* tab);
    int liveWebViewCount() const;

    /* Session (spec 8.4).  Private tabs are excluded from snapshots. */
    Session snapshot() const;
    void restore(const Session& session);

signals:
    void countChanged();
    void currentIndexChanged();
    void currentTabChanged();
    void maxLiveWebViewsChanged();
    void tabAdded(Tuuli::Tab* tab);
    void tabClosed(int tabId);
    /* Anything that should trigger a session save. */
    void sessionChanged();

private:
    void onEngineInitialized();
    void onRenderContextLost();
    void connectTab(Tab* tab);
    void trimLiveWebViews(Tab* keep);
    void emitRowChanged(Tab* tab, const QVector<int>& roles);

    Engine* m_engine;
    QVector<Tab*> m_tabs;
    int m_current = -1;
    int m_nextId = 1;
    int m_maxLive = 8;
    QSize m_viewportSize = QSize(1080, 2260);
    qreal m_dpr = 2.0;
};

} // namespace Tuuli

#endif
