// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "BookmarkModel.h"

#include "storage/Storage.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QtDebug>

namespace Tuuli {

namespace {

bool run(QSqlQuery &query)
{
    if (!query.exec()) {
        qWarning() << "BookmarkModel:" << query.lastError().text() << query.lastQuery();
        return false;
    }
    return true;
}

} // namespace

BookmarkModel::BookmarkModel(Storage &storage, QObject *parent)
    : QAbstractListModel(parent)
    , m_db(storage.database())
{
    reload();
}

int BookmarkModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }
    return m_bookmarks.count();
}

QVariant BookmarkModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_bookmarks.count()) {
        return {};
    }
    const Bookmark &bookmark = m_bookmarks.at(index.row());
    switch (role) {
    case BookmarkIdRole:
        return bookmark.id;
    case UrlRole:
        return bookmark.url;
    case TitleRole:
        return bookmark.title.isEmpty() ? bookmark.url : bookmark.title;
    case FaviconRole:
        return bookmark.favicon;
    default:
        return {};
    }
}

QHash<int, QByteArray> BookmarkModel::roleNames() const
{
    return {
        {BookmarkIdRole, QByteArrayLiteral("bookmarkId")},
        {UrlRole, QByteArrayLiteral("url")},
        {TitleRole, QByteArrayLiteral("title")},
        {FaviconRole, QByteArrayLiteral("favicon")},
    };
}

int BookmarkModel::count() const
{
    return m_bookmarks.count();
}

QString BookmarkModel::activeUrl() const
{
    return m_activeUrl;
}

void BookmarkModel::setActiveUrl(const QString &url)
{
    if (m_activeUrl == url) {
        return;
    }
    m_activeUrl = url;
    emit activeUrlChanged();
    emit activeUrlBookmarkedChanged();
}

bool BookmarkModel::activeUrlBookmarked() const
{
    return contains(m_activeUrl);
}

int BookmarkModel::add(const QString &url, const QString &title, const QString &favicon)
{
    if (url.isEmpty()) {
        return 0;
    }
    const int existing = indexOfUrl(url);
    if (existing >= 0) {
        return m_bookmarks.at(existing).id;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("INSERT INTO bookmark (url, title, favicon, position, created) "
                                 "VALUES (?, ?, ?, "
                                 "(SELECT COALESCE(MAX(position), 0) + 1 FROM bookmark), ?)"));
    query.addBindValue(url);
    query.addBindValue(Storage::text(title));
    query.addBindValue(Storage::text(favicon));
    query.addBindValue(QDateTime::currentDateTimeUtc().toMSecsSinceEpoch() / 1000);
    if (!run(query)) {
        return 0;
    }

    Bookmark bookmark;
    bookmark.id = query.lastInsertId().toInt();
    bookmark.url = url;
    bookmark.title = title;
    bookmark.favicon = favicon;

    const int index = m_bookmarks.count();
    beginInsertRows(QModelIndex(), index, index);
    m_bookmarks.append(bookmark);
    endInsertRows();
    emit countChanged();
    emit activeUrlBookmarkedChanged();
    return bookmark.id;
}

void BookmarkModel::remove(int index)
{
    if (index < 0 || index >= m_bookmarks.count()) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM bookmark WHERE id = ?"));
    query.addBindValue(m_bookmarks.at(index).id);
    if (!run(query)) {
        return;
    }
    beginRemoveRows(QModelIndex(), index, index);
    m_bookmarks.removeAt(index);
    endRemoveRows();
    emit countChanged();
    emit activeUrlBookmarkedChanged();
}

bool BookmarkModel::removeByUrl(const QString &url)
{
    const int index = indexOfUrl(url);
    if (index < 0) {
        return false;
    }
    remove(index);
    return true;
}

void BookmarkModel::edit(int index, const QString &url, const QString &title)
{
    if (index < 0 || index >= m_bookmarks.count() || url.isEmpty()) {
        return;
    }
    Bookmark &bookmark = m_bookmarks[index];
    if (bookmark.url == url && bookmark.title == title) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE bookmark SET url = ?, title = ? WHERE id = ?"));
    query.addBindValue(url);
    query.addBindValue(Storage::text(title));
    query.addBindValue(bookmark.id);
    if (!run(query)) {
        return;
    }
    QVector<int> roles;
    if (bookmark.url != url) {
        bookmark.url = url;
        roles.append(UrlRole);
    }
    if (bookmark.title != title) {
        bookmark.title = title;
        roles.append(TitleRole);
    }
    notifyRow(index, roles);
    emit activeUrlBookmarkedChanged();
}

bool BookmarkModel::contains(const QString &url) const
{
    return indexOfUrl(url) >= 0;
}

void BookmarkModel::updateFavicon(const QString &url, const QString &favicon)
{
    const int index = indexOfUrl(url);
    if (index < 0 || m_bookmarks.at(index).favicon == favicon) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE bookmark SET favicon = ? WHERE id = ?"));
    query.addBindValue(favicon);
    query.addBindValue(m_bookmarks.at(index).id);
    if (!run(query)) {
        return;
    }
    m_bookmarks[index].favicon = favicon;
    notifyRow(index, QVector<int>{FaviconRole});
}

void BookmarkModel::clear()
{
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM bookmark"));
    if (run(query)) {
        reload();
    }
}

int BookmarkModel::indexOfUrl(const QString &url) const
{
    for (int i = 0; i < m_bookmarks.count(); ++i) {
        if (m_bookmarks.at(i).url == url) {
            return i;
        }
    }
    return -1;
}

void BookmarkModel::notifyRow(int index, const QVector<int> &roles)
{
    const QModelIndex modelIndex = this->index(index, 0);
    emit dataChanged(modelIndex, modelIndex, roles);
}

void BookmarkModel::reload()
{
    QSqlQuery query(m_db);
    query.prepare(
        QStringLiteral("SELECT id, url, title, favicon FROM bookmark ORDER BY position ASC"));
    if (!run(query)) {
        return;
    }
    QList<Bookmark> bookmarks;
    while (query.next()) {
        Bookmark bookmark;
        bookmark.id = query.value(0).toInt();
        bookmark.url = query.value(1).toString();
        bookmark.title = query.value(2).toString();
        bookmark.favicon = query.value(3).toString();
        bookmarks.append(bookmark);
    }
    const int oldCount = m_bookmarks.count();
    beginResetModel();
    m_bookmarks = bookmarks;
    endResetModel();
    if (oldCount != m_bookmarks.count()) {
        emit countChanged();
    }
    emit activeUrlBookmarkedChanged();
}

} // namespace Tuuli
