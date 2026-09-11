// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Modelled on the tab handling of sailfish-browser apps/storage/dbworker.cpp and
// apps/history/persistenttabmodel.cpp (Copyright (c) 2013 - 2021 Jolla Ltd., MPL-2.0).
// The engine keeps each view's navigation history, so only the current page of every
// tab is stored here; sailfish-browser's link/tab_history tables are not needed.
#pragma once

#include "Tab.h"

#include <QList>
#include <QString>

namespace Tuuli {

class Storage;

class TabPersistence
{
public:
    explicit TabPersistence(Storage &storage);

    QList<Tab> loadTabs() const;
    int loadActiveTabId() const;

    // Private tabs are never written; every call below ignores them.
    void insertTab(const Tab &tab);
    void updateTab(const Tab &tab);
    void removeTab(int tabId);
    void removeAllTabs();
    void setActiveTabId(int tabId);

private:
    Storage &m_storage;
};

} // namespace Tuuli
