/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "model/historymodel.h"

#include <QtTest>

using namespace Tuuli;

class tst_HistoryModel : public QObject
{
    Q_OBJECT
private slots:
    void addsAndCountsVisits()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        QVERIFY(h.isOpen());
        QVERIFY(h.addVisit(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A")));
        QVERIFY(h.addVisit(QUrl(QStringLiteral("https://a.example/")), QString()));
        QVERIFY(h.addVisit(QUrl(QStringLiteral("https://b.example/")), QStringLiteral("B")));
        QCOMPARE(h.totalCount(), 2);
        QCOMPARE(h.count(), 2);
        const QVector<HistoryEntry> all = h.search(QString(), 10);
        QCOMPARE(all.size(), 2);
        int visitsA = 0;
        for (const HistoryEntry& e : all)
            if (e.url.host() == QLatin1String("a.example")) visitsA = e.visits;
        QCOMPARE(visitsA, 2);
    }

    void privateVisitsAreNeverRecorded()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        QVERIFY(!h.addVisit(QUrl(QStringLiteral("https://secret.example/")), QStringLiteral("S"), true));
        QVERIFY(!h.updateTitle(QUrl(QStringLiteral("https://secret.example/")), QStringLiteral("S"), true));
        QCOMPARE(h.totalCount(), 0);
    }

    void onlyHttpSchemesAreRecorded()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        QVERIFY(!h.addVisit(QUrl(QStringLiteral("about:blank")), QString()));
        QVERIFY(!h.addVisit(QUrl(QStringLiteral("file:///etc/passwd")), QString()));
        QVERIFY(h.addVisit(QUrl(QStringLiteral("http://a.example/")), QString()));
        QCOMPARE(h.totalCount(), 1);
    }

    void searchAndFilter()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        h.addVisit(QUrl(QStringLiteral("https://news.example/story")), QStringLiteral("Big news"));
        h.addVisit(QUrl(QStringLiteral("https://docs.example/")), QStringLiteral("Docs"));
        QCOMPARE(h.search(QStringLiteral("news"), 10).size(), 1);
        QCOMPARE(h.search(QStringLiteral("Docs"), 10).size(), 1);
        h.setFilter(QStringLiteral("example"));
        QCOMPARE(h.count(), 2);
        h.setLimit(1);
        QCOMPARE(h.count(), 1);
    }

    void updateTitleRemoveClear()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        h.addVisit(QUrl(QStringLiteral("https://a.example/")), QString());
        QVERIFY(h.updateTitle(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("Title")));
        QCOMPARE(h.search(QString(), 1).first().title, QStringLiteral("Title"));
        QVERIFY(!h.updateTitle(QUrl(QStringLiteral("https://missing.example/")), QStringLiteral("x")));
        QVERIFY(h.remove(QUrl(QStringLiteral("https://a.example/"))));
        QCOMPARE(h.totalCount(), 0);
        h.addVisit(QUrl(QStringLiteral("https://b.example/")), QString());
        QVERIFY(h.clear());
        QCOMPARE(h.totalCount(), 0);
    }

    void rolesForQml()
    {
        HistoryModel h(QStringLiteral(":memory:"));
        h.addVisit(QUrl(QStringLiteral("https://a.example/")), QString());
        const QModelIndex idx = h.index(0);
        QCOMPARE(h.data(idx, HistoryModel::UrlRole).toUrl(), QUrl(QStringLiteral("https://a.example/")));
        QCOMPARE(h.data(idx, HistoryModel::TitleRole).toString(), QStringLiteral("a.example"));
        QCOMPARE(h.data(idx, HistoryModel::VisitsRole).toInt(), 1);
        QVERIFY(h.roleNames().contains(HistoryModel::LastVisitRole));
    }
};

QTEST_GUILESS_MAIN(tst_HistoryModel)
#include "tst_historymodel.moc"
