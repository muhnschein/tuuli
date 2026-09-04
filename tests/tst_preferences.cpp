/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "prefs/preferences.h"
#include "prefs/servoprefs.h"

#include <QtTest>

using namespace Tuuli;

class tst_Preferences : public QObject
{
    Q_OBJECT
private slots:
    void privacyDefaultsMatchSpec()
    {
        QTemporaryDir dir;
        Preferences p(dir.path() + QStringLiteral("/tuuli.conf"));
        // Spec 9.4
        QVERIFY(p.blockThirdPartyCookies());
        QVERIFY(p.sendDoNotTrack());
        QVERIFY(p.sendGlobalPrivacyControl());
        QCOMPARE(p.referrerPolicy(), QStringLiteral("strict-origin-when-cross-origin"));
        QCOMPARE(p.searchEngine(), QStringLiteral("duckduckgo"));
        QVERIFY(p.restoreSession());
        QVERIFY(p.javascriptEnabled());
        QCOMPARE(p.devicePixelRatioOverride(), 0.0);
        QVERIFY(!p.basicRenderLoop());
        QCOMPARE(p.maxLiveWebViews(), 8);
    }

    void enginePrefLinesReflectSettings()
    {
        QTemporaryDir dir;
        Preferences p(dir.path() + QStringLiteral("/tuuli.conf"));
        QStringList lines = p.enginePrefs();
        QVERIFY(lines.contains(QString::fromLatin1(ServoPref::NetworkBlockThirdPartyCookies) + QStringLiteral("=true")));
        QVERIFY(lines.contains(QString::fromLatin1(ServoPref::NetworkSendDnt) + QStringLiteral("=true")));
        QVERIFY(lines.contains(QString::fromLatin1(ServoPref::NetworkReferrerPolicy) + QStringLiteral("=strict-origin-when-cross-origin")));
        p.setBlockThirdPartyCookies(false);
        p.setJavascriptEnabled(false);
        lines = p.enginePrefs();
        QVERIFY(lines.contains(QString::fromLatin1(ServoPref::NetworkBlockThirdPartyCookies) + QStringLiteral("=false")));
        QVERIFY(lines.contains(QString::fromLatin1(ServoPref::JsEnabled) + QStringLiteral("=false")));
    }

    void changesEmitOnceAndPersist()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/tuuli.conf");
        {
            Preferences p(path);
            QSignalSpy privacy(&p, &Preferences::privacyChanged);
            QSignalSpy search(&p, &Preferences::searchEngineChanged);
            p.setSendDoNotTrack(false);
            p.setSendDoNotTrack(false);
            QCOMPARE(privacy.size(), 1);
            p.setSearchEngine(QStringLiteral("qwant"));
            QCOMPARE(search.size(), 1);
            p.setMaxLiveWebViews(0);
            QCOMPARE(p.maxLiveWebViews(), 1);
            p.sync();
        }
        Preferences again(path);
        QVERIFY(!again.sendDoNotTrack());
        QCOMPARE(again.searchEngine(), QStringLiteral("qwant"));
        again.setSearchEngine(QStringLiteral("does-not-exist"));
        QCOMPARE(again.searchEngine(), QStringLiteral("duckduckgo"));
    }
};

QTEST_GUILESS_MAIN(tst_Preferences)
#include "tst_preferences.moc"
