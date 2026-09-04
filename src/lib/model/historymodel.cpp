/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "historymodel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace Tuuli {

HistoryModel::HistoryModel(const QString& databasePath, QObject* parent)
    : QAbstractListModel(parent)
{
    m_connectionName = QStringLiteral("tuuli-history-") + QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    if (databasePath != QLatin1String(":memory:"))
        QDir().mkpath(QFileInfo(databasePath).absolutePath());
    m_db.setDatabaseName(databasePath);
    if (m_db.open())
        ensureSchema();
    refresh();
}

HistoryModel::~HistoryModel()
{
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool HistoryModel::ensureSchema()
{
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS history ("
        " id INTEGER PRIMARY KEY,"
        " url TEXT NOT NULL UNIQUE,"
        " title TEXT,"
        " visits INTEGER NOT NULL DEFAULT 0,"
        " last_visit INTEGER NOT NULL DEFAULT 0)"))
        && q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS history_last_visit ON history(last_visit DESC)"));
}

int HistoryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant HistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return QVariant();
    const HistoryEntry& e = m_rows.at(index.row());
    switch (role) {
    case UrlRole: return e.url;
    case TitleRole: return e.title.isEmpty() ? e.url.host() : e.title;
    case VisitsRole: return e.visits;
    case LastVisitRole: return QDateTime::fromMSecsSinceEpoch(e.lastVisit);
    }
    return QVariant();
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UrlRole] = "url";
    roles[TitleRole] = "title";
    roles[VisitsRole] = "visits";
    roles[LastVisitRole] = "lastVisit";
    return roles;
}

void HistoryModel::setFilter(const QString& filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    emit filterChanged();
    refresh();
}

void HistoryModel::setLimit(int limit)
{
    limit = qMax(1, limit);
    if (m_limit == limit)
        return;
    m_limit = limit;
    emit limitChanged();
    refresh();
}

static bool isRecordable(const QUrl& url)
{
    const QString scheme = url.scheme();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https"));
}

bool HistoryModel::addVisit(const QUrl& url, const QString& title, bool isPrivate)
{
    if (isPrivate || !m_db.isOpen() || !isRecordable(url))
        return false;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Update-then-insert rather than UPSERT: portable across the SQLite
    // versions in the SDK targets and needs no reused placeholders.
    QSqlQuery up(m_db);
    if (title.isEmpty()) {
        up.prepare(QStringLiteral("UPDATE history SET visits = visits + 1, last_visit = :now WHERE url = :url"));
    } else {
        up.prepare(QStringLiteral("UPDATE history SET visits = visits + 1, last_visit = :now, title = :title WHERE url = :url"));
        up.bindValue(QStringLiteral(":title"), title);
    }
    up.bindValue(QStringLiteral(":now"), now);
    up.bindValue(QStringLiteral(":url"), url.toString());
    if (!up.exec())
        return false;
    bool ok = up.numRowsAffected() > 0;
    if (!ok) {
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral("INSERT INTO history(url, title, visits, last_visit) VALUES(:url, :title, 1, :now)"));
        ins.bindValue(QStringLiteral(":url"), url.toString());
        ins.bindValue(QStringLiteral(":title"), title);
        ins.bindValue(QStringLiteral(":now"), now);
        ok = ins.exec();
    }
    if (ok)
        refresh();
    return ok;
}

bool HistoryModel::updateTitle(const QUrl& url, const QString& title, bool isPrivate)
{
    if (isPrivate || !m_db.isOpen() || title.isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE history SET title = :title WHERE url = :url"));
    q.bindValue(QStringLiteral(":title"), title);
    q.bindValue(QStringLiteral(":url"), url.toString());
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok)
        refresh();
    return ok;
}

bool HistoryModel::remove(const QUrl& url)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM history WHERE url = :url"));
    q.bindValue(QStringLiteral(":url"), url.toString());
    const bool ok = q.exec();
    if (ok)
        refresh();
    return ok;
}

bool HistoryModel::clear()
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    const bool ok = q.exec(QStringLiteral("DELETE FROM history"));
    if (ok)
        refresh();
    return ok;
}

QVector<HistoryEntry> HistoryModel::search(const QString& text, int limit) const
{
    QVector<HistoryEntry> out;
    if (!m_db.isOpen())
        return out;
    QSqlQuery q(m_db);
    if (text.isEmpty()) {
        q.prepare(QStringLiteral("SELECT url, title, visits, last_visit FROM history"
                                 " ORDER BY last_visit DESC LIMIT :limit"));
    } else {
        q.prepare(QStringLiteral("SELECT url, title, visits, last_visit FROM history"
                                 " WHERE url LIKE :pat OR title LIKE :pat"
                                 " ORDER BY visits DESC, last_visit DESC LIMIT :limit"));
        q.bindValue(QStringLiteral(":pat"), QLatin1Char('%') + text + QLatin1Char('%'));
    }
    q.bindValue(QStringLiteral(":limit"), limit);
    if (!q.exec())
        return out;
    while (q.next()) {
        HistoryEntry e;
        e.url = QUrl(q.value(0).toString());
        e.title = q.value(1).toString();
        e.visits = q.value(2).toInt();
        e.lastVisit = q.value(3).toLongLong();
        out.append(e);
    }
    return out;
}

int HistoryModel::totalCount() const
{
    if (!m_db.isOpen())
        return 0;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM history")) || !q.next())
        return 0;
    return q.value(0).toInt();
}

void HistoryModel::refresh()
{
    beginResetModel();
    m_rows = search(m_filter, m_limit);
    endResetModel();
    emit countChanged();
}

} // namespace Tuuli
