/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "model/sessionstore.h"

#include <QtTest>

using namespace Tuuli;

class tst_SessionStore : public QObject
{
    Q_OBJECT

    Session sample()
    {
        Session s;
        SessionTab a;
        a.url = QUrl(QStringLiteral("https://example.org/a"));
        a.title = QStringLiteral("A");
        a.scroll = QPointF(0, 120.5);
        a.zoom = 1.5;
        SessionTab b;
        b.url = QUrl(QStringLiteral("https://example.org/b"));
        b.title = QStringLiteral("B");
        b.desktopMode = true;
        s.tabs << a << b;
        s.currentIndex = 1;
        return s;
    }

private slots:
    void jsonRoundTrip()
    {
        const Session s = sample();
        bool ok = false;
        const Session back = SessionStore::fromJson(SessionStore::toJson(s), &ok);
        QVERIFY(ok);
        QVERIFY(back == s);
    }

    void saveAndLoad()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path() + QStringLiteral("/nested/session.json"));
        QVERIFY(!store.exists());
        QVERIFY(store.saveNow(sample()));
        QVERIFY(store.exists());
        bool ok = false;
        const Session back = store.load(&ok);
        QVERIFY(ok);
        QVERIFY(back == sample());
        QVERIFY(!QFile::exists(dir.path() + QStringLiteral("/nested/session.json.tmp")));
    }

    void debouncedSaveCollapsesBursts()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path() + QStringLiteral("/session.json"));
        store.setDebounceMs(50);
        QSignalSpy saved(&store, &SessionStore::saved);
        for (int i = 0; i < 20; ++i) {
            Session s = sample();
            s.currentIndex = i % 2;
            store.scheduleSave(s);
        }
        QVERIFY(store.hasPendingSave());
        QVERIFY(!store.exists());
        QVERIFY(saved.wait(1000));
        QCOMPARE(saved.size(), 1);
        QCOMPARE(store.load().currentIndex, 1);
    }

    void flushWritesImmediately()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path() + QStringLiteral("/session.json"));
        store.setDebounceMs(60000);
        store.scheduleSave(sample());
        QVERIFY(store.flush());
        QVERIFY(store.exists());
        QVERIFY(!store.hasPendingSave());
        QVERIFY(store.flush()); // nothing pending is fine
    }

    void cleanExitFlag()
    {
        QTemporaryDir dir;
        SessionStore store(dir.path() + QStringLiteral("/session.json"));
        Session s = sample();
        s.cleanExit = true;
        store.saveNow(s);
        QVERIFY(store.load().cleanExit);
        s.cleanExit = false;
        store.saveNow(s);
        QVERIFY(!store.load().cleanExit);
    }

    void corruptFileFails()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/session.json");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ not json");
        f.close();
        SessionStore store(path);
        bool ok = true;
        store.load(&ok);
        QVERIFY(!ok);
    }

    void missingFileFails()
    {
        SessionStore store(QStringLiteral("/nonexistent/dir/session.json"));
        bool ok = true;
        const Session s = store.load(&ok);
        QVERIFY(!ok);
        QVERIFY(s.tabs.isEmpty());
    }

    void newerFormatIsRejected()
    {
        QJsonObject o;
        o.insert(QStringLiteral("version"), 99);
        bool ok = true;
        SessionStore::fromJson(o, &ok);
        QVERIFY(!ok);
    }

    void invalidEntriesAreSkippedAndIndexClamped()
    {
        QJsonObject o;
        QJsonArray tabs;
        QJsonObject bad;
        bad.insert(QStringLiteral("title"), QStringLiteral("no url"));
        tabs.append(bad);
        QJsonObject good;
        good.insert(QStringLiteral("url"), QStringLiteral("https://a.example/"));
        good.insert(QStringLiteral("zoom"), -3);
        tabs.append(good);
        o.insert(QStringLiteral("tabs"), tabs);
        o.insert(QStringLiteral("currentIndex"), 7);
        bool ok = false;
        const Session s = SessionStore::fromJson(o, &ok);
        QVERIFY(ok);
        QCOMPARE(s.tabs.size(), 1);
        QCOMPARE(s.tabs.first().zoom, 1.0);
        QCOMPARE(s.currentIndex, 0);
    }
};

QTEST_GUILESS_MAIN(tst_SessionStore)
#include "tst_sessionstore.moc"
