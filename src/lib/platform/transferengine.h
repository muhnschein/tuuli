/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TRANSFERENGINE_H
#define TUULI_TRANSFERENGINE_H

/*
 * Thin client for Nemo Transfer Engine (spec 7.1 Downloads, 8.3 Share) so
 * downloads show up in the system Transfers UI.  Everything is best-effort:
 * on a host without the service the calls are no-ops returning -1.
 */

#include <QObject>
#include <QString>

namespace Tuuli {

class TransferEngine : public QObject
{
    Q_OBJECT
public:
    /* org.nemo.transferengine TransferStatus */
    enum Status {
        Unknown = 0,
        NotStarted = 1,
        TransferStarted = 2,
        TransferCanceled = 3,
        TransferFinished = 4,
        TransferInterrupted = 5
    };

    explicit TransferEngine(QObject* parent = nullptr);

    bool available() const;

    /* Returns the transfer id or -1. */
    int createDownload(const QString& displayName, const QString& filePath, const QString& mimeType,
                       qint64 expectedSize);
    void startTransfer(int transferId);
    void updateProgress(int transferId, double progress);
    void finishTransfer(int transferId, Status status, const QString& reason = QString());

    static QString serviceName();
    static QString objectPath();
    static QString interfaceName();
};

} // namespace Tuuli

#endif
