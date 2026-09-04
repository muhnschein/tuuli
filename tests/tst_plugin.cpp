/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * Loads the built Tuuli QML plugin into a bare QQmlEngine (no Silica) and
 * drives the Browser singleton and a WebView item with the mock engine.
 * Catches registration and wiring mistakes the C++ tests cannot.
 */

#include "browsercontext.h"
#include "view/tuuliwebview.h"

#include "engine/mockengine.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QtTest>

static bool hostHasOpenGL()
{
    QOpenGLContext ctx;
    if (!ctx.create())
        return false;
    QOffscreenSurface surface;
    surface.create();
    return surface.isValid() && ctx.makeCurrent(&surface);
}

using namespace Tuuli;

class tst_Plugin : public QObject
{
    Q_OBJECT

    QTemporaryDir m_home;

private slots:
    void initTestCase()
    {
        QVERIFY(m_home.isValid());
        qputenv("XDG_DATA_HOME", (m_home.path() + QStringLiteral("/data")).toUtf8());
        qputenv("XDG_CONFIG_HOME", (m_home.path() + QStringLiteral("/config")).toUtf8());
        qputenv("XDG_CACHE_HOME", (m_home.path() + QStringLiteral("/cache")).toUtf8());
        qputenv("TUULI_ENGINE", "mock");
        QCoreApplication::setOrganizationName(QStringLiteral("org.tuuli"));
        QCoreApplication::setApplicationName(QStringLiteral("browser"));
    }

    void singletonAndWebViewWork()
    {
        QQuickView view;
        view.engine()->addImportPath(QStringLiteral(TUULI_QML_IMPORT_PATH));
        QQmlComponent component(view.engine());
        component.setData(
            "import QtQuick 2.2\n"
            "import Tuuli 1.0\n"
            "Item {\n"
            "  width: 540; height: 1130\n"
            "  property alias view: wv\n"
            "  property int tabCount: Browser.tabs.count\n"
            "  property string engine: Browser.engineName\n"
            "  WebView { id: wv; anchors.fill: parent; tab: Browser.tabs.currentTab }\n"
            "  Component.onCompleted: Browser.tabs.newTab('https://example.org/', false, true)\n"
            "}\n", QUrl(QStringLiteral("qrc:/inline.qml")));
        if (component.isError())
            qWarning() << component.errors();
        QVERIFY(!component.isError());

        QObject* root = component.create();
        QVERIFY(root);
        QCOMPARE(root->property("engine").toString(), QStringLiteral("mock"));
        QCOMPARE(root->property("tabCount").toInt(), 1);

        BrowserContext* ctx = BrowserContext::instance();
        QVERIFY(ctx);
        QVERIFY(ctx->dataDirectory().startsWith(m_home.path()));
        Tab* tab = ctx->tabs()->currentTab();
        QVERIFY(tab);
        QCOMPARE(tab->url(), QUrl(QStringLiteral("https://example.org/")));

        TuuliWebView* wv = qobject_cast<TuuliWebView*>(root->property("view").value<QObject*>());
        QVERIFY(wv);
        QCOMPARE(wv->tab(), tab);
        QVERIFY(wv->inputMethod());
        QCOMPARE(wv->engineName(), QStringLiteral("mock"));

        if (hostHasOpenGL()) {
            // Render once so the renderer initialises the engine on the
            // render thread and the tab materialises a webview: this is the
            // real QQuickFramebufferObject path.
            QQuickItem* rootItem = qobject_cast<QQuickItem*>(root);
            QVERIFY(rootItem);
            rootItem->setParentItem(view.contentItem());
            view.resize(540, 1130);
            view.show();
            QVERIFY(QTest::qWaitForWindowExposed(&view));
            QTRY_VERIFY_WITH_TIMEOUT(ctx->engine()->isInitialized(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(wv->frameCount() > 0, 5000);
        } else {
            qWarning("No OpenGL on this host; initialising the mock engine directly");
            static_cast<MockEngine*>(ctx->engine())->initializeForTests();
        }
        QTRY_VERIFY_WITH_TIMEOUT(tab->hasWebView(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(tab->title(), QStringLiteral("example.org"), 5000);
        QVERIFY(wv->engineReady());
        QVERIFY(wv->contentDevicePixelRatio() >= 1.0);

        // History recorded the visit; session file exists.
        QTRY_VERIFY(ctx->history()->totalCount() == 1);
        ctx->saveSessionNow();
        QVERIFY(ctx->session()->exists());

        // Address resolution goes through the user's search engine.
        QCOMPARE(ctx->resolveInput(QStringLiteral("jolla.com")), QUrl(QStringLiteral("http://jolla.com")));
        QCOMPARE(ctx->resolveInput(QStringLiteral("servo browser")).host(), QStringLiteral("duckduckgo.com"));

        delete root;
    }
};

QTEST_MAIN(tst_Plugin)
#include "tst_plugin.moc"
