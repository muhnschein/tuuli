/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_COSMETICFILTER_H
#define TUULI_COSMETICFILTER_H

/*
 * Cosmetic (element-hiding) filtering from an EasyList-derived rule set,
 * applied as a per-webview user stylesheet (spec 9.3, M3).
 *
 * This is deliberately NOT called ad blocking anywhere in the UI: network
 * requests are not intercepted.  Only these rule forms are understood:
 *
 *   ##selector                  generic hide
 *   example.com##selector       hide on example.com and its subdomains
 *   a.com,b.com##selector       several domains
 *   ~a.com##selector            generic hide except on a.com
 *   example.com#@#selector      exception: do not hide selector there
 *
 * Network rules (||, |, $ options), comments (!), headers ([Adblock...]) and
 * scriptlet / extended-CSS rules (#?#, #$#, #%#, :has(...) etc.) are ignored.
 */

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Tuuli {

class CosmeticFilter
{
public:
    struct Stats {
        int genericRules = 0;
        int domainRules = 0;
        int exceptions = 0;
        int ignored = 0;
    };

    CosmeticFilter();

    /* Parses rules; may be called several times to merge lists. */
    void addRules(const QString& listText);
    bool loadFile(const QString& path);
    void clear();

    Stats stats() const { return m_stats; }
    bool isEmpty() const { return m_generic.isEmpty() && m_domain.isEmpty(); }

    /* Selectors that apply to `host`, generic minus exceptions plus
     * domain-specific ones.  Sorted for determinism. */
    QStringList selectorsFor(const QString& host) const;

    /* The stylesheet the engine gets for `host`.  Empty when nothing applies.
     * Selectors are grouped `selectorsPerRule` at a time so a single bad
     * selector only invalidates a small group. */
    QString stylesheetFor(const QString& host, int selectorsPerRule = 50) const;

    static bool hostMatchesDomain(const QString& host, const QString& domain);
    static QString hostOf(const QString& url);

private:
    struct DomainRule {
        QString selector;
        bool exception = false;
    };
    void parseLine(const QString& line);

    QSet<QString> m_generic;
    // domain -> rules (both include and exception)
    QHash<QString, QVector<DomainRule>> m_domain;
    // selector -> domains where the generic rule does NOT apply (~domain)
    QHash<QString, QSet<QString>> m_genericExcludes;
    Stats m_stats;
};

} // namespace Tuuli

#endif
