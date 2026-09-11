// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "TabPersistence.h"

#include "storage/Storage.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

namespace Tuuli {

namespace {

const char *const ActiveTabSetting = "activeTabId";

bool run(QSqlQuery &query)
{
    if (!query.exec()) {
        qWarning() << "TabPersistence:" << query.lastError().text() << query.lastQuery();
        return false;
    }
    return true;
}

} // namespace

TabPersistence::TabPersistence(Storage &storage)
    : m_storage(storage)
{
}

QList<Tab> TabPersistence::loadTabs() const
{
    QList<Tab> tabs;
    QSqlQuery query(m_storage.database());
    query.prepare(
        QStringLiteral("SELECT tab_id, url, title, favicon FROM tab ORDER BY position ASC"));
    if (!run(query)) {
        return tabs;
    }
    while (query.next()) {
        Tab tab;
        tab.id = query.value(0).toInt();
        tab.url = query.value(1).toString();
        tab.title = query.value(2).toString();
        tab.favicon = query.value(3).toString();
        tabs.append(tab);
    }
    return tabs;
}

int TabPersistence::loadActiveTabId() const
{
    QSqlQuery query(m_storage.database());
    query.prepare(QStringLiteral("SELECT value FROM setting WHERE name = ?"));
    query.addBindValue(QLatin1String(ActiveTabSetting));
    if (run(query) && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

void TabPersistence::insertTab(const Tab &tab)
{
    if (tab.isPrivate || !tab.isValid()) {
        return;
    }
    QSqlQuery query(m_storage.database());
    query.prepare(QStringLiteral("INSERT INTO tab (tab_id, position, url, title, favicon) "
                                 "VALUES (?, (SELECT COALESCE(MAX(position), 0) + 1 FROM tab), "
                                 "?, ?, ?)"));
    query.addBindValue(tab.id);
    query.addBindValue(Storage::text(tab.url));
    query.addBindValue(Storage::text(tab.title));
    query.addBindValue(Storage::text(tab.favicon));
    run(query);
}

void TabPersistence::updateTab(const Tab &tab)
{
    if (tab.isPrivate || !tab.isValid()) {
        return;
    }
    QSqlQuery query(m_storage.database());
    query.prepare(
        QStringLiteral("UPDATE tab SET url = ?, title = ?, favicon = ? WHERE tab_id = ?"));
    query.addBindValue(Storage::text(tab.url));
    query.addBindValue(Storage::text(tab.title));
    query.addBindValue(Storage::text(tab.favicon));
    query.addBindValue(tab.id);
    run(query);
}

void TabPersistence::removeTab(int tabId)
{
    QSqlQuery query(m_storage.database());
    query.prepare(QStringLiteral("DELETE FROM tab WHERE tab_id = ?"));
    query.addBindValue(tabId);
    run(query);
}

void TabPersistence::removeAllTabs()
{
    QSqlQuery query(m_storage.database());
    query.prepare(QStringLiteral("DELETE FROM tab"));
    run(query);
}

void TabPersistence::setActiveTabId(int tabId)
{
    QSqlQuery query(m_storage.database());
    query.prepare(QStringLiteral("INSERT OR REPLACE INTO setting (name, value) VALUES (?, ?)"));
    query.addBindValue(QLatin1String(ActiveTabSetting));
    query.addBindValue(QString::number(tabId));
    run(query);
}

} // namespace Tuuli
