/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "bookmarkmodel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace Tuuli {

BookmarkModel::BookmarkModel(const QString& databasePath, QObject* parent)
    : QAbstractListModel(parent)
{
    m_connectionName = QStringLiteral("tuuli-bookmarks-") + QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    if (databasePath != QLatin1String(":memory:"))
        QDir().mkpath(QFileInfo(databasePath).absolutePath());
    m_db.setDatabaseName(databasePath);
    if (m_db.open())
        ensureSchema();
    refresh();
}

BookmarkModel::~BookmarkModel()
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool BookmarkModel::ensureSchema()
{
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS bookmarks ("
        " id INTEGER PRIMARY KEY,"
        " url TEXT NOT NULL UNIQUE,"
        " title TEXT,"
        " created INTEGER NOT NULL,"
        " position INTEGER NOT NULL DEFAULT 0)"));
}

int BookmarkModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant BookmarkModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const Bookmark& b = m_rows.at(index.row());
    switch (role) {
    case IdRole: return b.id;
    case UrlRole: return b.url;
    case TitleRole: return b.title.isEmpty() ? b.url.host() : b.title;
    case CreatedRole: return QDateTime::fromMSecsSinceEpoch(b.created);
    }
    return QVariant();
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "bookmarkId";
    roles[UrlRole] = "url";
    roles[TitleRole] = "title";
    roles[CreatedRole] = "created";
    return roles;
}

bool BookmarkModel::add(const QUrl& url, const QString& title)
{
    if (!m_db.isOpen() || !url.isValid() || url.isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO bookmarks(url, title, created, position)"
                             " VALUES(:url, :title, :created,"
                             " (SELECT COALESCE(MAX(position), 0) + 1 FROM bookmarks))"));
    q.bindValue(QStringLiteral(":url"), url.toString());
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":created"), QDateTime::currentMSecsSinceEpoch());
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok) {
        refresh();
        emit changed();
    }
    return ok;
}

bool BookmarkModel::remove(const QUrl& url)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM bookmarks WHERE url = :url"));
    q.bindValue(QStringLiteral(":url"), url.toString());
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok) {
        refresh();
        emit changed();
    }
    return ok;
}

bool BookmarkModel::contains(const QUrl& url) const
{
    for (const Bookmark& b : m_rows)
        if (b.url == url)
            return true;
    return false;
}

bool BookmarkModel::rename(const QUrl& url, const QString& title)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE bookmarks SET title = :title WHERE url = :url"));
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":url"), url.toString());
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok) {
        refresh();
        emit changed();
    }
    return ok;
}

bool BookmarkModel::move(int from, int to)
{
    if (from < 0 || to < 0 || from >= m_rows.size() || to >= m_rows.size() || from == to)
        return false;
    QVector<Bookmark> rows = m_rows;
    const Bookmark moved = rows.takeAt(from);
    rows.insert(to, moved);
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE bookmarks SET position = :pos WHERE id = :id"));
    for (int i = 0; i < rows.size(); ++i) {
        q.bindValue(QStringLiteral(":pos"), i + 1);
        q.bindValue(QStringLiteral(":id"), rows.at(i).id);
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();
    refresh();
    emit changed();
    return true;
}

void BookmarkModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    if (m_db.isOpen()) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("SELECT id, url, title, created, position FROM bookmarks"
                                  " ORDER BY position ASC, id ASC"))) {
            while (q.next()) {
                Bookmark b;
                b.id = q.value(0).toInt();
                b.url = QUrl(q.value(1).toString());
                b.title = q.value(2).toString();
                b.created = q.value(3).toLongLong();
                b.position = q.value(4).toInt();
                m_rows.append(b);
            }
        }
    }
    endResetModel();
    emit countChanged();
}

} // namespace Tuuli
