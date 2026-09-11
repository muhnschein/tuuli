// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Modelled on sailfish-browser apps/history/declarativehistorymodel.{h,cpp} and the
// browser_history handling in apps/storage/dbworker.cpp (Copyright (c) 2013 - 2021
// Jolla Ltd., MPL-2.0). Queries run synchronously: the table is capped at MaxEntries.
// Visit times are stored as milliseconds since the epoch, newest first.
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QSqlDatabase>
#include <QString>

namespace Tuuli {

class Storage;

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString searchTerm READ searchTerm WRITE setSearchTerm NOTIFY searchTermChanged)

public:
    enum Role
    {
        UrlRole = Qt::UserRole + 1,
        TitleRole,
        DateRole,
        VisitCountRole
    };

    static const int MaxEntries = 2000;
    static const int DisplayLimit = 500;

    explicit HistoryModel(Storage &storage, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    QString searchTerm() const;
    void setSearchTerm(const QString &term);

    Q_INVOKABLE void visit(const QString &url, const QString &title = QString());
    Q_INVOKABLE void updateTitle(const QString &url, const QString &title);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();

signals:
    void countChanged();
    void searchTermChanged();

private:
    struct Entry
    {
        int id = 0;
        QString url;
        QString title;
        QDateTime date;
        int visitCount = 0;
    };

    static bool isRecordable(const QString &url);
    void prune();
    void reload();

    QSqlDatabase m_db;
    QList<Entry> m_entries;
    QString m_searchTerm;
    qint64 m_lastVisit = 0;
};

} // namespace Tuuli
