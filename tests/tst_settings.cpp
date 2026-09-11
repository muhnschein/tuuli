// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "settings/Settings.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::Settings;

class tst_settings : public QObject
{
    Q_OBJECT

private slots:
    void defaults();
    void persistsValues();
    void searchEngineSelection();
    void searchUrl();
    void urlForInput_data();
    void urlForInput();
};

void tst_settings::defaults()
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/tuuli.conf"));
    QCOMPARE(settings.homePage(), Settings::defaultHomePage());
    QCOMPARE(settings.searchEngine(), Settings::defaultSearchEngine());
    QCOMPARE(settings.searchEngineIndex(), 0);
    QVERIFY(!settings.desktopMode());
    QCOMPARE(settings.searchEngineNames().count(), settings.searchEngineKeys().count());
    QVERIFY(settings.searchEngineNames().contains(QStringLiteral("DuckDuckGo")));
    QVERIFY(settings.searchEngineKeys().contains(QStringLiteral("wikipedia")));
}

void tst_settings::persistsValues()
{
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/tuuli.conf");
    {
        Settings settings(path);
        QSignalSpy homeSpy(&settings, &Settings::homePageChanged);
        QSignalSpy desktopSpy(&settings, &Settings::desktopModeChanged);

        settings.setHomePage(QStringLiteral("  https://sailfishos.org/  "));
        settings.setHomePage(QStringLiteral("https://sailfishos.org/"));
        QCOMPARE(homeSpy.count(), 1);
        QCOMPARE(settings.homePage(), QStringLiteral("https://sailfishos.org/"));

        settings.setDesktopMode(true);
        settings.setDesktopMode(true);
        QCOMPARE(desktopSpy.count(), 1);
        settings.setSearchEngine(QStringLiteral("bing"));
    }
    Settings reloaded(path);
    QCOMPARE(reloaded.homePage(), QStringLiteral("https://sailfishos.org/"));
    QVERIFY(reloaded.desktopMode());
    QCOMPARE(reloaded.searchEngine(), QStringLiteral("bing"));

    reloaded.setHomePage(QStringLiteral("   "));
    QCOMPARE(reloaded.homePage(), Settings::defaultHomePage());
}

void tst_settings::searchEngineSelection()
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/tuuli.conf"));
    QSignalSpy spy(&settings, &Settings::searchEngineChanged);

    settings.setSearchEngine(QStringLiteral("nonsense"));
    QCOMPARE(spy.count(), 0);
    settings.setSearchEngineIndex(-1);
    settings.setSearchEngineIndex(99);
    QCOMPARE(spy.count(), 0);

    settings.setSearchEngineIndex(2);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(settings.searchEngine(), QStringLiteral("bing"));
    QCOMPARE(settings.searchEngineIndex(), 2);
    settings.setSearchEngine(QStringLiteral("bing"));
    QCOMPARE(spy.count(), 1);
}

void tst_settings::searchUrl()
{
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/tuuli.conf"));
    QCOMPARE(settings.searchUrl(QStringLiteral("sailfish os")),
             QStringLiteral("https://duckduckgo.com/?q=sailfish%20os"));
    settings.setSearchEngine(QStringLiteral("google"));
    QCOMPARE(settings.searchUrl(QStringLiteral(" a&b ")),
             QStringLiteral("https://www.google.com/search?q=a%26b"));
}

void tst_settings::urlForInput_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("blank") << QStringLiteral("   ") << QString();
    QTest::newRow("https") << QStringLiteral("https://example.org/a?b=c#d")
                           << QStringLiteral("https://example.org/a?b=c#d");
    QTest::newRow("http") << QStringLiteral("http://example.org")
                          << QStringLiteral("http://example.org");
    QTest::newRow("about") << QStringLiteral("about:blank") << QStringLiteral("about:blank");
    QTest::newRow("host") << QStringLiteral("example.org") << QStringLiteral("https://example.org");
    QTest::newRow("host path") << QStringLiteral("example.org/path?q=1")
                               << QStringLiteral("https://example.org/path?q=1");
    QTest::newRow("host port") << QStringLiteral("example.org:8443")
                               << QStringLiteral("https://example.org:8443");
    QTest::newRow("localhost") << QStringLiteral("localhost:8080")
                               << QStringLiteral("http://localhost:8080");
    QTest::newRow("ipv4") << QStringLiteral("192.168.1.1/admin")
                          << QStringLiteral("http://192.168.1.1/admin");
    QTest::newRow("trimmed") << QStringLiteral("  example.org  ")
                             << QStringLiteral("https://example.org");
    QTest::newRow("word") << QStringLiteral("sailfish")
                          << QStringLiteral("https://duckduckgo.com/?q=sailfish");
    QTest::newRow("words") << QStringLiteral("jolla phone 2026")
                           << QStringLiteral("https://duckduckgo.com/?q=jolla%20phone%202026");
    QTest::newRow("dotted words") << QStringLiteral("what is example.org")
                                  << QStringLiteral(
                                         "https://duckduckgo.com/?q=what%20is%20example.org");
    QTest::newRow("question") << QStringLiteral("how? really")
                              << QStringLiteral("https://duckduckgo.com/?q=how%3F%20really");
}

void tst_settings::urlForInput()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QTemporaryDir dir;
    Settings settings(dir.path() + QStringLiteral("/tuuli.conf"));
    QCOMPARE(settings.urlForInput(input), expected);
}

QTEST_GUILESS_MAIN(tst_settings)
#include "tst_settings.moc"
