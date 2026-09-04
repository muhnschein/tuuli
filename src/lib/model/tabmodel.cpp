/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "tabmodel.h"

#include <algorithm>

namespace Tuuli {

TabModel::TabModel(Engine* engine, QObject* parent)
    : QAbstractListModel(parent), m_engine(engine)
{
    if (m_engine) {
        connect(m_engine, &Engine::initialized, this, &TabModel::onEngineInitialized);
        connect(m_engine, &Engine::renderContextLost, this, &TabModel::onRenderContextLost);
    }
}

TabModel::~TabModel()
{
    qDeleteAll(m_tabs);
}

int TabModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_tabs.size();
}

QVariant TabModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tabs.size())
        return QVariant();
    Tab* tab = m_tabs.at(index.row());
    switch (role) {
    case TabRole: return QVariant::fromValue<QObject*>(tab);
    case UrlRole: return tab->url();
    case TitleRole: return tab->displayTitle();
    case PrivateRole: return tab->isPrivate();
    case LoadingRole: return tab->loading();
    case FaviconRole: return tab->faviconSource();
    case ThumbnailRole: return tab->thumbnailSource();
    case ActiveRole: return index.row() == m_current;
    case TabIdRole: return tab->tabId();
    }
    return QVariant();
}

QHash<int, QByteArray> TabModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[TabRole] = "tab";
    roles[UrlRole] = "url";
    roles[TitleRole] = "title";
    roles[PrivateRole] = "isPrivate";
    roles[LoadingRole] = "loading";
    roles[FaviconRole] = "favicon";
    roles[ThumbnailRole] = "thumbnail";
    roles[ActiveRole] = "active";
    roles[TabIdRole] = "tabId";
    return roles;
}

int TabModel::privateCount() const
{
    int n = 0;
    for (Tab* t : m_tabs)
        if (t->isPrivate())
            ++n;
    return n;
}

Tab* TabModel::currentTab() const
{
    return (m_current >= 0 && m_current < m_tabs.size()) ? m_tabs.at(m_current) : nullptr;
}

Tab* TabModel::tabAt(int index) const
{
    return (index >= 0 && index < m_tabs.size()) ? m_tabs.at(index) : nullptr;
}

int TabModel::indexOf(Tab* tab) const
{
    return m_tabs.indexOf(tab);
}

int TabModel::indexOfId(int tabId) const
{
    for (int i = 0; i < m_tabs.size(); ++i)
        if (m_tabs.at(i)->tabId() == tabId)
            return i;
    return -1;
}

void TabModel::setMaxLiveWebViews(int n)
{
    n = qMax(1, n);
    if (m_maxLive == n)
        return;
    m_maxLive = n;
    trimLiveWebViews(currentTab());
    emit maxLiveWebViewsChanged();
}

void TabModel::setViewportGeometry(const QSize& devicePx, qreal dpr)
{
    if (devicePx.isValid() && !devicePx.isEmpty())
        m_viewportSize = devicePx;
    if (dpr > 0)
        m_dpr = dpr;
}

void TabModel::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_tabs.size())
        return;
    if (index == m_current)
        return;
    const int old = m_current;
    m_current = index;
    Tab* previous = tabAt(old);
    Tab* next = currentTab();
    if (previous && previous->handle()) {
        previous->handle()->setFocused(false);
        previous->handle()->setVisible(false);
    }
    if (next) {
        next->touchLastActive();
        ensureWebView(next);
        if (next->handle()) {
            next->handle()->setVisible(true);
            next->handle()->setFocused(true);
        }
    }
    if (old >= 0 && old < m_tabs.size())
        emitRowChanged(m_tabs.at(old), { ActiveRole });
    if (next)
        emitRowChanged(next, { ActiveRole });
    emit currentIndexChanged();
    emit currentTabChanged();
    emit sessionChanged();
}

void TabModel::connectTab(Tab* tab)
{
    connect(tab, &Tab::urlChanged, this, [this, tab]() { emitRowChanged(tab, { UrlRole, TitleRole }); emit sessionChanged(); });
    connect(tab, &Tab::titleChanged, this, [this, tab]() { emitRowChanged(tab, { TitleRole }); emit sessionChanged(); });
    connect(tab, &Tab::loadingChanged, this, [this, tab]() { emitRowChanged(tab, { LoadingRole }); });
    connect(tab, &Tab::faviconChanged, this, [this, tab]() { emitRowChanged(tab, { FaviconRole }); });
    connect(tab, &Tab::thumbnailChanged, this, [this, tab]() { emitRowChanged(tab, { ThumbnailRole }); });
    connect(tab, &Tab::viewportChanged, this, &TabModel::sessionChanged);
    connect(tab, &Tab::desktopModeChanged, this, &TabModel::sessionChanged);
    connect(tab, &Tab::newTabRequested, this, [this](const QUrl& url, bool isPrivate) {
        newTab(url, isPrivate, true);
    });
    connect(tab, &Tab::closeRequested, this, [this, tab]() { closeTabById(tab->tabId()); });
}

Tab* TabModel::newTab(const QUrl& url, bool isPrivate, bool activate)
{
    Tab* tab = new Tab(m_nextId++, isPrivate, this);
    tab->setRequestedUrl(url);
    connectTab(tab);
    const int row = m_tabs.size();
    beginInsertRows(QModelIndex(), row, row);
    m_tabs.append(tab);
    endInsertRows();
    emit countChanged();
    emit tabAdded(tab);
    if (activate)
        setCurrentIndex(row);
    else
        emit sessionChanged();
    return tab;
}

