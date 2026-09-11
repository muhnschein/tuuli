// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "bookmarks/BookmarkModel.h"
#include "storage/Storage.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::BookmarkModel;
using Tuuli::Storage;

class tst_bookmarkmodel : public QObject
{
    Q_OBJECT

private slots:
    void addAndRoles();
    void removeVariants();
    void edit();
    void favicons();
    void activeUrl();
    void persistence();
};

namespace {

QVariant role(const BookmarkModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

} // namespace

void tst_bookmarkmodel::addAndRoles()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    BookmarkModel model(storage);
    QSignalSpy countSpy(&model, &BookmarkModel::countChanged);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);

    const int first = model.add(QStringLiteral("https://a.example/"), QStringLiteral("A"),
                                QStringLiteral("https://a.example/favicon.ico"));
    const int second = model.add(QStringLiteral("https://b.example/"), QString());
    QVERIFY(first > 0);
    QVERIFY(second > first);
    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 2);

    QCOMPARE(model.add(QStringLiteral("https://a.example/"), QStringLiteral("Dup")), first);
    QCOMPARE(model.add(QString(), QStringLiteral("Empty")), 0);
    QCOMPARE(model.count(), 2);

    QCOMPARE(role(model, 0, BookmarkModel::BookmarkIdRole).toInt(), first);
    QCOMPARE(role(model, 0, BookmarkModel::UrlRole).toString(),
             QStringLiteral("https://a.example/"));
    QCOMPARE(role(model, 0, BookmarkModel::TitleRole).toString(), QStringLiteral("A"));
    QCOMPARE(role(model, 0, BookmarkModel::FaviconRole).toString(),
             QStringLiteral("https://a.example/favicon.ico"));
    QCOMPARE(role(model, 1, BookmarkModel::TitleRole).toString(),
             QStringLiteral("https://b.example/"));
    QVERIFY(!role(model, 1, Qt::DisplayRole).isValid());
    QVERIFY(!role(model, 9, BookmarkModel::UrlRole).isValid());
    QCOMPARE(model.roleNames().value(BookmarkModel::BookmarkIdRole),
             QByteArrayLiteral("bookmarkId"));
    QVERIFY(model.contains(QStringLiteral("https://b.example/")));
    QVERIFY(!model.contains(QStringLiteral("https://c.example/")));
}

void tst_bookmarkmodel::removeVariants()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    BookmarkModel model(storage);
    model.add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    model.add(QStringLiteral("https://b.example/"), QStringLiteral("B"));
    model.add(QStringLiteral("https://c.example/"), QStringLiteral("C"));

    model.remove(1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(role(model, 1, BookmarkModel::TitleRole).toString(), QStringLiteral("C"));
    model.remove(-1);
    model.remove(2);
    QCOMPARE(model.count(), 2);

    QVERIFY(model.removeByUrl(QStringLiteral("https://a.example/")));
    QVERIFY(!model.removeByUrl(QStringLiteral("https://a.example/")));
    QCOMPARE(model.count(), 1);

    model.clear();
    QCOMPARE(model.count(), 0);
}

void tst_bookmarkmodel::edit()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    BookmarkModel model(storage);
    model.add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    QSignalSpy rowSpy(&model, &BookmarkModel::dataChanged);

    model.edit(0, QStringLiteral("https://a.example/"), QStringLiteral("A"));
    model.edit(0, QString(), QStringLiteral("A"));
    model.edit(3, QStringLiteral("https://x.example/"), QStringLiteral("X"));
    QCOMPARE(rowSpy.count(), 0);

    model.edit(0, QStringLiteral("https://a.example/"), QStringLiteral("Alpha"));
    QCOMPARE(rowSpy.count(), 1);
    QCOMPARE(rowSpy.last().at(2).value<QVector<int>>(), QVector<int>{BookmarkModel::TitleRole});

    model.edit(0, QStringLiteral("https://alpha.example/"), QStringLiteral("Alpha!"));
    QCOMPARE(rowSpy.count(), 2);
    QCOMPARE(rowSpy.last().at(2).value<QVector<int>>().count(), 2);
    QVERIFY(model.contains(QStringLiteral("https://alpha.example/")));
    QVERIFY(!model.contains(QStringLiteral("https://a.example/")));

    BookmarkModel reloaded(storage);
    QCOMPARE(role(reloaded, 0, BookmarkModel::TitleRole).toString(), QStringLiteral("Alpha!"));
}

void tst_bookmarkmodel::favicons()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    BookmarkModel model(storage);
    model.add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    QSignalSpy rowSpy(&model, &BookmarkModel::dataChanged);

    model.updateFavicon(QStringLiteral("https://a.example/"),
                        QStringLiteral("https://a.example/i.png"));
    QCOMPARE(rowSpy.count(), 1);
    QCOMPARE(role(model, 0, BookmarkModel::FaviconRole).toString(),
             QStringLiteral("https://a.example/i.png"));
    model.updateFavicon(QStringLiteral("https://a.example/"),
                        QStringLiteral("https://a.example/i.png"));
    model.updateFavicon(QStringLiteral("https://none.example/"), QStringLiteral("x"));
    QCOMPARE(rowSpy.count(), 1);

    BookmarkModel reloaded(storage);
    QCOMPARE(role(reloaded, 0, BookmarkModel::FaviconRole).toString(),
             QStringLiteral("https://a.example/i.png"));
}

void tst_bookmarkmodel::activeUrl()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    BookmarkModel model(storage);
    QSignalSpy urlSpy(&model, &BookmarkModel::activeUrlChanged);
    QSignalSpy bookmarkedSpy(&model, &BookmarkModel::activeUrlBookmarkedChanged);

    QVERIFY(!model.activeUrlBookmarked());
    model.setActiveUrl(QStringLiteral("https://a.example/"));
    model.setActiveUrl(QStringLiteral("https://a.example/"));
    QCOMPARE(urlSpy.count(), 1);
    QCOMPARE(model.activeUrl(), QStringLiteral("https://a.example/"));
    QVERIFY(!model.activeUrlBookmarked());

    model.add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    QVERIFY(model.activeUrlBookmarked());
    QVERIFY(bookmarkedSpy.count() >= 2);

    model.removeByUrl(QStringLiteral("https://a.example/"));
    QVERIFY(!model.activeUrlBookmarked());
}

void tst_bookmarkmodel::persistence()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    {
        BookmarkModel model(storage);
        model.add(QStringLiteral("https://z.example/"), QStringLiteral("Z"));
        model.add(QStringLiteral("https://y.example/"), QStringLiteral("Y"));
    }
    BookmarkModel model(storage);
    QCOMPARE(model.count(), 2);
    QCOMPARE(role(model, 0, BookmarkModel::TitleRole).toString(), QStringLiteral("Z"));
    QCOMPARE(role(model, 1, BookmarkModel::TitleRole).toString(), QStringLiteral("Y"));
}

QTEST_GUILESS_MAIN(tst_bookmarkmodel)
#include "tst_bookmarkmodel.moc"
