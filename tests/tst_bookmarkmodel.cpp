/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "model/bookmarkmodel.h"

#include <QtTest>

using namespace Tuuli;

class tst_BookmarkModel : public QObject
{
    Q_OBJECT
private slots:
    void addRemoveContains()
    {
        BookmarkModel b(QStringLiteral(":memory:"));
        QVERIFY(b.isOpen());
        QSignalSpy changed(&b, &BookmarkModel::changed);
        QVERIFY(b.add(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A")));
        QVERIFY(!b.add(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("dup")));
        QVERIFY(!b.add(QUrl(), QStringLiteral("empty")));
        QVERIFY(b.contains(QUrl(QStringLiteral("https://a.example/"))));
        QCOMPARE(b.count(), 1);
        QCOMPARE(changed.size(), 1);
        QVERIFY(b.rename(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("Renamed")));
        QCOMPARE(b.data(b.index(0), BookmarkModel::TitleRole).toString(), QStringLiteral("Renamed"));
        QVERIFY(b.remove(QUrl(QStringLiteral("https://a.example/"))));
        QVERIFY(!b.remove(QUrl(QStringLiteral("https://a.example/"))));
        QCOMPARE(b.count(), 0);
    }

    void orderAndMove()
    {
        BookmarkModel b(QStringLiteral(":memory:"));
        b.add(QUrl(QStringLiteral("https://1.example/")), QStringLiteral("1"));
        b.add(QUrl(QStringLiteral("https://2.example/")), QStringLiteral("2"));
        b.add(QUrl(QStringLiteral("https://3.example/")), QStringLiteral("3"));
        QCOMPARE(b.data(b.index(0), BookmarkModel::TitleRole).toString(), QStringLiteral("1"));
        QVERIFY(b.move(0, 2));
        QCOMPARE(b.data(b.index(0), BookmarkModel::TitleRole).toString(), QStringLiteral("2"));
        QCOMPARE(b.data(b.index(2), BookmarkModel::TitleRole).toString(), QStringLiteral("1"));
        QVERIFY(!b.move(0, 9));
    }

    void persistsToDisk()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/bm.sqlite");
        {
            BookmarkModel b(path);
            b.add(QUrl(QStringLiteral("https://a.example/")), QStringLiteral("A"));
        }
        BookmarkModel again(path);
        QCOMPARE(again.count(), 1);
        QVERIFY(again.contains(QUrl(QStringLiteral("https://a.example/"))));
    }
};

QTEST_GUILESS_MAIN(tst_BookmarkModel)
#include "tst_bookmarkmodel.moc"
