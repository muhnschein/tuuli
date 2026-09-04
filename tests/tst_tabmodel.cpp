/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "engine/mockengine.h"
#include "model/tabmodel.h"

#include <QtTest>

using namespace Tuuli;

class tst_TabModel : public QObject
{
    Q_OBJECT
private slots:
    void newTabActivatesAndCreatesWebView()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        tabs.setViewportGeometry(QSize(1080, 2260), 2.5);
        QSignalSpy count(&tabs, &TabModel::countChanged);
        Tab* t = tabs.newTab(QUrl(QStringLiteral("https://example.org/")));
        QVERIFY(t);
        QCOMPARE(tabs.count(), 1);
        QCOMPARE(count.size(), 1);
        QCOMPARE(tabs.currentIndex(), 0);
        QCOMPARE(tabs.currentTab(), t);
        QVERIFY(t->hasWebView());
        QCOMPARE(engine.webViews.size(), 1);
        MockWebView* wv = engine.webViews.first();
        QCOMPARE(wv->size(), QSize(1080, 2260));
        QCOMPARE(wv->dpr(), 2.5);
        QVERIFY(wv->visible());
        QVERIFY(wv->focused());
        QCOMPARE(wv->url(), QUrl(QStringLiteral("https://example.org/")));
    }

    void tabsAreLazyWithoutEngine()
    {
        MockEngine engine;
        TabModel tabs(&engine);
        Tab* t = tabs.newTab(QUrl(QStringLiteral("https://example.org/")));
        QVERIFY(!t->hasWebView());
        QCOMPARE(t->url(), QUrl(QStringLiteral("https://example.org/")));
        engine.initializeForTests();
        QVERIFY(t->hasWebView());
    }

    void switchingTabsTogglesVisibility()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        tabs.newTab(QUrl(QStringLiteral("https://b.example/")));
        QCOMPARE(tabs.currentIndex(), 1);
        QVERIFY(!engine.webViews.at(0)->visible());
        QVERIFY(engine.webViews.at(1)->visible());
        tabs.setCurrentIndex(0);
        QVERIFY(engine.webViews.at(0)->visible());
        QVERIFY(!engine.webViews.at(1)->visible());
    }

    void closeTabPicksNeighbour()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        Tab* a = tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        Tab* b = tabs.newTab(QUrl(QStringLiteral("https://b.example/")));
        Tab* c = tabs.newTab(QUrl(QStringLiteral("https://c.example/")));
        Q_UNUSED(b);
        QSignalSpy closed(&tabs, &TabModel::tabClosed);
        tabs.setCurrentIndex(1);
        tabs.closeTab(1);
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(closed.size(), 1);
        QCOMPARE(tabs.currentTab(), c);
        tabs.closeTab(1);
        QCOMPARE(tabs.currentTab(), a);
        tabs.closeTab(0);
        QCOMPARE(tabs.count(), 0);
        QCOMPARE(tabs.currentIndex(), -1);
        QVERIFY(!tabs.currentTab());
    }

    void closingEarlierTabKeepsCurrent()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        Tab* b = tabs.newTab(QUrl(QStringLiteral("https://b.example/")));
        tabs.closeTab(0);
        QCOMPARE(tabs.currentIndex(), 0);
        QCOMPARE(tabs.currentTab(), b);
    }

    void moveTabTracksCurrent()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        Tab* a = tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        Tab* b = tabs.newTab(QUrl(QStringLiteral("https://b.example/")));
        Tab* c = tabs.newTab(QUrl(QStringLiteral("https://c.example/")));
        tabs.setCurrentIndex(0);
        tabs.moveTab(0, 2);
        QCOMPARE(tabs.tabAt(0), b);
        QCOMPARE(tabs.tabAt(1), c);
        QCOMPARE(tabs.tabAt(2), a);
        QCOMPARE(tabs.currentIndex(), 2);
        QCOMPARE(tabs.currentTab(), a);
        tabs.moveTab(2, 0);
        QCOMPARE(tabs.currentIndex(), 0);
    }

    void privateTabsAreExcludedFromSnapshot()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        Tab* p = tabs.newTab(QUrl(QStringLiteral("https://secret.example/")), true);
        tabs.newTab(QUrl(QStringLiteral("https://c.example/")));
        QVERIFY(p->isPrivate());
        QVERIFY(engine.webViews.at(1)->isPrivate());
        QCOMPARE(tabs.privateCount(), 1);
        tabs.setCurrentIndex(2);
        const Session s = tabs.snapshot();
        QCOMPARE(s.tabs.size(), 2);
        QCOMPARE(s.tabs.at(0).url, QUrl(QStringLiteral("https://a.example/")));
        QCOMPARE(s.tabs.at(1).url, QUrl(QStringLiteral("https://c.example/")));
        QCOMPARE(s.currentIndex, 1);
        // Current tab private: snapshot points at the first non-private tab.
        tabs.setCurrentIndex(1);
        QCOMPARE(tabs.snapshot().currentIndex, 0);
        tabs.closeAllPrivate();
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(tabs.privateCount(), 0);
    }

    void restoreRecreatesTabsAndState()
    {
        MockEngine engine;
        TabModel tabs(&engine);
        Session s;
        SessionTab a;
        a.url = QUrl(QStringLiteral("https://a.example/"));
        a.title = QStringLiteral("A");
        a.scroll = QPointF(0, 300);
        a.zoom = 2.0;
        SessionTab b;
        b.url = QUrl(QStringLiteral("https://b.example/"));
        b.desktopMode = true;
        s.tabs << a << b;
        s.currentIndex = 1;
        tabs.restore(s);
        QCOMPARE(tabs.count(), 2);
        QCOMPARE(tabs.currentIndex(), 1);
        QCOMPARE(tabs.tabAt(0)->title(), QStringLiteral("A"));
        QCOMPARE(tabs.tabAt(0)->scrollOffset(), QPointF(0, 300));
        QVERIFY(tabs.tabAt(1)->desktopMode());
        QVERIFY(!tabs.tabAt(0)->hasWebView());

        engine.initializeForTests();
        // Only the current tab is materialised.
        QVERIFY(tabs.tabAt(1)->hasWebView());
        QVERIFY(!tabs.tabAt(0)->hasWebView());
        QCOMPARE(engine.webViews.size(), 1);
        QVERIFY(!engine.webViews.first()->userAgent.isEmpty()); // desktop UA applied

        // Activating the other tab restores its viewport after load.
        tabs.setCurrentIndex(0);
        QVERIFY(tabs.tabAt(0)->hasWebView());
        MockWebView* wv = engine.webViews.last();
        QTRY_COMPARE(wv->pinchZoom(), 2.0);
        QCOMPARE(wv->scroll(), QPointF(0, 300));
    }

    void liveWebViewBudgetDropsLeastRecentlyUsed()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        tabs.setMaxLiveWebViews(2);
        Tab* a = tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        QTest::qWait(2);
        Tab* b = tabs.newTab(QUrl(QStringLiteral("https://b.example/")));
        QTest::qWait(2);
        Tab* c = tabs.newTab(QUrl(QStringLiteral("https://c.example/")));
        QCOMPARE(tabs.liveWebViewCount(), 2);
        QVERIFY(!a->hasWebView());
        QVERIFY(b->hasWebView());
        QVERIFY(c->hasWebView());
        QCOMPARE(a->url(), QUrl(QStringLiteral("https://a.example/")));
        tabs.setCurrentIndex(0);
        QVERIFY(a->hasWebView());
        QVERIFY(!b->hasWebView());
        QCOMPARE(tabs.liveWebViewCount(), 2);
    }

    void renderContextLossDetachesAndRecreates()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        Tab* a = tabs.newTab(QUrl(QStringLiteral("https://a.example/")));
        QTRY_VERIFY(!a->title().isEmpty());
        engine.shutdownOnRenderThread();
        QTRY_VERIFY(!a->hasWebView());
        QCOMPARE(a->url(), QUrl(QStringLiteral("https://a.example/")));
        QCOMPARE(a->title(), QStringLiteral("a.example")); // kept for the UI
        engine.initializeOnRenderThread(nullptr);
        QTRY_VERIFY(a->hasWebView());
        QCOMPARE(engine.webViews.last()->url(), QUrl(QStringLiteral("https://a.example/")));
    }

    void tabPropertiesFollowEngine()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        Tab* t = tabs.newTab(QUrl(QStringLiteral("https://a.example/path")));
        QVERIFY(t->loading());
        QTRY_VERIFY(!t->loading());
        QCOMPARE(t->title(), QStringLiteral("a.example"));
        QCOMPARE(t->displayTitle(), QStringLiteral("a.example"));
        MockWebView* wv = engine.webViews.first();
        wv->simulateHistory(true, false);
        QVERIFY(t->canGoBack());
        QVERIFY(!t->canGoForward());
        QImage icon(16, 16, QImage::Format_ARGB32);
        icon.fill(Qt::red);
        wv->simulateFavicon(icon);
        QVERIFY(t->hasFavicon());
        QVERIFY(t->faviconSource().toString().startsWith(QStringLiteral("image://tuuli/favicon/")));
        QCOMPARE(tabs.data(tabs.index(0), TabModel::TitleRole).toString(), QStringLiteral("a.example"));
        QCOMPARE(tabs.data(tabs.index(0), TabModel::ActiveRole).toBool(), true);
    }

    void newTabRequestFromContentOpensTab()
    {
        MockEngine engine;
        engine.initializeForTests();
        TabModel tabs(&engine);
        Tab* p = tabs.newTab(QUrl(QStringLiteral("https://a.example/")), true);
        static_cast<WebViewClient*>(p)->onNewWebViewRequested(QUrl(QStringLiteral("https://popup.example/")));
        QCOMPARE(tabs.count(), 2);
        QVERIFY(tabs.currentTab()->isPrivate()); // inherits privacy
        QCOMPARE(tabs.currentTab()->url(), QUrl(QStringLiteral("https://popup.example/")));
    }
};

QTEST_GUILESS_MAIN(tst_TabModel)
#include "tst_tabmodel.moc"
