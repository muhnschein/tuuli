// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#pragma once

#include "bookmarks/BookmarkModel.h"
#include "engine/EngineMessages.h"
#include "history/HistoryModel.h"
#include "settings/Settings.h"
#include "storage/Storage.h"
#include "tabs/TabModel.h"
#include "tabs/TabPersistence.h"

#include <QObject>
#include <QString>

namespace Tuuli {

// Owns every model and the wiring between them. One per process; tests build one
// per test case on a temporary directory.
class Core : public QObject
{
    Q_OBJECT

public:
    Core(const QString &dataDirectory, const QString &configFilePath, QObject *parent = nullptr);

    Storage &storage();
    TabModel *tabs();
    HistoryModel *history();
    BookmarkModel *bookmarks();
    Settings *settings();
    EngineMessages *engineMessages();

private:
    Storage m_storage;
    TabPersistence m_tabPersistence;
    TabModel m_tabs;
    HistoryModel m_history;
    BookmarkModel m_bookmarks;
    Settings m_settings;
    EngineMessages m_engineMessages;
};

} // namespace Tuuli
