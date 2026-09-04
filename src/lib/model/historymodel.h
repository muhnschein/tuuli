/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_HISTORYMODEL_H
#define TUULI_HISTORYMODEL_H

/*
 * Browsing history in SQLite (QtSql).  Private tabs never write here
 * (spec 7.3); the caller passes the privacy flag and we refuse.
 */

#include <QAbstractListModel>
#include <QDateTime>
#include <QSqlDatabase>
#include <QUrl>
#include <QVector>

namespace Tuuli {

struct HistoryEntry {
    QUrl url;
    QString title;
    int visits = 0;
    qint64 lastVisit = 0;
};

class HistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    enum Roles { UrlRole = Qt::UserRole + 1, TitleRole, VisitsRole, LastVisitRole };

    /* databasePath ":memory:" for tests. */
    explicit HistoryModel(const QString& databasePath, QObject* parent = nullptr);
    ~HistoryModel();

    bool isOpen() const { return m_db.isOpen(); }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_rows.size(); }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);
    int limit() const { return m_limit; }
    void setLimit(int limit);

    Q_INVOKABLE bool addVisit(const QUrl& url, const QString& title, bool isPrivate = false);
    Q_INVOKABLE bool updateTitle(const QUrl& url, const QString& title, bool isPrivate = false);
    Q_INVOKABLE bool remove(const QUrl& url);
    Q_INVOKABLE bool clear();
    Q_INVOKABLE void refresh();

    QVector<HistoryEntry> search(const QString& text, int limit) const;
    int totalCount() const;

signals:
    void countChanged();
    void filterChanged();
    void limitChanged();

private:
    bool ensureSchema();

    QSqlDatabase m_db;
    QString m_connectionName;
    QString m_filter;
    int m_limit = 50;
    QVector<HistoryEntry> m_rows;
};

} // namespace Tuuli

#endif
