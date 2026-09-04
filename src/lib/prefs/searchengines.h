/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_SEARCHENGINES_H
#define TUULI_SEARCHENGINES_H

/*
 * Built-in search engines (spec 9.4: non-tracking default, user-changeable,
 * no revenue arrangement of any kind) and the URL-or-search resolver used by
 * the address field.
 */

#include <QString>
#include <QUrl>
#include <QVector>

namespace Tuuli {

struct SearchEngine {
    QString id;
    QString name;
    QString searchUrl;   // contains {searchTerms}
    QString homeUrl;
};

class SearchEngines
{
public:
    static const QVector<SearchEngine>& builtin();
    static QString defaultId();
    static const SearchEngine* byId(const QString& id);
    static QUrl searchUrl(const QString& engineId, const QString& terms);

    /* Turns address-field input into something to load: a URL when it looks
     * like one, a search otherwise.  Empty input yields an empty URL. */
    static QUrl resolve(const QString& input, const QString& engineId);
    static bool looksLikeUrl(const QString& input);
};

} // namespace Tuuli

#endif
