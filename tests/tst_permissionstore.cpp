/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "engine/mockengine.h"
#include "model/permissionstore.h"
#include "model/tab.h"

#include <QtTest>

using namespace Tuuli;

class tst_PermissionStore : public QObject
{
    Q_OBJECT
private slots:
    void defaultIsAsk()
    {
        QTemporaryDir dir;
        PermissionStore s(dir.path() + QStringLiteral("/p.json"));
        QCOMPARE(s.decision(QStringLiteral("https://a.example"), PermissionKind::Geolocation), PermissionStore::Ask);
        QCOMPARE(s.count(), 0);
    }

    void normalizesOrigins()
    {
        QCOMPARE(PermissionStore::normalizeOrigin(QStringLiteral("HTTPS://A.Example/path?x")), QStringLiteral("https://a.example"));
        QCOMPARE(PermissionStore::normalizeOrigin(QStringLiteral("https://a.example:8443/")), QStringLiteral("https://a.example:8443"));
        QCOMPARE(PermissionStore::normalizeOrigin(QStringLiteral(" weird ")), QStringLiteral("weird"));
    }

    void storesAndPersists()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/p.json");
        {
            PermissionStore s(path);
            QSignalSpy changed(&s, &PermissionStore::changed);
            s.setDecision(QStringLiteral("https://a.example/x"), PermissionKind::Geolocation, PermissionStore::Allow);
            s.setDecision(QStringLiteral("https://a.example"), PermissionKind::Camera, PermissionStore::Deny);
            QCOMPARE(changed.size(), 2);
            QCOMPARE(s.count(), 2);
        }
        PermissionStore again(path);
        QCOMPARE(again.decision(QStringLiteral("https://a.example"), PermissionKind::Geolocation), PermissionStore::Allow);
        QCOMPARE(again.decision(QStringLiteral("https://a.example"), PermissionKind::Camera), PermissionStore::Deny);
        QCOMPARE(again.decisionFor(QStringLiteral("https://a.example"), static_cast<int>(PermissionKind::Microphone)), int(PermissionStore::Ask));
        const QVariantList entries = again.entries();
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.first().toMap().value(QStringLiteral("kindName")).toString(), QStringLiteral("geolocation"));
    }

    void askRemovesAndClearWorks()
    {
        QTemporaryDir dir;
        PermissionStore s(dir.path() + QStringLiteral("/p.json"));
        s.setDecision(QStringLiteral("https://a.example"), PermissionKind::Geolocation, PermissionStore::Allow);
        s.setDecision(QStringLiteral("https://b.example"), PermissionKind::Geolocation, PermissionStore::Allow);
        s.setDecision(QStringLiteral("https://a.example"), PermissionKind::Geolocation, PermissionStore::Ask);
        QCOMPARE(s.count(), 1);
        s.clearOrigin(QStringLiteral("https://b.example"));
        QCOMPARE(s.count(), 0);
        s.setDecision(QStringLiteral("https://c.example"), PermissionKind::Notifications, PermissionStore::Deny);
        s.clearAll();
        QCOMPARE(s.count(), 0);
    }

    void answersRequestsFromStore()
    {
        QTemporaryDir dir;
        PermissionStore s(dir.path() + QStringLiteral("/p.json"));
        s.setDecision(QStringLiteral("https://a.example"), PermissionKind::Geolocation, PermissionStore::Allow);
        s.setDecision(QStringLiteral("https://d.example"), PermissionKind::Geolocation, PermissionStore::Deny);

        MockEngine engine;
        engine.initializeForTests();
        Tab tab(1, false);
        MockWebView* wv = static_cast<MockWebView*>(engine.createWebView(&tab, false, 2.0, QSize(10, 10)));
        tab.attachWebView(wv);

        PermissionRequest* seen = nullptr;
        connect(&tab, &Tab::permissionRequest, this, [&seen](PermissionRequest* r) { seen = r; });

        bool allowed = false;
        wv->simulatePermission(PermissionKind::Geolocation, QStringLiteral("https://a.example"), &allowed);
        QVERIFY(seen);
        QVERIFY(s.answerFromStore(seen));
        QVERIFY(allowed);

        allowed = true;
        wv->simulatePermission(PermissionKind::Geolocation, QStringLiteral("https://d.example"), &allowed);
        QVERIFY(s.answerFromStore(seen));
        QVERIFY(!allowed);

        wv->simulatePermission(PermissionKind::Camera, QStringLiteral("https://a.example"), &allowed);
        QVERIFY(!s.answerFromStore(seen)); // must prompt
        seen->deny();
        QVERIFY(!allowed);
        QVERIFY(seen->answered());
        QTest::qWait(0);
    }
};

QTEST_GUILESS_MAIN(tst_PermissionStore)
#include "tst_permissionstore.moc"
