// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "storage/Storage.h"
#include "tabs/TabModel.h"
#include "tabs/TabPersistence.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::Storage;
using Tuuli::TabModel;
using Tuuli::TabPersistence;

class tst_tabmodel : public QObject
{
    Q_OBJECT

private slots:
    void emptyModel();
    void newTabAppendsAndActivates();
    void newTabRejectsExternalUrls();
    void activation();
    void closeTabActivatesPrevious();
    void closeAllTabs();
    void urlUpdatesAndVisits();
    void titleAndFavicon();
    void privateTabsStayQuiet();
    void persistenceRoundTrip();
};

namespace {

QVariant role(const TabModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

} // namespace

void tst_tabmodel::emptyModel()
{
    TabModel model(nullptr);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(model.activeTabIndex(), -1);
    QCOMPARE(model.activeTabId(), 0);
    QVERIFY(!model.activeIsPrivate());
    QVERIFY(model.activeUrl().isEmpty());
    QVERIFY(model.activeTitle().isEmpty());
    QVERIFY(model.activeFavicon().isEmpty());
    QVERIFY(!model.data(model.index(0, 0), TabModel::UrlRole).isValid());
    QCOMPARE(model.roleNames().value(TabModel::PrivateRole), QByteArrayLiteral("privateTab"));
    QCOMPARE(model.roleNames().value(TabModel::ActiveRole), QByteArrayLiteral("activeTab"));
    model.activateTab(0);
    model.closeActiveTab();
    model.closeAllTabs();
    QCOMPARE(model.count(), 0);
}

void tst_tabmodel::newTabAppendsAndActivates()
{
    TabModel model(nullptr);
    QSignalSpy countSpy(&model, &TabModel::countChanged);
    QSignalSpy addedSpy(&model, &TabModel::tabAdded);
    QSignalSpy activeSpy(&model, &TabModel::activeTabChanged);
    QSignalSpy dataSpy(&model, &TabModel::activeTabDataChanged);

    const int first = model.newTab(QStringLiteral("https://one.example/"));
    const int second = model.newTab(QStringLiteral("https://two.example/"), true);
    QVERIFY(first > 0);
    QCOMPARE(second, first + 1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 2);
    QCOMPARE(addedSpy.count(), 2);
    QCOMPARE(addedSpy.last().first().toInt(), second);
    QCOMPARE(activeSpy.count(), 2);
    QCOMPARE(dataSpy.count(), 2);

    QCOMPARE(model.activeTabIndex(), 1);
    QCOMPARE(model.activeTabId(), second);
    QVERIFY(model.activeIsPrivate());
    QCOMPARE(model.activeUrl(), QStringLiteral("https://two.example/"));
    QCOMPARE(role(model, 0, TabModel::TabIdRole).toInt(), first);
    QCOMPARE(role(model, 0, TabModel::UrlRole).toString(), QStringLiteral("https://one.example/"));
    QCOMPARE(role(model, 0, TabModel::ActiveRole).toBool(), false);
    QCOMPARE(role(model, 1, TabModel::ActiveRole).toBool(), true);
    QCOMPARE(role(model, 1, TabModel::PrivateRole).toBool(), true);
    QVERIFY(role(model, 1, TabModel::TitleRole).toString().isEmpty());
    QVERIFY(role(model, 1, TabModel::FaviconRole).toString().isEmpty());
    QVERIFY(!role(model, 1, Qt::DisplayRole).isValid());
    QCOMPARE(model.tabs().count(), 2);
}

void tst_tabmodel::newTabRejectsExternalUrls()
{
    TabModel model(nullptr);
    QCOMPARE(model.newTab(QStringLiteral("tel:+358401234567")), 0);
    QCOMPARE(model.newTab(QStringLiteral("sms:123")), 0);
    QCOMPARE(model.newTab(QStringLiteral("mailto:a@b.c")), 0);
    QCOMPARE(model.newTab(QStringLiteral("geo:60.17,24.94")), 0);
    QCOMPARE(model.count(), 0);
    QVERIFY(TabModel::isExternalUrl(QStringLiteral("MAILTO:x@y.z")));
    QVERIFY(!TabModel::isExternalUrl(QStringLiteral("https://x.y/")));
}

void tst_tabmodel::activation()
{
    TabModel model(nullptr);
    const int a = model.newTab(QStringLiteral("https://a.example/"));
    const int b = model.newTab(QStringLiteral("https://b.example/"));
    QSignalSpy activeSpy(&model, &TabModel::activeTabChanged);
    QSignalSpy rowSpy(&model, &TabModel::dataChanged);

    model.activateTab(0);
    QCOMPARE(model.activeTabId(), a);
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(rowSpy.count(), 2);

    model.activateTab(0);
    QCOMPARE(activeSpy.count(), 1);

    model.activateTab(99);
    QCOMPARE(model.activeTabId(), b);
    model.activateTab(-5);
    QCOMPARE(model.activeTabId(), a);

    QVERIFY(model.activateTabById(b));
    QCOMPARE(model.activeTabIndex(), 1);
    QVERIFY(!model.activateTabById(1234));
    QCOMPARE(model.activeTabIndex(), 1);
    QCOMPARE(model.indexOf(a), 0);
    QCOMPARE(model.indexOf(1234), -1);
}

void tst_tabmodel::closeTabActivatesPrevious()
{
    TabModel model(nullptr);
    const int a = model.newTab(QStringLiteral("https://a.example/"));
    const int b = model.newTab(QStringLiteral("https://b.example/"));
    const int c = model.newTab(QStringLiteral("https://c.example/"));
    QSignalSpy closedSpy(&model, &TabModel::tabClosed);

    model.closeTab(2);
    QCOMPARE(closedSpy.count(), 1);
    QCOMPARE(closedSpy.last().first().toInt(), c);
    QCOMPARE(model.activeTabId(), b);

    model.closeTab(0);
    QCOMPARE(model.activeTabId(), b);
    QCOMPARE(model.count(), 1);
    QCOMPARE(model.indexOf(a), -1);

    model.closeTab(7);
    model.closeTab(-1);
    QCOMPARE(model.count(), 1);

    QSignalSpy activeSpy(&model, &TabModel::activeTabChanged);
    model.closeActiveTab();
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.activeTabId(), 0);
    QCOMPARE(activeSpy.count(), 1);

