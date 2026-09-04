/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "permissionstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>
#include <QVariantMap>

namespace Tuuli {

PermissionStore::PermissionStore(const QString& filePath, QObject* parent)
    : QObject(parent), m_path(filePath)
{
    load();
}

QString PermissionStore::normalizeOrigin(const QString& origin)
{
    const QUrl u(origin);
    if (!u.isValid() || u.scheme().isEmpty() || u.host().isEmpty())
        return origin.trimmed().toLower();
    QString out = u.scheme().toLower() + QStringLiteral("://") + u.host().toLower();
    if (u.port() > 0)
        out += QLatin1Char(':') + QString::number(u.port());
    return out;
}

PermissionStore::Decision PermissionStore::decision(const QString& origin, PermissionKind kind) const
{
    auto it = m_decisions.constFind(normalizeOrigin(origin));
    if (it == m_decisions.constEnd())
        return Ask;
    return it->value(static_cast<int>(kind), Ask);
}

int PermissionStore::decisionFor(const QString& origin, int kind) const
{
    return decision(origin, static_cast<PermissionKind>(kind));
}

void PermissionStore::setDecision(const QString& origin, int kind, int decision)
{
    setDecision(origin, static_cast<PermissionKind>(kind), static_cast<Decision>(decision));
}

void PermissionStore::setDecision(const QString& origin, PermissionKind kind, Decision decision)
{
    const QString key = normalizeOrigin(origin);
    if (key.isEmpty())
        return;
    if (decision == Ask) {
        auto it = m_decisions.find(key);
        if (it != m_decisions.end()) {
            it->remove(static_cast<int>(kind));
            if (it->isEmpty())
                m_decisions.erase(it);
        }
    } else {
        m_decisions[key][static_cast<int>(kind)] = decision;
    }
    save();
    emit changed();
}

void PermissionStore::clearOrigin(const QString& origin)
{
    if (m_decisions.remove(normalizeOrigin(origin)) > 0) {
        save();
        emit changed();
    }
}

void PermissionStore::clearAll()
{
    if (m_decisions.isEmpty())
        return;
    m_decisions.clear();
    save();
    emit changed();
}

int PermissionStore::count() const
{
    int n = 0;
    for (auto it = m_decisions.constBegin(); it != m_decisions.constEnd(); ++it)
        n += it->size();
    return n;
}

QVariantList PermissionStore::entries() const
{
    QVariantList out;
    QStringList origins = m_decisions.keys();
    origins.sort();
    for (const QString& origin : origins) {
        const QHash<int, Decision>& kinds = m_decisions.value(origin);
        QList<int> keys = kinds.keys();
        std::sort(keys.begin(), keys.end());
        for (int kind : keys) {
            QVariantMap e;
            e.insert(QStringLiteral("origin"), origin);
            e.insert(QStringLiteral("kind"), kind);
            e.insert(QStringLiteral("kindName"), PermissionRequest::kindName(static_cast<PermissionKind>(kind)));
            e.insert(QStringLiteral("decision"), static_cast<int>(kinds.value(kind)));
            out.append(e);
        }
    }
    return out;
}

bool PermissionStore::answerFromStore(PermissionRequest* request) const
{
    if (!request)
        return false;
    switch (decision(request->origin(), request->kind())) {
    case Allow: request->allow(); return true;
    case Deny: request->deny(); return true;
    case Ask: return false;
    }
    return false;
}

bool PermissionStore::load()
{
    m_decisions.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        const QJsonObject kinds = it.value().toObject();
        for (auto k = kinds.constBegin(); k != kinds.constEnd(); ++k) {
            bool ok = false;
            const int kind = k.key().toInt(&ok);
            if (!ok)
                continue;
            const QString v = k.value().toString();
            if (v == QLatin1String("allow"))
                m_decisions[it.key()][kind] = Allow;
            else if (v == QLatin1String("deny"))
                m_decisions[it.key()][kind] = Deny;
        }
    }
    return true;
}

bool PermissionStore::save() const
{
    if (m_path.isEmpty())
        return false;
    QJsonObject root;
    for (auto it = m_decisions.constBegin(); it != m_decisions.constEnd(); ++it) {
        QJsonObject kinds;
        for (auto k = it->constBegin(); k != it->constEnd(); ++k)
            kinds.insert(QString::number(k.key()),
                         k.value() == Allow ? QStringLiteral("allow") : QStringLiteral("deny"));
        root.insert(it.key(), kinds);
    }
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace Tuuli
