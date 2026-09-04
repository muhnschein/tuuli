/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "platform/connmanproxy.h"

#include <QtTest>

using namespace Tuuli;

class tst_ConnmanProxy : public QObject
{
    Q_OBJECT
private slots:
    void directWhenMethodIsDirectOrMissing()
    {
        QVariantMap m;
        m.insert(QStringLiteral("Method"), QStringLiteral("direct"));
        QVERIFY(ConnmanProxy::fromProxyProperties(m).isDirect());
        QVERIFY(ConnmanProxy::fromProxyProperties(QVariantMap()).isDirect());
    }

    void manualProxyWithExcludes()
    {
        QVariantMap m;
        m.insert(QStringLiteral("Method"), QStringLiteral("manual"));
        m.insert(QStringLiteral("Servers"), QStringList() << QStringLiteral("http://proxy.corp:3128/"));
        m.insert(QStringLiteral("Excludes"), QStringList() << QStringLiteral("localhost") << QStringLiteral("*.corp"));
        const ProxyConfig c = ConnmanProxy::fromProxyProperties(m);
        QVERIFY(!c.isDirect());
        QCOMPARE(c.http, QStringLiteral("proxy.corp:3128"));
        QCOMPARE(c.https, QStringLiteral("proxy.corp:3128"));
        QCOMPARE(c.noProxy, QStringList() << QStringLiteral("localhost") << QStringLiteral("*.corp"));
        QVERIFY(c.pacUrl.isEmpty());
    }

    void separateHttpsServer()
    {
        QVariantMap m;
        m.insert(QStringLiteral("Method"), QStringLiteral("manual"));
        m.insert(QStringLiteral("Servers"), QStringList() << QStringLiteral("plain:8080") << QStringLiteral("https://secure:8443"));
        const ProxyConfig c = ConnmanProxy::fromProxyProperties(m);
        QCOMPARE(c.http, QStringLiteral("plain:8080"));
        QCOMPARE(c.https, QStringLiteral("secure:8443"));
    }

    void autoProxyGivesPacUrl()
    {
        QVariantMap m;
        m.insert(QStringLiteral("Method"), QStringLiteral("auto"));
        m.insert(QStringLiteral("URL"), QStringLiteral("http://wpad.corp/wpad.dat"));
        const ProxyConfig c = ConnmanProxy::fromProxyProperties(m);
        QVERIFY(!c.isDirect());
        QCOMPARE(c.pacUrl, QUrl(QStringLiteral("http://wpad.corp/wpad.dat")));
        QVERIFY(c.http.isEmpty());
    }

    void stripScheme()
    {
        QCOMPARE(ConnmanProxy::stripScheme(QStringLiteral("socks5://h:1/")), QStringLiteral("h:1"));
        QCOMPARE(ConnmanProxy::stripScheme(QStringLiteral(" h:1 ")), QStringLiteral("h:1"));
    }

    void startWithoutConnmanIsHarmless()
    {
        ConnmanProxy p;
        p.start();
        p.refresh();
        QVERIFY(p.current().isDirect());
    }
};

QTEST_GUILESS_MAIN(tst_ConnmanProxy)
#include "tst_connmanproxy.moc"
