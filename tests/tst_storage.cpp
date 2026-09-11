// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "storage/Storage.h"

#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::Storage;

class tst_storage : public QObject
{
    Q_OBJECT

private slots:
    void createsSchema();
    void reopenKeepsData();
    void refusesUnusableDirectory();
    void refusesNewerSchema();
    void defaultPaths();
};

namespace {

QStringList tableNames(const Storage &storage)
{
    QStringList names;
    QSqlQuery query(storage.database());
    query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' ORDER BY name"));
    while (query.next()) {
        names.append(query.value(0).toString());
    }
    return names;
}

} // namespace

void tst_storage::createsSchema()
{
    QTemporaryDir dir;
    Storage storage(dir.path() + QStringLiteral("/nested/data"));
    QVERIFY(storage.isOpen());
    QCOMPARE(storage.userVersion(), Storage::SchemaVersion);
    QVERIFY(storage.databasePath().endsWith(QStringLiteral("tuuli.sqlite")));

    const QStringList tables = tableNames(storage);
    QVERIFY(tables.contains(QStringLiteral("tab")));
    QVERIFY(tables.contains(QStringLiteral("browser_history")));
    QVERIFY(tables.contains(QStringLiteral("bookmark")));
    QVERIFY(tables.contains(QStringLiteral("setting")));
}

void tst_storage::reopenKeepsData()
{
    QTemporaryDir dir;
    {
        Storage storage(dir.path());
        QSqlQuery query(storage.database());
        QVERIFY(query.exec(
            QStringLiteral("INSERT INTO setting (name, value) VALUES ('probe', 'kept')")));
    }
    Storage storage(dir.path());
    QVERIFY(storage.isOpen());
    QCOMPARE(storage.userVersion(), Storage::SchemaVersion);
    QSqlQuery query(storage.database());
    QVERIFY(query.exec(QStringLiteral("SELECT value FROM setting WHERE name = 'probe'")));
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toString(), QStringLiteral("kept"));
}

void tst_storage::refusesUnusableDirectory()
{
    const QString none;
    Storage empty(none);
    QVERIFY(!empty.isOpen());
    QVERIFY(!empty.database().isValid());

    QTemporaryDir dir;
    QFile blocker(dir.path() + QStringLiteral("/file"));
    QVERIFY(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    Storage underFile(dir.path() + QStringLiteral("/file/data"));
    QVERIFY(!underFile.isOpen());
}

void tst_storage::refusesNewerSchema()
{
    QTemporaryDir dir;
    {
        Storage storage(dir.path());
        QSqlQuery query(storage.database());
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 99")));
    }
    Storage storage(dir.path());
    QVERIFY(!storage.isOpen());
}

void tst_storage::defaultPaths()
{
    QVERIFY(!Storage::defaultDataDirectory().isEmpty());
    QVERIFY(Storage::defaultConfigFilePath().endsWith(QStringLiteral(".conf")));
}

QTEST_GUILESS_MAIN(tst_storage)
#include "tst_storage.moc"
