/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_PERFLOG_H
#define TUULI_PERFLOG_H

/*
 * On-device timing samples for the spec 11 budgets, as JSON lines that
 * tools/perf/run-budgets.py evaluates.  Enabled by the "Performance
 * logging" developer toggle; otherwise every call is a no-op.
 */

#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

namespace Tuuli {

class PerfLog : public QObject
{
    Q_OBJECT
public:
    explicit PerfLog(const QString& filePath, QObject* parent = nullptr);
    ~PerfLog();

    void setEnabled(bool on);
    bool isEnabled() const { return m_enabled; }
    QString filePath() const { return m_path; }

    /* Process start is the reference for the first paint. */
    void markProcessStart();
    void markFirstPaint(bool coldStart);

    /* Navigation timing: start on URL change, first frame after load
     * completes counts as the first contentful paint we can observe. */
    void navigationStarted(int tabId, const QUrl& url);
    void loadFinished(int tabId);
    void frameReady(int tabId, int openTabs);

    /* Interaction frame statistics; the view calls these around a touch
     * sequence. */
    void interactionBegin(const QString& kind, const QUrl& url);
    void interactionFrame(qreal frameMs, qreal budgetMs);
    void interactionEnd();

    void sampleRss(int openTabs);

    static qint64 residentSetMb();
    /* Corpus id for a URL (tools/corpus/pages.json) or the host. */
    static QString pageIdFor(const QUrl& url);

private:
    void write(const QJsonObject& record);

    QString m_path;
    QFile m_file;
    bool m_enabled = false;
    QElapsedTimer m_processTimer;
    bool m_firstPaintLogged = false;
    struct Nav { QElapsedTimer timer; QUrl url; bool loaded = false; };
    QHash<int, Nav> m_navs;
    struct Interaction { QString kind; QUrl url; QElapsedTimer timer; int frames = 0; int dropped = 0; bool active = false; };
    Interaction m_interaction;
};

} // namespace Tuuli

#endif
