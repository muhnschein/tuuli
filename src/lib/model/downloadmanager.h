/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_DOWNLOADMANAGER_H
#define TUULI_DOWNLOADMANAGER_H

/*
 * Downloads (spec 7.1): the engine performs the transfer, we pick the
 * destination and mirror progress into Nemo Transfer Engine so the system
 * Transfers page shows it.  Downloads from private tabs are listed only for
 * the session and never registered with Transfer Engine (spec 7.3).
 */

#include "engine/engine.h"

#include <QAbstractListModel>
#include <QVector>

namespace Tuuli {

class TransferEngine;

struct DownloadItem {
    int id = 0;
    QUrl url;
    QString fileName;
    QString path;
    QString mimeType;
    qint64 received = 0;
    qint64 total = -1;
    bool finished = false;
    bool ok = false;
    QString error;
    bool isPrivate = false;
    int transferId = -1;
    DownloadRequest* request = nullptr;
};

class DownloadManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY countChanged)
    Q_PROPERTY(QString directory READ directory WRITE setDirectory NOTIFY directoryChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1, UrlRole, FileNameRole, PathRole, MimeRole,
        ReceivedRole, TotalRole, ProgressRole, FinishedRole, OkRole, ErrorRole, PrivateRole
    };

    explicit DownloadManager(TransferEngine* transfers, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    int activeCount() const;
    QString directory() const { return m_directory; }
    void setDirectory(const QString& dir);

    /* Accepts the request into the download directory. */
    void handleRequest(DownloadRequest* request, bool isPrivate);

    Q_INVOKABLE void cancel(int id);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void clearPrivate();

    /* Transfer Engine callbacks (D-Bus adaptor). */
    Q_INVOKABLE void cancelTransfer(int transferId);
    Q_INVOKABLE void restartTransfer(int transferId);

    static QString uniquePath(const QString& directory, const QString& suggestedName);
    static QString sanitizeFileName(const QString& name);

signals:
    void countChanged();
    void directoryChanged();
    void downloadStarted(int id, const QString& fileName);
    void downloadFinished(int id, bool ok, const QString& path);

private:
    int rowOf(int id) const;
    void emitRow(int row);

    TransferEngine* m_transfers;
    QString m_directory;
    QVector<DownloadItem> m_items;
    int m_nextId = 1;
};

} // namespace Tuuli

#endif
