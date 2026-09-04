/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "transferengine.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QStringList>

namespace Tuuli {

QString TransferEngine::serviceName() { return QStringLiteral("org.nemo.transferengine"); }
QString TransferEngine::objectPath() { return QStringLiteral("/org/nemo/transferengine"); }
QString TransferEngine::interfaceName() { return QStringLiteral("org.nemo.transferengine"); }

TransferEngine::TransferEngine(QObject* parent)
    : QObject(parent)
{
}

bool TransferEngine::available() const
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface())
        return false;
    // The service is activatable; treat "known or activatable" as available.
    QDBusReply<bool> reg = bus.interface()->isServiceRegistered(serviceName());
    if (reg.isValid() && reg.value())
        return true;
    QDBusReply<QStringList> act = bus.interface()->call(QStringLiteral("ListActivatableNames"));
    return act.isValid() && act.value().contains(serviceName());
}

int TransferEngine::createDownload(const QString& displayName, const QString& filePath,
                                   const QString& mimeType, qint64 expectedSize)
{
    QDBusInterface iface(serviceName(), objectPath(), interfaceName(), QDBusConnection::sessionBus());
    if (!iface.isValid())
        return -1;
    // Cancel/restart callbacks are served by DownloadManager's D-Bus adaptor.
    const QStringList callback = { QStringLiteral("org.tuuli.browser"),
                                   QStringLiteral("/org/tuuli/browser/downloads"),
                                   QStringLiteral("org.tuuli.browser.Downloads") };
    QDBusReply<int> reply = iface.call(QStringLiteral("createDownload"), displayName,
                                       QStringLiteral("icon-launcher-tuuli-browser"),
                                       QStringLiteral("icon-s-cloud-download"), filePath, mimeType,
                                       expectedSize, callback, QStringLiteral("cancelTransfer"),
                                       QStringLiteral("restartTransfer"));
    return reply.isValid() ? reply.value() : -1;
}

void TransferEngine::startTransfer(int transferId)
{
    if (transferId < 0)
        return;
    QDBusInterface iface(serviceName(), objectPath(), interfaceName(), QDBusConnection::sessionBus());
    if (iface.isValid())
        iface.asyncCall(QStringLiteral("startTransfer"), transferId);
}

void TransferEngine::updateProgress(int transferId, double progress)
{
    if (transferId < 0)
        return;
    QDBusInterface iface(serviceName(), objectPath(), interfaceName(), QDBusConnection::sessionBus());
    if (iface.isValid())
        iface.asyncCall(QStringLiteral("updateTransferProgress"), transferId, progress);
}

void TransferEngine::finishTransfer(int transferId, Status status, const QString& reason)
{
    if (transferId < 0)
        return;
    QDBusInterface iface(serviceName(), objectPath(), interfaceName(), QDBusConnection::sessionBus());
    if (iface.isValid())
        iface.asyncCall(QStringLiteral("finishTransfer"), transferId, static_cast<int>(status), reason);
}

} // namespace Tuuli