void TabModel::closeTab(int index)
{
    if (index < 0 || index >= m_tabs.size())
        return;
    Tab* tab = m_tabs.at(index);
    const int id = tab->tabId();
    beginRemoveRows(QModelIndex(), index, index);
    m_tabs.removeAt(index);
    endRemoveRows();

    int newCurrent = m_current;
    if (index < m_current)
        newCurrent = m_current - 1;
    else if (index == m_current)
        newCurrent = qMin(index, m_tabs.size() - 1);
    if (m_tabs.isEmpty())
        newCurrent = -1;

    tab->deleteLater();
    emit countChanged();
    emit tabClosed(id);

    if (newCurrent != m_current) {
        m_current = -1; // force re-activation of the new current tab
        setCurrentIndex(newCurrent);
    } else {
        emit sessionChanged();
    }
}

void TabModel::closeTabById(int tabId)
{
    closeTab(indexOfId(tabId));
}

void TabModel::closeAll()
{
    if (m_tabs.isEmpty())
        return;
    beginResetModel();
    QVector<Tab*> old;
    old.swap(m_tabs);
    m_current = -1;
    endResetModel();
    for (Tab* t : old) {
        emit tabClosed(t->tabId());
        t->deleteLater();
    }
    emit countChanged();
    emit currentIndexChanged();
    emit currentTabChanged();
    emit sessionChanged();
}

void TabModel::closeAllPrivate()
{
    for (int i = m_tabs.size() - 1; i >= 0; --i)
        if (m_tabs.at(i)->isPrivate())
            closeTab(i);
}

void TabModel::moveTab(int from, int to)
{
    if (from < 0 || from >= m_tabs.size() || to < 0 || to >= m_tabs.size() || from == to)
        return;
    const int dest = to > from ? to + 1 : to;
    if (!beginMoveRows(QModelIndex(), from, from, QModelIndex(), dest))
        return;
    Tab* tab = m_tabs.takeAt(from);
    m_tabs.insert(to, tab);
    endMoveRows();
    if (m_current == from)
        m_current = to;
    else if (from < m_current && to >= m_current)
        --m_current;
    else if (from > m_current && to <= m_current)
        ++m_current;
    emit currentIndexChanged();
    emit sessionChanged();
}

int TabModel::liveWebViewCount() const
{
    int n = 0;
    for (Tab* t : m_tabs)
        if (t->hasWebView())
            ++n;
    return n;
}

bool TabModel::ensureWebView(Tab* tab)
{
    if (!tab || !m_engine || !m_engine->isInitialized())
        return false;
    if (tab->hasWebView())
        return true;
    trimLiveWebViews(tab);
    WebViewHandle* handle = m_engine->createWebView(tab, tab->isPrivate(), m_dpr, m_viewportSize);
    if (!handle)
        return false;
    tab->attachWebView(handle);
    return true;
}

void TabModel::trimLiveWebViews(Tab* keep)
{
    int live = liveWebViewCount();
    const int budget = qMax(1, m_maxLive) - (keep && !keep->hasWebView() ? 1 : 0);
    if (live <= budget)
        return;
    QVector<Tab*> candidates;
    for (Tab* t : m_tabs)
        if (t->hasWebView() && t != keep && t != currentTab())
            candidates.append(t);
    std::sort(candidates.begin(), candidates.end(),
              [](Tab* a, Tab* b) { return a->lastActive() < b->lastActive(); });
    for (Tab* t : candidates) {
        if (live <= budget)
            break;
        t->detachWebView();
        --live;
    }
}

void TabModel::onEngineInitialized()
{
    if (Tab* tab = currentTab()) {
        ensureWebView(tab);
        if (tab->handle()) {
            tab->handle()->setVisible(true);
            tab->handle()->setFocused(true);
        }
    }
}

void TabModel::onRenderContextLost()
{
    // Engine-side webviews are gone with the GL context (spec 5.2).  Tabs
    // keep their URL/title and are re-materialised on the next render.
    for (Tab* t : m_tabs)
        t->detachWebView();
}

void TabModel::emitRowChanged(Tab* tab, const QVector<int>& roles)
{
    const int row = m_tabs.indexOf(tab);
    if (row < 0)
        return;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx, roles);
}

Session TabModel::snapshot() const
{
    Session s;
    int current = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        Tab* t = m_tabs.at(i);
        if (t->isPrivate())
            continue;
        SessionTab st;
        st.url = t->url().isEmpty() ? t->requestedUrl() : t->url();
        if (st.url.isEmpty())
            continue;
        st.title = t->title();
        st.scroll = t->scrollOffset();
        st.zoom = t->pinchZoom();
        st.desktopMode = t->desktopMode();
        if (i == m_current)
            current = s.tabs.size();
        s.tabs.append(st);
    }
    s.currentIndex = current >= 0 ? current : (s.tabs.isEmpty() ? -1 : 0);
    return s;
}

void TabModel::restore(const Session& session)
{
    for (const SessionTab& st : session.tabs) {
        Tab* tab = newTab(st.url, false, false);
        tab->setRestoredState(st.title, st.scroll, st.zoom, st.desktopMode);
    }
    if (!session.tabs.isEmpty()) {
        const int idx = qBound(0, session.currentIndex, m_tabs.size() - 1);
        setCurrentIndex(idx);
    }
}

} // namespace Tuuli
