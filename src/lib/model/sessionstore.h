/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_SESSIONSTORE_H
#define TUULI_SESSIONSTORE_H

/*
 * Session persistence (spec 8.4): tabs, scroll offsets and zoom, written
 * on a 5-second debounce, on every backgrounding and on aboutToQuit.  With
 * a single-process engine (spec 4.1) this is the crash mitigation, so it is
 * written atomically (temp file + rename) and never skipped.
 *
 * Private tabs are never persisted (spec 7.3).
 */

#include <QJsonObject>
#include <QObject>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVector>

namespace Tuuli {

struct SessionTab {
    QUrl url;
    QString title;
    QPointF scroll;
    qreal zoom = 1.0;
    bool desktopMode = false;
    bool operator==(const SessionTab& o) const
    {
        return url == o.url && title == o.title && scroll == o.scroll && zoom == o.zoom
            && desktopMode == o.desktopMode;
    }
};

struct Session {
    QVector<SessionTab> tabs;
    int currentIndex = -1;
    bool cleanExit = false;
    bool operator==(const Session& o) const
    {
        return tabs == o.tabs && currentIndex == o.currentIndex && cleanExit == o.cleanExit;
    }
};

class SessionStore : public QObject
{
    Q_OBJECT
public:
    explicit SessionStore(const QString& filePath, QObject* parent = nullptr);

    QString filePath() const { return m_path; }
    void setDebounceMs(int ms) { m_debounceMs = ms; }
    int debounceMs() const { return m_debounceMs; }

    /* Queue a snapshot; written after the debounce interval. */
    void scheduleSave(const Session& session);
    /* Write the queued snapshot (or `session`) right now. */
    bool flush();
    bool saveNow(const Session& session);
    bool hasPendingSave() const { return m_timer.isActive(); }

    Session load(bool* ok = nullptr) const;
    bool exists() const;
    bool remove();

    static QJsonObject toJson(const Session& session);
    static Session fromJson(const QJsonObject& object, bool* ok = nullptr);

signals:
    void saved();
    void saveFailed(const QString& error);

private:
    bool writeAtomically(const QByteArray& data);

    QString m_path;
    int m_debounceMs = 5000;
    QTimer m_timer;
    Session m_pending;
    bool m_hasPending = false;
};

} // namespace Tuuli

#endif