    const int d = model.newTab(QStringLiteral("https://d.example/"));
    model.newTab(QStringLiteral("https://e.example/"));
    model.activateTabById(d);
    model.closeTab(0);
    QCOMPARE(model.activeTabIndex(), 0);
}

void tst_tabmodel::closeAllTabs()
{
    TabModel model(nullptr);
    model.newTab(QStringLiteral("https://a.example/"));
    model.newTab(QStringLiteral("https://b.example/"));
    QSignalSpy closedSpy(&model, &TabModel::tabClosed);
    QSignalSpy countSpy(&model, &TabModel::countChanged);

    model.closeAllTabs();
    QCOMPARE(model.count(), 0);
    QCOMPARE(closedSpy.count(), 2);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(model.activeTabId(), 0);
}

void tst_tabmodel::urlUpdatesAndVisits()
{
    TabModel model(nullptr);
    QSignalSpy visitedSpy(&model, &TabModel::visited);
    QSignalSpy dataSpy(&model, &TabModel::activeTabDataChanged);
    const int a = model.newTab(QStringLiteral("https://a.example/"));
    const int b = model.newTab(QStringLiteral("https://b.example/"));
    QCOMPARE(visitedSpy.count(), 0);
    dataSpy.clear();

    // The engine reporting the requested url is the first visit, reported once.
    model.updateUrl(a, QStringLiteral("https://a.example/"));
    QCOMPARE(visitedSpy.count(), 1);
    model.updateUrl(a, QStringLiteral("https://a.example/"));
    QCOMPARE(visitedSpy.count(), 1);
    QCOMPARE(dataSpy.count(), 0);

    // Navigation within a tab.
    model.updateUrl(a, QStringLiteral("https://a.example/next"));
    QCOMPARE(visitedSpy.count(), 2);
    QCOMPARE(visitedSpy.last().first().toString(), QStringLiteral("https://a.example/next"));
    QCOMPARE(role(model, 0, TabModel::UrlRole).toString(),
             QStringLiteral("https://a.example/next"));
    QCOMPARE(dataSpy.count(), 0);

    // Active tab reports through activeTabDataChanged too.
    model.updateUrl(b, QStringLiteral("https://b.example/"));
    QCOMPARE(dataSpy.count(), 1);
    QCOMPARE(model.activeUrl(), QStringLiteral("https://b.example/"));

    // Ignored inputs.
    model.updateUrl(b, QString());
    model.updateUrl(b, QStringLiteral("tel:112"));
    model.updateUrl(999, QStringLiteral("https://nowhere.example/"));
    QCOMPARE(visitedSpy.count(), 3);
    QCOMPARE(model.activeUrl(), QStringLiteral("https://b.example/"));
}

