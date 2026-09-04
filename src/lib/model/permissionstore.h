/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_PERMISSIONSTORE_H
#define TUULI_PERMISSIONSTORE_H

/*
 * Per-origin permission decisions (spec 8.3): denied by default, remembered
 * per origin when the user asks, stored as JSON in the data dir.  Private
 * tabs consult stored decisions but never write them.
 */

#include "engine/engine.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace Tuuli {

class PermissionStore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY changed)

public:
    enum Decision { Ask = 0, Allow = 1, Deny = 2 };
    Q_ENUM(Decision)

    explicit PermissionStore(const QString& filePath, QObject* parent = nullptr);

    Decision decision(const QString& origin, PermissionKind kind) const;
    Q_INVOKABLE int decisionFor(const QString& origin, int kind) const;
    Q_INVOKABLE void setDecision(const QString& origin, int kind, int decision);
    void setDecision(const QString& origin, PermissionKind kind, Decision decision);
    Q_INVOKABLE void clearOrigin(const QString& origin);
    Q_INVOKABLE void clearAll();
    int count() const;

    /* [{origin, kind, kindName, decision}] for the settings UI. */
    Q_INVOKABLE QVariantList entries() const;

    /* Answer a request from the store if a decision exists.  Returns true
     * when handled; otherwise the caller must prompt.  A prompt that is
     * dismissed without an answer must end in deny(). */
    bool answerFromStore(PermissionRequest* request) const;

    static QString normalizeOrigin(const QString& origin);

    bool load();
    bool save() const;

signals:
    void changed();

private:
    QString m_path;
    QHash<QString, QHash<int, Decision>> m_decisions;
};

} // namespace Tuuli

#endif
