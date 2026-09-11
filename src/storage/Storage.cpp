// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "Storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QtDebug>

namespace Tuuli {

namespace {

const char *const DatabaseFileName = "tuuli.sqlite";

const QStringList &schemaStatements()
{
    static const QStringList statements{
        QStringLiteral("CREATE TABLE IF NOT EXISTS tab ("
                       "tab_id INTEGER PRIMARY KEY, "
                       "position INTEGER NOT NULL, "
                       "url TEXT NOT NULL, "
                       "title TEXT NOT NULL DEFAULT '', "
                       "favicon TEXT NOT NULL DEFAULT '')"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS browser_history ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "url TEXT NOT NULL UNIQUE, "
                       "title TEXT NOT NULL DEFAULT '', "
                       "visited_count INTEGER NOT NULL DEFAULT 1, "
                       "date INTEGER NOT NULL)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS browser_history_date ON browser_history(date)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS bookmark ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "url TEXT NOT NULL, "
                       "title TEXT NOT NULL DEFAULT '', "
                       "favicon TEXT NOT NULL DEFAULT '', "
                       "position INTEGER NOT NULL, "
                       "created INTEGER NOT NULL)"),
        QStringLiteral("CREATE TABLE IF NOT EXISTS setting ("
                       "name TEXT PRIMARY KEY, "
                       "value TEXT NOT NULL)"),
    };
    return statements;
}

} // namespace

Storage::Storage(const QString &dataDirectory)
    : m_connectionName(QStringLiteral("tuuli-") + QUuid::createUuid().toString())
{
    QDir dir(dataDirectory);
    if (dataDirectory.isEmpty() || (!dir.exists() && !dir.mkpath(QStringLiteral(".")))) {
        qWarning() << "Storage: cannot create data directory" << dataDirectory;
        return;
    }

    m_databasePath = dir.absoluteFilePath(QLatin1String(DatabaseFileName));
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_databasePath);
    if (!db.open()) {
        qWarning() << "Storage: cannot open" << m_databasePath << db.lastError().text();
        return;
    }

    if (!applySchema()) {
        qWarning() << "Storage: schema setup failed for" << m_databasePath;
        db.close();
    }
}

Storage::~Storage()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Storage::isOpen() const
{
    return QSqlDatabase::database(m_connectionName, false).isOpen();
}

QSqlDatabase Storage::database() const
{
    return QSqlDatabase::database(m_connectionName, false);
}

QString Storage::databasePath() const
{
    return m_databasePath;
}

int Storage::userVersion() const
{
    QSqlQuery query(database());
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

QString Storage::defaultDataDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString Storage::defaultConfigFilePath()
{
    // Sandboxed apps must not use the default QSettings path; this is the layout
    // recommended by sailjail-permissions/README.md.
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QLatin1Char('/') +
           QCoreApplication::applicationName() + QStringLiteral(".conf");
}

QVariant Storage::text(const QString &value)
{
    return value.isNull() ? QVariant(QStringLiteral("")) : QVariant(value);
}

bool Storage::applySchema() const
{
    QSqlDatabase db = database();
    const int version = userVersion();
    if (version == SchemaVersion) {
        return true;
    }
    if (version > SchemaVersion) {
        qWarning() << "Storage: database is newer than this build:" << version;
        return false;
    }

    if (!db.transaction()) {
        return false;
    }
    for (const QString &statement : schemaStatements()) {
        QSqlQuery query(db);
        if (!query.exec(statement)) {
            qWarning() << "Storage:" << query.lastError().text();
            db.rollback();
            return false;
        }
    }
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA user_version = %1").arg(SchemaVersion))) {
        db.rollback();
        return false;
    }
    return db.commit();
}

} // namespace Tuuli
