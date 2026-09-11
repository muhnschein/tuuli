// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "history/HistoryModel.h"
#include "storage/Storage.h"

#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::HistoryModel;
using Tuuli::Storage;

class tst_historymodel : public QObject
{
    Q_OBJECT

private slots:
    void visitsAndCounts();
    void ignoresUnrecordableUrls();
    void search();
    void titles();
    void removeAndClear();
    void prunesAndLimits();
};

namespace {

QVariant role(const HistoryModel &model, int row, int role)
{
    return model.data(model.index(row, 0), role);
}

int rowsInDatabase(const Storage &storage)
{
    QSqlQuery query(storage.database());
    query.exec(QStringLiteral("SELECT COUNT(*) FROM browser_history"));
    return query.next() ? query.value(0).toInt() : -1;
}

} // namespace

void tst_historymodel::visitsAndCounts()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    HistoryModel model(storage);
    QSignalSpy countSpy(&model, &HistoryModel::countChanged);
    QCOMPARE(model.count(), 0);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);

    model.visit(QStringLiteral("https://a.example/"), QStringLiteral("A"));
    model.visit(QStringLiteral("https://b.example/"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(countSpy.count(), 2);
    QCOMPARE(role(model, 0, HistoryModel::UrlRole).toString(),
             QStringLiteral("https://b.example/"));
    QCOMPARE(role(model, 0, HistoryModel::TitleRole).toString(),
             QStringLiteral("https://b.example/"));
    QCOMPARE(role(model, 1, HistoryModel::TitleRole).toString(), QStringLiteral("A"));
    QCOMPARE(role(model, 1, HistoryModel::VisitCountRole).toInt(), 1);
    QVERIFY(role(model, 1, HistoryModel::DateRole).toDateTime().isValid());
    QVERIFY(!role(model, 1, Qt::DisplayRole).isValid());
    QVERIFY(!role(model, 5, HistoryModel::UrlRole).isValid());
    QCOMPARE(model.roleNames().value(HistoryModel::VisitCountRole),
             QByteArrayLiteral("visitCount"));

    model.visit(QStringLiteral("https://a.example/"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(role(model, 0, HistoryModel::UrlRole).toString(),
             QStringLiteral("https://a.example/"));
    QCOMPARE(role(model, 0, HistoryModel::VisitCountRole).toInt(), 2);
    QCOMPARE(role(model, 0, HistoryModel::TitleRole).toString(), QStringLiteral("A"));

    model.visit(QStringLiteral("https://a.example/"), QStringLiteral("A again"));
    QCOMPARE(role(model, 0, HistoryModel::TitleRole).toString(), QStringLiteral("A again"));
    QCOMPARE(role(model, 0, HistoryModel::VisitCountRole).toInt(), 3);
}

void tst_historymodel::ignoresUnrecordableUrls()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    HistoryModel model(storage);
    model.visit(QString());
    model.visit(QStringLiteral("about:blank"));
    model.updateTitle(QStringLiteral("about:blank"), QStringLiteral("Blank"));
    QCOMPARE(model.count(), 0);
}

void tst_historymodel::search()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    HistoryModel model(storage);
    model.visit(QStringLiteral("https://sailfishos.org/"), QStringLiteral("Sailfish OS"));
    model.visit(QStringLiteral("https://example.org/"), QStringLiteral("Example"));
    model.visit(QStringLiteral("https://duckduckgo.com/?q=sailfish"), QStringLiteral("Search"));

    QSignalSpy termSpy(&model, &HistoryModel::searchTermChanged);
    model.setSearchTerm(QStringLiteral("sailfish"));
    QCOMPARE(termSpy.count(), 1);
    QCOMPARE(model.searchTerm(), QStringLiteral("sailfish"));
    QCOMPARE(model.count(), 2);
    model.setSearchTerm(QStringLiteral("sailfish"));
    QCOMPARE(termSpy.count(), 1);

    model.setSearchTerm(QStringLiteral("EXAMPLE"));
    QCOMPARE(model.count(), 1);
    QCOMPARE(role(model, 0, HistoryModel::TitleRole).toString(), QStringLiteral("Example"));

    model.visit(QStringLiteral("https://another.example/"));
    QCOMPARE(model.count(), 2);

    model.setSearchTerm(QString());
    QCOMPARE(model.count(), 4);
}

void tst_historymodel::titles()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    HistoryModel model(storage);
    model.visit(QStringLiteral("https://a.example/"));
    model.visit(QStringLiteral("https://b.example/"));
    QSignalSpy rowSpy(&model, &HistoryModel::dataChanged);

    model.updateTitle(QStringLiteral("https://a.example/"), QStringLiteral("Alpha"));
    QCOMPARE(rowSpy.count(), 1);
    QCOMPARE(role(model, 1, HistoryModel::TitleRole).toString(), QStringLiteral("Alpha"));
    model.updateTitle(QStringLiteral("https://a.example/"), QStringLiteral("Alpha"));
    model.updateTitle(QStringLiteral("https://a.example/"), QString());
    model.updateTitle(QStringLiteral("https://missing.example/"), QStringLiteral("Nope"));
    QCOMPARE(rowSpy.count(), 1);

    HistoryModel reloaded(storage);
    QCOMPARE(role(reloaded, 1, HistoryModel::TitleRole).toString(), QStringLiteral("Alpha"));
}

void tst_historymodel::removeAndClear()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    HistoryModel model(storage);
    model.visit(QStringLiteral("https://a.example/"));
    model.visit(QStringLiteral("https://b.example/"));
    model.visit(QStringLiteral("https://c.example/"));
    QSignalSpy countSpy(&model, &HistoryModel::countChanged);

    model.remove(1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(role(model, 1, HistoryModel::UrlRole).toString(),
             QStringLiteral("https://a.example/"));
    QCOMPARE(rowsInDatabase(storage), 2);

    model.remove(-1);
    model.remove(2);
    QCOMPARE(model.count(), 2);

    model.clear();
    QCOMPARE(model.count(), 0);
    QCOMPARE(rowsInDatabase(storage), 0);
}

void tst_historymodel::prunesAndLimits()
{
    QTemporaryDir dir;
    Storage storage(dir.path());
    {
        QSqlQuery query(storage.database());
        QVERIFY(query.exec(QStringLiteral("BEGIN")));
        for (int i = 0; i < HistoryModel::MaxEntries + 25; ++i) {
            QSqlQuery insert(storage.database());
            insert.prepare(
                QStringLiteral("INSERT INTO browser_history (url, title, date) VALUES (?, '', ?)"));
            insert.addBindValue(QStringLiteral("https://site%1.example/").arg(i));
            insert.addBindValue(i);
            QVERIFY(insert.exec());
        }
        QVERIFY(query.exec(QStringLiteral("COMMIT")));
    }
    HistoryModel model(storage);
    QCOMPARE(rowsInDatabase(storage), HistoryModel::MaxEntries);
    QCOMPARE(model.count(), HistoryModel::DisplayLimit);
    // Newest first, oldest pruned.
    QCOMPARE(role(model, 0, HistoryModel::UrlRole).toString(),
             QStringLiteral("https://site%1.example/").arg(HistoryModel::MaxEntries + 24));
}

QTEST_GUILESS_MAIN(tst_historymodel)
#include "tst_historymodel.moc"
