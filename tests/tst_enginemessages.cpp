// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "engine/EngineMessages.h"

#include <QtTest>

using Tuuli::EngineMessages;

class tst_enginemessages : public QObject
{
    Q_OBJECT

private slots:
    void constants();
    void defaultFavicon_data();
    void defaultFavicon();
    void resolveFavicon_data();
    void resolveFavicon();
};

void tst_enginemessages::constants()
{
    EngineMessages messages;
    QCOMPARE(messages.clearPrivateDataTopic(), QStringLiteral("clear-private-data"));
    QCOMPARE(messages.cookiesAndSiteDataPayload(), QStringLiteral("cookies-and-site-data"));
    QCOMPARE(messages.cachePayload(), QStringLiteral("cache"));
    QVERIFY(messages.faviconScript().contains(QStringLiteral("icon")));
    QVERIFY(messages.faviconScript().startsWith(QStringLiteral("(function")));
}

void tst_enginemessages::defaultFavicon_data()
{
    QTest::addColumn<QString>("page");
    QTest::addColumn<QString>("expected");
    QTest::newRow("https") << QStringLiteral("https://example.org/deep/path?x=1")
                           << QStringLiteral("https://example.org/favicon.ico");
    QTest::newRow("http port") << QStringLiteral("http://example.org:8080/")
                               << QStringLiteral("http://example.org:8080/favicon.ico");
    QTest::newRow("about") << QStringLiteral("about:blank") << QString();
    QTest::newRow("file") << QStringLiteral("file:///tmp/x.html") << QString();
    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("no host") << QStringLiteral("https:///nohost") << QString();
}

void tst_enginemessages::defaultFavicon()
{
    QFETCH(QString, page);
    QFETCH(QString, expected);
    EngineMessages messages;
    QCOMPARE(messages.defaultFavicon(page), expected);
}

void tst_enginemessages::resolveFavicon_data()
{
    QTest::addColumn<QString>("page");
    QTest::addColumn<QString>("href");
    QTest::addColumn<QString>("expected");
    const QString page = QStringLiteral("https://example.org/news/today");
    QTest::newRow("relative") << page << QStringLiteral("icons/site.png")
                              << QStringLiteral("https://example.org/news/icons/site.png");
    QTest::newRow("root relative")
        << page << QStringLiteral("/i.png") << QStringLiteral("https://example.org/i.png");
    QTest::newRow("absolute") << page << QStringLiteral("https://cdn.example.net/i.ico")
                              << QStringLiteral("https://cdn.example.net/i.ico");
    QTest::newRow("data") << page << QStringLiteral("data:image/png;base64,AAAA")
                          << QStringLiteral("data:image/png;base64,AAAA");
    QTest::newRow("empty") << page << QString()
                           << QStringLiteral("https://example.org/favicon.ico");
    QTest::newRow("spaces") << page << QStringLiteral("   ")
                            << QStringLiteral("https://example.org/favicon.ico");
    QTest::newRow("javascript") << page << QStringLiteral("javascript:alert(1)")
                                << QStringLiteral("https://example.org/favicon.ico");
    QTest::newRow("no page") << QString() << QStringLiteral("/i.png") << QString();
}

void tst_enginemessages::resolveFavicon()
{
    QFETCH(QString, page);
    QFETCH(QString, href);
    QFETCH(QString, expected);
    EngineMessages messages;
    QCOMPARE(messages.resolveFavicon(page, href), expected);
}

QTEST_GUILESS_MAIN(tst_enginemessages)
#include "tst_enginemessages.moc"
