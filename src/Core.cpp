// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "Core.h"

namespace Tuuli {

Core::Core(const QString &dataDirectory, const QString &configFilePath, QObject *parent)
    : QObject(parent)
    , m_storage(dataDirectory)
    , m_tabPersistence(m_storage)
    , m_tabs(&m_tabPersistence)
    , m_history(m_storage)
    , m_bookmarks(m_storage)
    , m_settings(configFilePath)
{
    // Private tabs never emit these, which is what keeps them out of history.
    connect(&m_tabs, &TabModel::visited, &m_history,
            [this](const QString &url) { m_history.visit(url); });
    connect(&m_tabs, &TabModel::titleUpdated, &m_history, &HistoryModel::updateTitle);
    connect(&m_tabs, &TabModel::faviconUpdated, &m_bookmarks, &BookmarkModel::updateFavicon);
    connect(&m_tabs, &TabModel::activeTabDataChanged, &m_bookmarks,
            [this]() { m_bookmarks.setActiveUrl(m_tabs.activeUrl()); });
    m_bookmarks.setActiveUrl(m_tabs.activeUrl());
}

Storage &Core::storage()
{
    return m_storage;
}

TabModel *Core::tabs()
{
    return &m_tabs;
}

HistoryModel *Core::history()
{
    return &m_history;
}

BookmarkModel *Core::bookmarks()
{
    return &m_bookmarks;
}

Settings *Core::settings()
{
    return &m_settings;
}

EngineMessages *Core::engineMessages()
{
    return &m_engineMessages;
}

} // namespace Tuuli
