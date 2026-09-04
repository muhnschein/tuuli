/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_BOOKMARKMODEL_H
#define TUULI_BOOKMARKMODEL_H

#include <QAbstractListModel>
#include <QSqlDatabase>
#include <QUrl>
#include <QVector>

namespace Tuuli {

struct Bookmark {
    int id = 0;
    QUrl url;
    QString title;
    qint64 created = 0;
    int position = 0;
};

class BookmarkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, UrlRole, TitleRole, CreatedRole };

    explicit BookmarkModel(const QString& databasePath, QObject* parent = nullptr);
    ~BookmarkModel();

    bool isOpen() const { return m_db.isOpen(); }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_rows.size(); }

    Q_INVOKABLE bool add(const QUrl& url, const QString& title);
    Q_INVOKABLE bool remove(const QUrl& url);
    Q_INVOKABLE bool contains(const QUrl& url) const;
    Q_INVOKABLE bool rename(const QUrl& url, const QString& title);
    Q_INVOKABLE bool move(int from, int to);
    Q_INVOKABLE void refresh();

signals:
    void countChanged();
    void changed();

private:
    bool ensureSchema();
    QSqlDatabase m_db;
    QString m_connectionName;
    QVector<Bookmark> m_rows;
};

} // namespace Tuuli

#endif
