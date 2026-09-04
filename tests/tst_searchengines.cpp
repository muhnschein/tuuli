/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "prefs/searchengines.h"

#include <QtTest>

using namespace Tuuli;

class tst_SearchEngines : public QObject
{
    Q_OBJECT
private slots:
    void defaultIsNonTracking()
    {
        QCOMPARE(SearchEngines::defaultId(), QStringLiteral("duckduckgo"));
        QVERIFY(SearchEngines::byId(SearchEngines::defaultId()));
        QVERIFY(!SearchEngines::byId(QStringLiteral("google")));
        QVERIFY(SearchEngines::builtin().size() >= 3);
    }

    void searchUrlEncodesTerms()
    {
        const QUrl u = SearchEngines::searchUrl(QStringLiteral("duckduckgo"), QStringLiteral("sailfish os & servo"));
        QCOMPARE(u.host(), QStringLiteral("duckduckgo.com"));
        QVERIFY(QString::fromLatin1(u.toEncoded()).contains(QStringLiteral("q=sailfish%20os%20%26%20servo")));
        // Unknown engine falls back to the default.
        QCOMPARE(SearchEngines::searchUrl(QStringLiteral("nope"), QStringLiteral("x")).host(), QStringLiteral("duckduckgo.com"));
    }

    void looksLikeUrl_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("expected");
        QTest::newRow("scheme") << "https://jolla.com" << true;
        QTest::newRow("bare domain") << "jolla.com" << true;
        QTest::newRow("domain with path") << "jolla.com/phone" << true;
        QTest::newRow("subdomain") << "docs.servo.org" << true;
        QTest::newRow("localhost") << "localhost" << true;
        QTest::newRow("localhost port") << "localhost:8080/x" << true;
        QTest::newRow("ipv4") << "192.168.1.1" << true;
        QTest::newRow("ipv4 port") << "192.168.1.1:3000" << true;
        QTest::newRow("about") << "about:blank" << true;
        QTest::newRow("words") << "jolla phone review" << false;
        QTest::newRow("single word") << "servo" << false;
        QTest::newRow("question") << "what is 2+2?" << false;
        QTest::newRow("dot but spaces") << "jolla.com is nice" << false;
        QTest::newRow("scheme with space") << "https://x.org/a b" << true;
        QTest::newRow("unknown scheme-ish") << "what:ever" << false;
        QTest::newRow("javascript") << "javascript:alert(1)" << false;
        QTest::newRow("file") << "file:///home/user/x.html" << true;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("trailing dot") << "example.org." << true;
        QTest::newRow("numeric tld") << "foo.123" << false;
    }

    void looksLikeUrl()
    {
        QFETCH(QString, input);
        QFETCH(bool, expected);
        QCOMPARE(SearchEngines::looksLikeUrl(input), expected);
    }

    void resolveAddsSchemeOrSearches()
    {
        QCOMPARE(SearchEngines::resolve(QStringLiteral("jolla.com"), QStringLiteral("duckduckgo")),
                 QUrl(QStringLiteral("http://jolla.com")));
        QCOMPARE(SearchEngines::resolve(QStringLiteral("  https://x.org/a b  "), QStringLiteral("duckduckgo")).host(),
                 QStringLiteral("x.org"));
        const QUrl s = SearchEngines::resolve(QStringLiteral("jolla phone"), QStringLiteral("qwant"));
        QCOMPARE(s.host(), QStringLiteral("www.qwant.com"));
        QVERIFY(SearchEngines::resolve(QString(), QStringLiteral("duckduckgo")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(tst_SearchEngines)
#include "tst_searchengines.moc"
