/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "sessionstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace Tuuli {

static const int kSessionFormatVersion = 1;

SessionStore::SessionStore(const QString& filePath, QObject* parent)
    : QObject(parent), m_path(filePath)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, [this]() { flush(); });
}

void SessionStore::scheduleSave(const Session& session)
{
    m_pending = session;
    m_hasPending = true;
    m_timer.start(m_debounceMs);
}

bool SessionStore::flush()
{
    m_timer.stop();
    if (!m_hasPending)
        return true;
    m_hasPending = false;
    return saveNow(m_pending);
}

bool SessionStore::saveNow(const Session& session)
{
    m_timer.stop();
    m_hasPending = false;
    const QJsonDocument doc(toJson(session));
    return writeAtomically(doc.toJson(QJsonDocument::Compact));
}

bool SessionStore::writeAtomically(const QByteArray& data)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit saveFailed(file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        emit saveFailed(file.errorString());
        return false;
    }
    emit saved();
    return true;
}

bool SessionStore::exists() const
{
    return QFile::exists(m_path);
}

bool SessionStore::remove()
{
    return QFile::remove(m_path);
}

Session SessionStore::load(bool* ok) const
{
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return Session();
    }
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok) *ok = false;
        return Session();
    }
    return fromJson(doc.object(), ok);
}

QJsonObject SessionStore::toJson(const Session& session)
{
    QJsonArray tabs;
    for (const SessionTab& t : session.tabs) {
        QJsonObject o;
        o.insert(QStringLiteral("url"), t.url.toString());
        o.insert(QStringLiteral("title"), t.title);
        o.insert(QStringLiteral("scrollX"), t.scroll.x());
        o.insert(QStringLiteral("scrollY"), t.scroll.y());
        o.insert(QStringLiteral("zoom"), t.zoom);
        o.insert(QStringLiteral("desktopMode"), t.desktopMode);
        tabs.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), kSessionFormatVersion);
    root.insert(QStringLiteral("tabs"), tabs);
    root.insert(QStringLiteral("currentIndex"), session.currentIndex);
    root.insert(QStringLiteral("cleanExit"), session.cleanExit);
    return root;
}

Session SessionStore::fromJson(const QJsonObject& root, bool* ok)
{
    Session s;
    if (root.value(QStringLiteral("version")).toInt(0) > kSessionFormatVersion) {
        if (ok) *ok = false;
        return s;
    }
    const QJsonArray tabs = root.value(QStringLiteral("tabs")).toArray();
    for (const QJsonValue& v : tabs) {
        const QJsonObject o = v.toObject();
        SessionTab t;
        t.url = QUrl(o.value(QStringLiteral("url")).toString());
        if (t.url.isEmpty())
            continue;
        t.title = o.value(QStringLiteral("title")).toString();
        t.scroll = QPointF(o.value(QStringLiteral("scrollX")).toDouble(),
                           o.value(QStringLiteral("scrollY")).toDouble());
        t.zoom = o.value(QStringLiteral("zoom")).toDouble(1.0);
        if (!(t.zoom > 0))
            t.zoom = 1.0;
        t.desktopMode = o.value(QStringLiteral("desktopMode")).toBool(false);
        s.tabs.append(t);
    }
    s.currentIndex = root.value(QStringLiteral("currentIndex")).toInt(-1);
    if (s.currentIndex >= s.tabs.size())
        s.currentIndex = s.tabs.size() - 1;
    if (s.currentIndex < 0 && !s.tabs.isEmpty())
        s.currentIndex = 0;
    s.cleanExit = root.value(QStringLiteral("cleanExit")).toBool(false);
    if (ok) *ok = true;
    return s;
}

} // namespace Tuuli
