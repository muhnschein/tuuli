/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "cosmeticfilter.h"

#include <QFile>
#include <QTextStream>
#include <QUrl>
#include <algorithm>

namespace Tuuli {

CosmeticFilter::CosmeticFilter()
{
}

void CosmeticFilter::clear()
{
    m_generic.clear();
    m_domain.clear();
    m_genericExcludes.clear();
    m_stats = Stats();
}

bool CosmeticFilter::loadFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QTextStream in(&f);
    in.setCodec("UTF-8");
    addRules(in.readAll());
    return true;
}

void CosmeticFilter::addRules(const QString& listText)
{
    const QStringList lines = listText.split(QLatin1Char('\n'));
    for (const QString& raw : lines)
        parseLine(raw.trimmed());
}

static bool isExtendedSelector(const QString& sel)
{
    // Extended CSS that Servo's user stylesheets cannot evaluate.
    static const char* const bad[] = {
        ":has(", ":has-text(", ":contains(", ":matches-css", ":xpath(", ":-abp-", ":upward(",
        ":remove(", ":style(", ":if(", ":if-not(", ":nth-ancestor(", ":min-text-length(",
        ":watch-attr(", ":matches-path(", ":others(", ":matches-attr(", ":remove-attr(",
        ":remove-class(", ":matches-prop("
    };
    for (const char* b : bad)
        if (sel.contains(QLatin1String(b)))
            return true;
    return false;
}

void CosmeticFilter::parseLine(const QString& line)
{
    if (line.isEmpty() || line.startsWith(QLatin1Char('!')) || line.startsWith(QLatin1Char('['))) {
        return;
    }

    bool exception = false;
    int sep = line.indexOf(QLatin1String("#@#"));
    int sepLen = 3;
    if (sep >= 0) {
        exception = true;
    } else {
        sep = line.indexOf(QLatin1String("##"));
        sepLen = 2;
    }
    if (sep < 0) {
        ++m_stats.ignored; // network rule or something else
        return;
    }
    // Scriptlet / extended syntax: #?# #$# #%# #$?#
    if (sep + sepLen < line.size()) {
        const QChar after = line.at(sep + 1);
        if (!exception && (after == QLatin1Char('?') || after == QLatin1Char('$') || after == QLatin1Char('%'))) {
            ++m_stats.ignored;
            return;
        }
    }

    const QString domains = line.left(sep);
    const QString selector = line.mid(sep + sepLen).trimmed();
    if (selector.isEmpty() || isExtendedSelector(selector)) {
        ++m_stats.ignored;
        return;
    }

    if (domains.isEmpty()) {
        if (exception) {
            // "#@#sel" with no domain: remove a generic rule globally.
            m_generic.remove(selector);
            ++m_stats.exceptions;
        } else {
            m_generic.insert(selector);
            ++m_stats.genericRules;
        }
        return;
    }

    QStringList includes, excludes;
    const QStringList parts = domains.split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString& p : parts) {
        const QString d = p.trimmed().toLower();
        if (d.startsWith(QLatin1Char('~')))
            excludes << d.mid(1);
        else if (!d.isEmpty())
            includes << d;
    }

    if (includes.isEmpty() && !excludes.isEmpty() && !exception) {
        // "~a.com##sel": generic everywhere except a.com
        m_generic.insert(selector);
        for (const QString& d : excludes)
            m_genericExcludes[selector].insert(d);
        ++m_stats.genericRules;
        return;
    }

    for (const QString& d : includes) {
        DomainRule r;
        r.selector = selector;
        r.exception = exception;
        m_domain[d].append(r);
        if (exception)
            ++m_stats.exceptions;
        else
            ++m_stats.domainRules;
    }
    // An excluded domain on a domain-specific rule acts as an exception there.
    for (const QString& d : excludes) {
        DomainRule r;
        r.selector = selector;
        r.exception = true;
        m_domain[d].append(r);
    }
}

bool CosmeticFilter::hostMatchesDomain(const QString& host, const QString& domain)
{
    if (host.isEmpty() || domain.isEmpty())
        return false;
    if (host == domain)
        return true;
    return host.endsWith(QLatin1Char('.') + domain);
}

QString CosmeticFilter::hostOf(const QString& url)
{
    return QUrl(url).host().toLower();
}

QStringList CosmeticFilter::selectorsFor(const QString& rawHost) const
{
    const QString host = rawHost.toLower();
    QSet<QString> result;
    QSet<QString> exceptions;

    // Domain rules: walk host and every parent domain.
    QString probe = host;
    while (!probe.isEmpty()) {
        auto it = m_domain.constFind(probe);
        if (it != m_domain.constEnd()) {
            for (const DomainRule& r : it.value()) {
                if (r.exception)
                    exceptions.insert(r.selector);
                else
                    result.insert(r.selector);
            }
        }
        const int dot = probe.indexOf(QLatin1Char('.'));
        if (dot < 0)
            break;
        probe = probe.mid(dot + 1);
    }

    for (const QString& sel : m_generic) {
        auto ex = m_genericExcludes.constFind(sel);
        if (ex != m_genericExcludes.constEnd()) {
            bool excluded = false;
            for (const QString& d : ex.value())
                if (hostMatchesDomain(host, d)) { excluded = true; break; }
            if (excluded)
                continue;
        }
        result.insert(sel);
    }

    for (const QString& sel : exceptions)
        result.remove(sel);

    QStringList out = result.toList();
    std::sort(out.begin(), out.end());
    return out;
}

QString CosmeticFilter::stylesheetFor(const QString& host, int selectorsPerRule) const
{
    const QStringList selectors = selectorsFor(host);
    if (selectors.isEmpty())
        return QString();
    const int group = qMax(1, selectorsPerRule);
    QString css;
    css.reserve(selectors.size() * 32);
    for (int i = 0; i < selectors.size(); i += group) {
        const QStringList chunk = selectors.mid(i, group);
        css += chunk.join(QLatin1String(",\n"));
        css += QLatin1String(" { display: none !important; }\n");
    }
    return css;
}

} // namespace Tuuli
