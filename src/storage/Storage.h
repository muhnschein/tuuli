// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QVariant>

namespace Tuuli {

// Owns the single SQLite database (tabs, history, bookmarks, settings table) and
// applies the schema. One Storage per process; models borrow its connection.
// Schema is documented in docs/ARCHITECTURE.md; bump SchemaVersion on change.
class Storage
{
public:
    static const int SchemaVersion = 1;

    explicit Storage(const QString &dataDirectory);
    ~Storage();

    Storage(const Storage &) = delete;
    Storage &operator=(const Storage &) = delete;

    bool isOpen() const;
    QSqlDatabase database() const;
    QString databasePath() const;
    int userVersion() const;

    // Sailjail grants write access only below these locations; see docs/HARBOUR.md.
    static QString defaultDataDirectory();
    static QString defaultConfigFilePath();

    // Bind value for a TEXT NOT NULL column: a null QString would bind SQL NULL.
    static QVariant text(const QString &value);

private:
    bool applySchema() const;

    QString m_connectionName;
    QString m_databasePath;
};

} // namespace Tuuli
