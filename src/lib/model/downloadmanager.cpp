/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "downloadmanager.h"
#include "platform/transferengine.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace Tuuli {

DownloadManager::DownloadManager(TransferEngine* transfers, QObject* parent)
    : QAbstractListModel(parent), m_transfers(transfers)
{
    m_directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

int DownloadManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant DownloadManager::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return QVariant();
    const DownloadItem& d = m_items.at(index.row());
    switch (role) {
    case IdRole: return d.id;
    case UrlRole: return d.url;
    case FileNameRole: return d.fileName;
    case PathRole: return d.path;
    case MimeRole: return d.mimeType;
    case ReceivedRole: return d.received;
    case TotalRole: return d.total;
    case ProgressRole: return d.total > 0 ? double(d.received) / double(d.total) : (d.finished ? 1.0 : 0.0);
    case FinishedRole: return d.finished;
    case OkRole: return d.ok;
    case ErrorRole: return d.error;
    case PrivateRole: return d.isPrivate;
    }
    return QVariant();
}

QHash<int, QByteArray> DownloadManager::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "downloadId";
    roles[UrlRole] = "url";
    roles[FileNameRole] = "fileName";
    roles[PathRole] = "path";
    roles[MimeRole] = "mimeType";
    roles[ReceivedRole] = "received";
    roles[TotalRole] = "total";
    roles[ProgressRole] = "progress";
    roles[FinishedRole] = "finished";
    roles[OkRole] = "ok";
    roles[ErrorRole] = "error";
    roles[PrivateRole] = "isPrivate";
    return roles;
}

int DownloadManager::activeCount() const
{
    int n = 0;
    for (const DownloadItem& d : m_items)
        if (!d.finished)
            ++n;
    return n;
}

void DownloadManager::setDirectory(const QString& dir)
{
    if (dir.isEmpty() || dir == m_directory)
        return;
    m_directory = dir;
    emit directoryChanged();
}

QString DownloadManager::sanitizeFileName(const QString& name)
{
    QString out = name.trimmed();
    out.replace(QLatin1Char('/'), QLatin1Char('_'));
    out.replace(QLatin1Char('\\'), QLatin1Char('_'));
    out.replace(QLatin1Char('\0'), QLatin1Char('_'));
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    if (out.isEmpty())
        out = QStringLiteral("download");
    if (out.size() > 200)
        out = out.left(200);
    return out;
}

QString DownloadManager::uniquePath(const QString& directory, const QString& suggestedName)
{
    const QString name = sanitizeFileName(suggestedName);
    const QFileInfo info(name);
    const QString base = info.completeBaseName().isEmpty() ? name : info.completeBaseName();
    const QString suffix = info.suffix();
    QString candidate = directory + QLatin1Char('/') + name;
    int n = 1;
    while (QFile::exists(candidate)) {
        candidate = directory + QLatin1Char('/') + base + QStringLiteral("(%1)").arg(n++);
        if (!suffix.isEmpty())
            candidate += QLatin1Char('.') + suffix;
    }
    return candidate;
}

void DownloadManager::handleRequest(DownloadRequest* request, bool isPrivate)
{
    if (!request)
        return;
    QDir().mkpath(m_directory);
    DownloadItem item;
    item.id = m_nextId++;
    item.url = request->url();
    item.mimeType = request->mimeType();
    item.total = request->totalBytes();
    item.isPrivate = isPrivate;
    item.request = request;
    QString name = request->suggestedName();
    if (name.isEmpty())
        name = QFileInfo(request->url().path()).fileName();
    item.path = uniquePath(m_directory, name);
    item.fileName = QFileInfo(item.path).fileName();

    if (!isPrivate && m_transfers) {
        item.transferId = m_transfers->createDownload(item.fileName, item.path, item.mimeType, item.total);
        m_transfers->startTransfer(item.transferId);
    }

    const int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);
    m_items.append(item);
    endInsertRows();
    emit countChanged();

    const int id = item.id;
    connect(request, &DownloadRequest::progress, this, [this, id](qint64 received, qint64 total) {
        const int r = rowOf(id);
        if (r < 0)
            return;
        DownloadItem& d = m_items[r];
        d.received = received;
        if (total > 0)
            d.total = total;
        if (d.transferId >= 0 && m_transfers && d.total > 0)
            m_transfers->updateProgress(d.transferId, double(received) / double(d.total));
        emitRow(r);
    });
    connect(request, &DownloadRequest::finished, this, [this, id](bool ok, const QString& error) {
        const int r = rowOf(id);
        if (r < 0)
            return;
        DownloadItem& d = m_items[r];
        d.finished = true;
        d.ok = ok;
        d.error = error;
        d.request = nullptr;
        if (d.transferId >= 0 && m_transfers)
            m_transfers->finishTransfer(d.transferId, ok ? TransferEngine::TransferFinished
                                                         : TransferEngine::TransferInterrupted, error);
        emitRow(r);
        emit countChanged();
        emit downloadFinished(d.id, ok, d.path);
    });
    connect(request, &QObject::destroyed, this, [this, id]() {
        const int r = rowOf(id);
        if (r >= 0)
            m_items[r].request = nullptr;
    });

    request->accept(item.path);
    emit downloadStarted(item.id, item.fileName);
}

void DownloadManager::cancel(int id)
{
    const int r = rowOf(id);
    if (r < 0)
        return;
    DownloadItem& d = m_items[r];
    if (d.request)
        d.request->cancel();
    if (!d.finished) {
        d.finished = true;
        d.ok = false;
        d.error = QStringLiteral("cancelled");
        if (d.transferId >= 0 && m_transfers)
            m_transfers->finishTransfer(d.transferId, TransferEngine::TransferCanceled);
        emitRow(r);
        emit countChanged();
    }
}

void DownloadManager::remove(int id)
{
    const int r = rowOf(id);
    if (r < 0)
        return;
    if (!m_items.at(r).finished)
        cancel(id);
    beginRemoveRows(QModelIndex(), r, r);
    m_items.removeAt(r);
    endRemoveRows();
    emit countChanged();
}

void DownloadManager::clearFinished()
{
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items.at(i).finished)
            remove(m_items.at(i).id);
}

void DownloadManager::clearPrivate()
{
    for (int i = m_items.size() - 1; i >= 0; --i)
        if (m_items.at(i).isPrivate)
            remove(m_items.at(i).id);
}

void DownloadManager::cancelTransfer(int transferId)
{
    for (const DownloadItem& d : m_items)
        if (d.transferId == transferId) {
            cancel(d.id);
            return;
        }
}

void DownloadManager::restartTransfer(int transferId)
{
    // Servo has no resume; a restart is a fresh request from the page.
    Q_UNUSED(transferId);
}

int DownloadManager::rowOf(int id) const
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items.at(i).id == id)
            return i;
    return -1;
}

void DownloadManager::emitRow(int row)
{
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

} // namespace Tuuli
