// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Modelled on sailfish-browser apps/browser/bookmarks/declarativebookmarkmodel.{h,cpp}
// (Copyright (c) 2013 - 2021 Jolla Ltd., MPL-2.0), stored in SQLite instead of JSON so
// Phase 2 folders are a column, not a file-format change.
#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace Tuuli {

class Storage;

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString activeUrl READ activeUrl WRITE setActiveUrl NOTIFY activeUrlChanged)
    Q_PROPERTY(bool activeUrlBookmarked READ activeUrlBookmarked NOTIFY activeUrlBookmarkedChanged)

public:
    enum Role
    {
        BookmarkIdRole = Qt::UserRole + 1,
        UrlRole,
        TitleRole,
        FaviconRole
    };

    explicit BookmarkModel(Storage &storage, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString activeUrl() const;
    void setActiveUrl(const QString &url);
    bool activeUrlBookmarked() const;

    // Returns the bookmark id; an already bookmarked url returns its existing id.
    Q_INVOKABLE int add(const QString &url, const QString &title,
                        const QString &favicon = QString());
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE bool removeByUrl(const QString &url);
    Q_INVOKABLE void edit(int index, const QString &url, const QString &title);
    Q_INVOKABLE bool contains(const QString &url) const;
    Q_INVOKABLE void updateFavicon(const QString &url, const QString &favicon);
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void activeUrlChanged();
    void activeUrlBookmarkedChanged();

private:
    struct Bookmark
    {
        int id = 0;
        QString url;
        QString title;
        QString favicon;
    };

    int indexOfUrl(const QString &url) const;
    void notifyRow(int index, const QVector<int> &roles);
    void reload();

    QSqlDatabase m_db;
    QList<Bookmark> m_bookmarks;
    QString m_activeUrl;
};

} // namespace Tuuli
