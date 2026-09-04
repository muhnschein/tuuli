/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "engine/mockengine.h"
#include "model/downloadmanager.h"
#include "model/tab.h"

#include <QtTest>

using namespace Tuuli;

class tst_DownloadManager : public QObject
{
    Q_OBJECT
private slots:
    void sanitizesNames()
    {
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("../../etc/passwd")), QStringLiteral("_.._etc_passwd"));
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("..hidden")), QStringLiteral("hidden"));
        QCOMPARE(DownloadManager::sanitizeFileName(QStringLiteral("   ")), QStringLiteral("download"));
        QCOMPARE(DownloadManager::sanitizeFileName(QString(300, QLatin1Char('a'))).size(), 200);
    }

    void uniquePathsAvoidCollisions()
    {
        QTemporaryDir dir;
        const QString first = DownloadManager::uniquePath(dir.path(), QStringLiteral("file.tar.gz"));
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("file.tar.gz"));
        QFile f(first);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        const QString second = DownloadManager::uniquePath(dir.path(), QStringLiteral("file.tar.gz"));
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("file.tar(1).gz"));
    }

    void handlesEngineDownloadToCompletion()
    {
        QTemporaryDir dir;
        DownloadManager dm(nullptr);
        dm.setDirectory(dir.path());
        QSignalSpy started(&dm, &DownloadManager::downloadStarted);
        QSignalSpy finished(&dm, &DownloadManager::downloadFinished);

        MockEngine engine;
        engine.initializeForTests();
        Tab tab(1, false);
        MockWebView* wv = static_cast<MockWebView*>(engine.createWebView(&tab, false, 1.0, QSize(10, 10)));
        tab.attachWebView(wv);
        connect(&tab, &Tab::downloadRequest, this, [&dm](DownloadRequest* r) { dm.handleRequest(r, false); });

        wv->simulateDownload(QUrl(QStringLiteral("https://a.example/big.bin")), QStringLiteral("big.bin"),
                             QStringLiteral("application/octet-stream"), 1000);
        QCOMPARE(dm.count(), 1);
        QCOMPARE(dm.activeCount(), 1);
        QCOMPARE(started.size(), 1);
        QCOMPARE(dm.data(dm.index(0), DownloadManager::FileNameRole).toString(), QStringLiteral("big.bin"));
        QVERIFY(dm.data(dm.index(0), DownloadManager::PathRole).toString().startsWith(dir.path()));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(dm.activeCount(), 0);
        QCOMPARE(dm.data(dm.index(0), DownloadManager::OkRole).toBool(), true);
        QCOMPARE(dm.data(dm.index(0), DownloadManager::ProgressRole).toDouble(), 1.0);
        dm.clearFinished();
        QCOMPARE(dm.count(), 0);
    }

    void privateDownloadsAreFlaggedAndClearable()
    {
        QTemporaryDir dir;
        DownloadManager dm(nullptr);
        dm.setDirectory(dir.path());
        MockEngine engine;
        engine.initializeForTests();
        Tab tab(1, true);
        MockWebView* wv = static_cast<MockWebView*>(engine.createWebView(&tab, true, 1.0, QSize(10, 10)));
        tab.attachWebView(wv);
        connect(&tab, &Tab::downloadRequest, this, [&dm, &tab](DownloadRequest* r) { dm.handleRequest(r, tab.isPrivate()); });
        wv->simulateDownload(QUrl(QStringLiteral("https://a.example/x")), QString(), QString(), -1);
        QCOMPARE(dm.count(), 1);
        QCOMPARE(dm.data(dm.index(0), DownloadManager::PrivateRole).toBool(), true);
        QCOMPARE(dm.data(dm.index(0), DownloadManager::FileNameRole).toString(), QStringLiteral("x"));
        dm.clearPrivate();
        QCOMPARE(dm.count(), 0);
    }
};

QTEST_GUILESS_MAIN(tst_DownloadManager)
#include "tst_downloadmanager.moc"
