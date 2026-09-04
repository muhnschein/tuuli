/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "perflog.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

namespace Tuuli {

PerfLog::PerfLog(const QString& filePath, QObject* parent)
    : QObject(parent), m_path(filePath)
{
    m_processTimer.start();
}

PerfLog::~PerfLog()
{
    if (m_file.isOpen())
        m_file.close();
}

void PerfLog::setEnabled(bool on)
{
    if (m_enabled == on)
        return;
    m_enabled = on;
    if (on) {
        QDir().mkpath(QFileInfo(m_path).absolutePath());
        m_file.setFileName(m_path);
        m_file.open(QIODevice::Append | QIODevice::Text);
    } else if (m_file.isOpen()) {
        m_file.close();
    }
}

void PerfLog::write(const QJsonObject& record)
{
    if (!m_enabled || !m_file.isOpen())
        return;
    QJsonObject r = record;
    r.insert(QStringLiteral("t_ms"), m_processTimer.elapsed());
    m_file.write(QJsonDocument(r).toJson(QJsonDocument::Compact));
    m_file.write("\n");
    m_file.flush();
}

void PerfLog::markProcessStart()
{
    m_processTimer.restart();
    m_firstPaintLogged = false;
}

void PerfLog::markFirstPaint(bool coldStart)
{
    if (m_firstPaintLogged)
        return;
    m_firstPaintLogged = true;
    QJsonObject r;
    r.insert(QStringLiteral("kind"), QStringLiteral("start"));
    r.insert(QStringLiteral("cold"), coldStart);
    r.insert(QStringLiteral("first_paint_ms"), m_processTimer.elapsed());
    write(r);
}

void PerfLog::navigationStarted(int tabId, const QUrl& url)
{
    if (!m_enabled)
        return;
    Nav n;
    n.timer.start();
    n.url = url;
    m_navs.insert(tabId, n);
}

void PerfLog::loadFinished(int tabId)
{
    auto it = m_navs.find(tabId);
    if (it != m_navs.end())
        it->loaded = true;
}

void PerfLog::frameReady(int tabId, int openTabs)
{
    auto it = m_navs.find(tabId);
    if (it == m_navs.end() || !it->loaded)
        return;
    QJsonObject r;
    r.insert(QStringLiteral("kind"), QStringLiteral("load"));
    r.insert(QStringLiteral("page"), pageIdFor(it->url));
    r.insert(QStringLiteral("url"), it->url.toString());
    r.insert(QStringLiteral("fcp_ms"), it->timer.elapsed());
    r.insert(QStringLiteral("rss_mb"), residentSetMb());
    r.insert(QStringLiteral("tabs"), openTabs);
    write(r);
    m_navs.erase(it);
}

void PerfLog::interactionBegin(const QString& kind, const QUrl& url)
{
    if (!m_enabled)
        return;
    m_interaction = Interaction();
    m_interaction.kind = kind;
    m_interaction.url = url;
    m_interaction.timer.start();
    m_interaction.active = true;
}

void PerfLog::interactionFrame(qreal frameMs, qreal budgetMs)
{
    if (!m_interaction.active)
        return;
    ++m_interaction.frames;
    if (budgetMs > 0 && frameMs > budgetMs * 1.5)
        m_interaction.dropped += int(frameMs / budgetMs) - 1;
}

void PerfLog::interactionEnd()
{
    if (!m_interaction.active)
        return;
    m_interaction.active = false;
    if (m_interaction.frames < 5)
        return;
    QJsonObject r;
    r.insert(QStringLiteral("kind"), QStringLiteral("frames"));
    r.insert(QStringLiteral("page"), pageIdFor(m_interaction.url));
    r.insert(QStringLiteral("interaction"), m_interaction.kind);
    r.insert(QStringLiteral("frames"), m_interaction.frames);
    r.insert(QStringLiteral("dropped"), m_interaction.dropped);
    r.insert(QStringLiteral("duration_ms"), m_interaction.timer.elapsed());
    write(r);
}

void PerfLog::sampleRss(int openTabs)
{
    QJsonObject r;
    r.insert(QStringLiteral("kind"), QStringLiteral("rss"));
    r.insert(QStringLiteral("tabs"), openTabs);
    r.insert(QStringLiteral("rss_mb"), residentSetMb());
    write(r);
}

qint64 PerfLog::residentSetMb()
{
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> fields = statm.readAll().split(' ');
    if (fields.size() < 2)
        return -1;
    const qint64 pages = fields.at(1).toLongLong();
    return pages * 4096 / (1024 * 1024);
}

QString PerfLog::pageIdFor(const QUrl& url)
{
    // Mirrors tools/corpus/pages.json; anything else is reported by host.
    static const struct { const char* host; const char* id; } corpus[] = {
        { "www.theguardian.com", "news-article" },
        { "app.tuta.com", "webmail" },
        { "forum.sailfishos.org", "forum-thread" },
        { "book.servo.org", "docs-site" },
        { "en.wikipedia.org", "wiki" },
        { "duckduckgo.com", "search-results" },
        { "github.com", "github-file" },
        { "fosstodon.org", "mastodon" },
        { "shop.jolla.com", "webshop" },
        { "excalidraw.com", "heavy-spa" },
    };
    const QString host = url.host().toLower();
    for (const auto& c : corpus)
        if (host == QLatin1String(c.host))
            return QLatin1String(c.id);
    return host;
}

} // namespace Tuuli
