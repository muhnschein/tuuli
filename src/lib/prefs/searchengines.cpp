/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "searchengines.h"

#include <QHostAddress>
#include <QRegularExpression>

namespace Tuuli {

const QVector<SearchEngine>& SearchEngines::builtin()
{
    static const QVector<SearchEngine> engines = {
        { QStringLiteral("duckduckgo"), QStringLiteral("DuckDuckGo"),
          QStringLiteral("https://duckduckgo.com/?q={searchTerms}"), QStringLiteral("https://duckduckgo.com/") },
        { QStringLiteral("startpage"), QStringLiteral("Startpage"),
          QStringLiteral("https://www.startpage.com/do/search?q={searchTerms}"), QStringLiteral("https://www.startpage.com/") },
        { QStringLiteral("qwant"), QStringLiteral("Qwant"),
          QStringLiteral("https://www.qwant.com/?q={searchTerms}"), QStringLiteral("https://www.qwant.com/") },
        { QStringLiteral("mojeek"), QStringLiteral("Mojeek"),
          QStringLiteral("https://www.mojeek.com/search?q={searchTerms}"), QStringLiteral("https://www.mojeek.com/") },
        { QStringLiteral("brave"), QStringLiteral("Brave Search"),
          QStringLiteral("https://search.brave.com/search?q={searchTerms}"), QStringLiteral("https://search.brave.com/") },
        { QStringLiteral("wikipedia"), QStringLiteral("Wikipedia"),
          QStringLiteral("https://en.wikipedia.org/w/index.php?search={searchTerms}"), QStringLiteral("https://en.wikipedia.org/") },
    };
    return engines;
}

QString SearchEngines::defaultId()
{
    return QStringLiteral("duckduckgo");
}

const SearchEngine* SearchEngines::byId(const QString& id)
{
    for (const SearchEngine& e : builtin())
        if (e.id == id)
            return &e;
    return nullptr;
}

QUrl SearchEngines::searchUrl(const QString& engineId, const QString& terms)
{
    const SearchEngine* e = byId(engineId);
    if (!e)
        e = byId(defaultId());
    QString url = e->searchUrl;
    url.replace(QStringLiteral("{searchTerms}"),
                QString::fromLatin1(QUrl::toPercentEncoding(terms)));
    return QUrl(url);
}

bool SearchEngines::looksLikeUrl(const QString& raw)
{
    const QString input = raw.trimmed();
    if (input.isEmpty())
        return false;

    // An explicit scheme wins, spaces or not ("https://x.org/a b").
    static const QRegularExpression withAuthority(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
    if (withAuthority.match(input).hasMatch())
        return true;
    static const QRegularExpression knownOpaque(QStringLiteral("^(about|file|data|blob):"), QRegularExpression::CaseInsensitiveOption);
    if (knownOpaque.match(input).hasMatch())
        return true;
    // Anything else with a colon prefix ("what:ever", "javascript:...") is a
    // search, except host:port.
    static const QRegularExpression hostPort(QStringLiteral("^[a-zA-Z0-9.-]+:[0-9]{1,5}(/.*)?$"));
    if (hostPort.match(input).hasMatch())
        return true;
    static const QRegularExpression otherScheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*:"));
    if (otherScheme.match(input).hasMatch())
        return false;

    if (input.contains(QLatin1Char(' ')))
        return false;

    QString host = input;
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0)
        host = host.left(slash);

    if (host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0)
        return true;
    if (!QHostAddress(host).isNull())
        return true;

    static const QRegularExpression domain(
        QStringLiteral("^([a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?\\.)+[a-zA-Z]{2,63}\\.?$"));
    return domain.match(host).hasMatch();
}

QUrl SearchEngines::resolve(const QString& raw, const QString& engineId)
{
    const QString input = raw.trimmed();
    if (input.isEmpty())
        return QUrl();
    if (looksLikeUrl(input)) {
        QUrl url = QUrl::fromUserInput(input);
        if (url.isValid())
            return url;
    }
    return searchUrl(engineId, input);
}

} // namespace Tuuli