void tst_tabmodel::titleAndFavicon()
{
    TabModel model(nullptr);
    const int a = model.newTab(QStringLiteral("https://a.example/"));
    const int b = model.newTab(QStringLiteral("https://b.example/"));
    QSignalSpy titleSpy(&model, &TabModel::titleUpdated);
    QSignalSpy faviconSpy(&model, &TabModel::faviconUpdated);
    QSignalSpy dataSpy(&model, &TabModel::activeTabDataChanged);
    QSignalSpy rowSpy(&model, &TabModel::dataChanged);

    model.updateTitle(a, QStringLiteral("A"));
    model.updateTitle(a, QStringLiteral("A"));
    model.updateTitle(999, QStringLiteral("X"));
    QCOMPARE(titleSpy.count(), 1);
    QCOMPARE(titleSpy.last().at(0).toString(), QStringLiteral("https://a.example/"));
    QCOMPARE(titleSpy.last().at(1).toString(), QStringLiteral("A"));
    QCOMPARE(role(model, 0, TabModel::TitleRole).toString(), QStringLiteral("A"));
    QCOMPARE(dataSpy.count(), 0);
    QCOMPARE(rowSpy.count(), 1);
    QCOMPARE(rowSpy.last().at(2).value<QVector<int>>(), QVector<int>{TabModel::TitleRole});

    model.updateTitle(b, QStringLiteral("B"));
    QCOMPARE(model.activeTitle(), QStringLiteral("B"));
    QCOMPARE(dataSpy.count(), 1);

    model.updateFavicon(a, QStringLiteral("https://a.example/favicon.ico"));
    model.updateFavicon(a, QStringLiteral("https://a.example/favicon.ico"));
    model.updateFavicon(999, QStringLiteral("x"));
    QCOMPARE(faviconSpy.count(), 1);
    QCOMPARE(role(model, 0, TabModel::FaviconRole).toString(),
             QStringLiteral("https://a.example/favicon.ico"));
    model.updateFavicon(b, QStringLiteral("https://b.example/icon.png"));
    QCOMPARE(model.activeFavicon(), QStringLiteral("https://b.example/icon.png"));
    QCOMPARE(dataSpy.count(), 2);
}

void tst_tabmodel::privateTabsStayQuiet()
{
    TabModel model(nullptr);
    QSignalSpy visitedSpy(&model, &TabModel::visited);
    QSignalSpy titleSpy(&model, &TabModel::titleUpdated);
    QSignalSpy faviconSpy(&model, &TabModel::faviconUpdated);
    const int p = model.newTab(QStringLiteral("https://secret.example/"), true);

    model.updateUrl(p, QStringLiteral("https://secret.example/"));
    model.updateUrl(p, QStringLiteral("https://secret.example/more"));
    model.updateTitle(p, QStringLiteral("Secret"));
    model.updateFavicon(p, QStringLiteral("https://secret.example/favicon.ico"));

    QCOMPARE(visitedSpy.count(), 0);
    QCOMPARE(titleSpy.count(), 0);
    QCOMPARE(faviconSpy.count(), 0);
    QCOMPARE(model.activeUrl(), QStringLiteral("https://secret.example/more"));
    QCOMPARE(model.activeTitle(), QStringLiteral("Secret"));
}

void tst_tabmodel::persistenceRoundTrip()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    TabPersistence persistence(storage);
    int publicId = 0;
    int otherId = 0;
    {
        TabModel model(&persistence);
        publicId = model.newTab(QStringLiteral("https://public.example/"));
        model.newTab(QStringLiteral("https://secret.example/"), true);
        otherId = model.newTab(QStringLiteral("https://other.example/"));
        model.updateTitle(publicId, QStringLiteral("Public"));
        model.updateFavicon(publicId, QStringLiteral("https://public.example/favicon.ico"));
        model.updateUrl(otherId, QStringLiteral("https://other.example/moved"));
        model.activateTabById(publicId);
    }
    {
        TabModel model(&persistence);
        QCOMPARE(model.count(), 2);
        QCOMPARE(model.activeTabId(), publicId);
        QCOMPARE(model.activeTitle(), QStringLiteral("Public"));
        QCOMPARE(model.activeFavicon(), QStringLiteral("https://public.example/favicon.ico"));
        QCOMPARE(role(model, 1, TabModel::UrlRole).toString(),
                 QStringLiteral("https://other.example/moved"));
        QVERIFY(!role(model, 1, TabModel::PrivateRole).toBool());

        // Restored tabs are not "visited" again when the engine reports their url.
        QSignalSpy visitedSpy(&model, &TabModel::visited);
        model.updateUrl(publicId, QStringLiteral("https://public.example/"));
        QCOMPARE(visitedSpy.count(), 0);

        const int fresh = model.newTab(QStringLiteral("https://fresh.example/"));
        QVERIFY(fresh > otherId);
        model.closeTab(model.indexOf(publicId));
        model.closeAllTabs();
    }
    {
        TabModel model(&persistence);
        QCOMPARE(model.count(), 0);
        QCOMPARE(model.activeTabId(), 0);
    }
    {
        // A stale active id falls back to the first tab.
        TabModel model(&persistence);
        model.newTab(QStringLiteral("https://x.example/"));
        model.newTab(QStringLiteral("https://y.example/"));
        persistence.setActiveTabId(4242);
    }
    {
        TabModel model(&persistence);
        QCOMPARE(model.activeTabIndex(), 0);
    }
}

QTEST_GUILESS_MAIN(tst_tabmodel)
#include "tst_tabmodel.moc"
