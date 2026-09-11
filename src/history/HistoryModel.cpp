// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "HistoryModel.h"

#include "storage/Storage.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

namespace Tuuli {

namespace {

bool run(QSqlQuery &query)
{
    if (!query.exec()) {
        qWarning() << "HistoryModel:" << query.lastError().text() << query.lastQuery();
        return false;
    }
    return true;
}

} // namespace

HistoryModel::HistoryModel(Storage &storage, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(storage.database())
{
    prune();
    reload();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_entries.count();
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_entries.count()) {
        return {};
    }
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case UrlRole:
        return entry.url;
    case TitleRole:
        return entry.title.isEmpty() ? entry.url : entry.title;
    case DateRole:
        return entry.date;
    case VisitCountRole:
        return entry.visitCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {UrlRole, QByteArrayLiteral("url")},
        {TitleRole, QByteArrayLiteral("title")},
        {DateRole, QByteArrayLiteral("date")},
        {VisitCountRole, QByteArrayLiteral("visitCount")},
    };
}

int HistoryModel::count() const
{
    return m_entries.count();
}

QString HistoryModel::searchTerm() const
{
    return m_searchTerm;
}

void HistoryModel::setSearchTerm(const QString &term)
{
    if (m_searchTerm == term) {
        return;
    }
    m_searchTerm = term;
    emit searchTermChanged();
    reload();
}

bool HistoryModel::isRecordable(const QString &url)
{
    return !url.isEmpty() && !url.startsWith(QLatin1String("about:"));
}

void HistoryModel::visit(const QString &url, const QString &title)
{
    if (!isRecordable(url)) {
        return;
    }
    const qint64 now = QDateTime::currentDateTimeUtc().toMSecsSinceEpoch();

    QSqlQuery exists(m_db);
    exists.prepare(QStringLiteral("SELECT id FROM browser_history WHERE url = ?"));
    exists.addBindValue(url);
    if (!run(exists)) {
        return;
    }

    QSqlQuery query(m_db);
    if (exists.next()) {
        if (title.isEmpty()) {
            query.prepare(QStringLiteral("UPDATE browser_history SET date = ?, "
                                         "visited_count = visited_count + 1 WHERE url = ?"));
            query.addBindValue(now);
        } else {
            query.prepare(QStringLiteral("UPDATE browser_history SET date = ?, title = ?, "
                                         "visited_count = visited_count + 1 WHERE url = ?"));
            query.addBindValue(now);
            query.addBindValue(title);
        }
        query.addBindValue(url);
    } else {
        query.prepare(
            QStringLiteral("INSERT INTO browser_history (url, title, date) VALUES (?, ?, ?)"));
        query.addBindValue(url);
        query.addBindValue(Storage::text(title));
        query.addBindValue(now);
    }
    if (run(query)) {
        reload();
    }
}

void HistoryModel::updateTitle(const QString &url, const QString &title)
{
    if (!isRecordable(url) || title.isEmpty()) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE browser_history SET title = ? WHERE url = ?"));
    query.addBindValue(title);
    query.addBindValue(url);
    if (!run(query)) {
        return;
    }
    for (int i = 0; i < m_entries.count(); ++i) {
        if (m_entries.at(i).url == url && m_entries.at(i).title != title) {
            m_entries[i].title = title;
            const QModelIndex modelIndex = index(i, 0);
            emit dataChanged(modelIndex, modelIndex, QVector<int>{TitleRole});
        }
    }
}

void HistoryModel::remove(int index)
{
    if (index < 0 || index >= m_entries.count()) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM browser_history WHERE id = ?"));
    query.addBindValue(m_entries.at(index).id);
    if (!run(query)) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_entries.removeAt(index);
    endRemoveRows();
    emit countChanged();
}

void HistoryModel::clear()
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM browser_history"));
    if (run(query)) {
        reload();
    }
}

void HistoryModel::prune()
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM browser_history WHERE id NOT IN "
                                 "(SELECT id FROM browser_history ORDER BY date DESC LIMIT ?)"));
    query.addBindValue(MaxEntries);
    run(query);
}

void HistoryModel::reload()
{
    QSqlQuery query(m_db);
    if (m_searchTerm.isEmpty()) {
        query.prepare(QStringLiteral("SELECT id, url, title, date, visited_count "
                                     "FROM browser_history ORDER BY date DESC, id DESC LIMIT ?"));
    } else {
        query.prepare(QStringLiteral("SELECT id, url, title, date, visited_count "
                                     "FROM browser_history WHERE url LIKE ? OR title LIKE ? "
                                     "ORDER BY date DESC, id DESC LIMIT ?"));
        const QString pattern = QLatin1Char('%') + m_searchTerm + QLatin1Char('%');
        query.addBindValue(pattern);
        query.addBindValue(pattern);
    }
    query.addBindValue(DisplayLimit);
    if (!run(query)) {
        return;
    }

    QList<Entry> entries;
    while (query.next()) {
        Entry entry;
        entry.id = query.value(0).toInt();
        entry.url = query.value(1).toString();
        entry.title = query.value(2).toString();
        entry.date = QDateTime::fromMSecsSinceEpoch(query.value(3).toLongLong());
        entry.visitCount = query.value(4).toInt();
        entries.append(entry);
    }

    const int oldCount = m_entries.count();
    beginResetModel();
    m_entries = entries;
    endResetModel();
    if (oldCount != m_entries.count()) {
        emit countChanged();
    }
}

} // namespace Tuuli
