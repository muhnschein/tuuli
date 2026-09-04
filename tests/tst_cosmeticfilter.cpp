/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "blocking/cosmeticfilter.h"

#include <QtTest>

using namespace Tuuli;

static const char* kList =
    "[Adblock Plus 2.0]\n"
    "! Title: test list\n"
    "||ads.example^$third-party\n"
    "##.ad-banner\n"
    "##div[id^=\"google_ads\"]\n"
    "example.com##.sidebar-promo\n"
    "example.com,other.net###promo\n"
    "example.com#@#.ad-banner\n"
    "~news.example.org##.newsletter\n"
    "example.com#?#.foo:has(> .bar)\n"
    "example.com##.x:has-text(buy)\n"
    "example.com#$#body { overflow: auto !important }\n"
    "\n";

class tst_CosmeticFilter : public QObject
{
    Q_OBJECT
private slots:
    void parsesSupportedRulesOnly()
    {
        CosmeticFilter f;
        f.addRules(QString::fromLatin1(kList));
        const CosmeticFilter::Stats s = f.stats();
        QCOMPARE(s.genericRules, 3);   // .ad-banner, div[id^=google_ads], .newsletter
        QCOMPARE(s.domainRules, 3);    // .sidebar-promo, #promo x2
        QCOMPARE(s.exceptions, 1);
        QCOMPARE(s.ignored, 4);        // network rule, #?#, :has-text, #$#
        QVERIFY(!f.isEmpty());
    }

    void genericAppliesEverywhere()
    {
        CosmeticFilter f;
        f.addRules(QString::fromLatin1(kList));
        const QStringList sel = f.selectorsFor(QStringLiteral("random.site"));
        QVERIFY(sel.contains(QStringLiteral(".ad-banner")));
        QVERIFY(sel.contains(QStringLiteral("div[id^=\"google_ads\"]")));
        QVERIFY(sel.contains(QStringLiteral(".newsletter")));
        QVERIFY(!sel.contains(QStringLiteral(".sidebar-promo")));
    }

    void domainRulesAndSubdomains()
    {
        CosmeticFilter f;
        f.addRules(QString::fromLatin1(kList));
        QVERIFY(f.selectorsFor(QStringLiteral("example.com")).contains(QStringLiteral(".sidebar-promo")));
        QVERIFY(f.selectorsFor(QStringLiteral("www.example.com")).contains(QStringLiteral(".sidebar-promo")));
        QVERIFY(f.selectorsFor(QStringLiteral("WWW.EXAMPLE.COM")).contains(QStringLiteral("#promo")));
        QVERIFY(f.selectorsFor(QStringLiteral("other.net")).contains(QStringLiteral("#promo")));
        QVERIFY(!f.selectorsFor(QStringLiteral("notexample.com")).contains(QStringLiteral(".sidebar-promo")));
    }

    void exceptionsRemoveGenericRules()
    {
        CosmeticFilter f;
        f.addRules(QString::fromLatin1(kList));
        QVERIFY(!f.selectorsFor(QStringLiteral("example.com")).contains(QStringLiteral(".ad-banner")));
        QVERIFY(!f.selectorsFor(QStringLiteral("sub.example.com")).contains(QStringLiteral(".ad-banner")));
        QVERIFY(f.selectorsFor(QStringLiteral("other.net")).contains(QStringLiteral(".ad-banner")));
    }

    void negatedDomainExcludesGeneric()
    {
        CosmeticFilter f;
        f.addRules(QString::fromLatin1(kList));
        QVERIFY(!f.selectorsFor(QStringLiteral("news.example.org")).contains(QStringLiteral(".newsletter")));
        QVERIFY(!f.selectorsFor(QStringLiteral("m.news.example.org")).contains(QStringLiteral(".newsletter")));
        QVERIFY(f.selectorsFor(QStringLiteral("example.org")).contains(QStringLiteral(".newsletter")));
    }

    void stylesheetIsGroupedAndDeterministic()
    {
        CosmeticFilter f;
        QString list;
        for (int i = 0; i < 120; ++i)
            list += QStringLiteral("##.r%1\n").arg(i);
        f.addRules(list);
        const QString css = f.stylesheetFor(QStringLiteral("a.example"), 50);
        QCOMPARE(css.count(QStringLiteral("display: none !important")), 3);
        QCOMPARE(css, f.stylesheetFor(QStringLiteral("a.example"), 50));
        QVERIFY(f.stylesheetFor(QStringLiteral("a.example"), 0).contains(QStringLiteral(".r0")));
        CosmeticFilter empty;
        QVERIFY(empty.stylesheetFor(QStringLiteral("a.example")).isEmpty());
    }

    void hostHelpers()
    {
        QVERIFY(CosmeticFilter::hostMatchesDomain(QStringLiteral("a.b.c"), QStringLiteral("b.c")));
        QVERIFY(CosmeticFilter::hostMatchesDomain(QStringLiteral("b.c"), QStringLiteral("b.c")));
        QVERIFY(!CosmeticFilter::hostMatchesDomain(QStringLiteral("ab.c"), QStringLiteral("b.c")));
        QVERIFY(!CosmeticFilter::hostMatchesDomain(QString(), QStringLiteral("b.c")));
        QCOMPARE(CosmeticFilter::hostOf(QStringLiteral("https://WWW.Example.com/x")), QStringLiteral("www.example.com"));
    }

    void loadFileAndClear()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/list.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kList);
        file.close();
        CosmeticFilter f;
        QVERIFY(f.loadFile(path));
        QVERIFY(!f.isEmpty());
        QVERIFY(!f.loadFile(dir.path() + QStringLiteral("/missing.txt")));
        f.clear();
        QVERIFY(f.isEmpty());
        QCOMPARE(f.stats().genericRules, 0);
    }
};

QTEST_GUILESS_MAIN(tst_CosmeticFilter)
#include "tst_cosmeticfilter.moc"
