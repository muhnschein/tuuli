// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "storage/Storage.h"
#include "tabs/TabPersistence.h"

#include <QTemporaryDir>
#include <QtTest>

using Tuuli::Storage;
using Tuuli::Tab;
using Tuuli::TabPersistence;

class tst_tabpersistence : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void ignoresPrivateAndInvalidTabs();
    void activeTabId();
    void removeAll();
};

namespace {

Tab makeTab(int id, const QString &url, bool isPrivate = false)
{
    Tab tab;
    tab.id = id;
    tab.url = url;
    tab.title = QStringLiteral("Title %1").arg(id);
    tab.isPrivate = isPrivate;
    return tab;
}

} // namespace

void tst_tabpersistence::roundTrip()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabPersistence persistence(storage);

    QVERIFY(persistence.loadTabs().isEmpty());
    persistence.insertTab(makeTab(7, QStringLiteral("https://a.example/")));
    persistence.insertTab(makeTab(3, QStringLiteral("https://b.example/")));

    QList<Tab> tabs = persistence.loadTabs();
    QCOMPARE(tabs.count(), 2);
    QCOMPARE(tabs.at(0).id, 7);
    QCOMPARE(tabs.at(1).id, 3);
    QCOMPARE(tabs.at(1).title, QStringLiteral("Title 3"));

    Tab updated = tabs.at(0);
    updated.url = QStringLiteral("https://a.example/page");
    updated.favicon = QStringLiteral("https://a.example/favicon.ico");
    persistence.updateTab(updated);
    tabs = persistence.loadTabs();
    QCOMPARE(tabs.at(0), updated);
    QVERIFY(tabs.at(0) != tabs.at(1));

    persistence.removeTab(7);
    tabs = persistence.loadTabs();
    QCOMPARE(tabs.count(), 1);
    QCOMPARE(tabs.first().id, 3);
}

void tst_tabpersistence::ignoresPrivateAndInvalidTabs()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabPersistence persistence(storage);

    persistence.insertTab(makeTab(1, QStringLiteral("https://secret.example/"), true));
    persistence.insertTab(makeTab(0, QStringLiteral("https://invalid.example/")));
    QVERIFY(persistence.loadTabs().isEmpty());

    persistence.insertTab(makeTab(2, QStringLiteral("https://public.example/")));
    Tab privateUpdate = makeTab(2, QStringLiteral("https://changed.example/"), true);
    persistence.updateTab(privateUpdate);
    QCOMPARE(persistence.loadTabs().first().url, QStringLiteral("https://public.example/"));
}

void tst_tabpersistence::activeTabId()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabPersistence persistence(storage);

    QCOMPARE(persistence.loadActiveTabId(), 0);
    persistence.setActiveTabId(5);
    QCOMPARE(persistence.loadActiveTabId(), 5);
    persistence.setActiveTabId(9);
    QCOMPARE(persistence.loadActiveTabId(), 9);
}

void tst_tabpersistence::removeAll()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabPersistence persistence(storage);

    persistence.insertTab(makeTab(1, QStringLiteral("https://a.example/")));
    persistence.insertTab(makeTab(2, QStringLiteral("https://b.example/")));
    persistence.removeAllTabs();
    QVERIFY(persistence.loadTabs().isEmpty());
}

QTEST_GUILESS_MAIN(tst_tabpersistence)
#include "tst_tabpersistence.moc"
