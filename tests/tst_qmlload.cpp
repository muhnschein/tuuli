// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
//
// Loads the real QML against tests/silica-stubs and drives it through objectNames.
// The stubs imitate no layout: these tests prove structure and wiring, not appearance.
#include "Core.h"
#include "QmlTypes.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQuickItem>
#include <QScopedPointer>
#include <QSet>
#include <QTemporaryDir>
#include <QtTest>

using Tuuli::BookmarkModel;
using Tuuli::Core;
using Tuuli::Settings;

namespace {

const char *const RootQml = TUULI_SOURCE_DIR "/qml/harbour-tuuli.qml";

} // namespace

class tst_qmlload : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void rootWindowLoads();
    void addressBarNavigates();
    void toolbarDrivesWebView();
    void faviconResolvedAfterLoad();
    void tabsPage();
    void restoredTabsLoadLazily();
    void menuPage();
    void historyPage();
    void bookmarksPage();
    void settingsPage();
    void cover();

private:
    bool loadWindow();
    QObject *find(const QString &name) const;
    QList<QObject *> findAll(const QString &name) const;
    QObject *pageStack() const;
    QObject *currentPage() const;
    QObject *currentWebView() const;
    QVariant evaluate(QObject *scope, const QString &expression) const;
    static void click(QObject *object);
    static void enterKey(QObject *field);
    void popPage() const;
    QObject *openMenuItem(const QString &itemName);

    QScopedPointer<QTemporaryDir> m_dir;
    QScopedPointer<Core> m_core;
    QScopedPointer<QQmlEngine> m_engine;
    QScopedPointer<QObject> m_window;
};

void tst_qmlload::init()
{
    m_dir.reset(new QTemporaryDir);
    m_core.reset(new Core(m_dir->path(), m_dir->path() + QStringLiteral("/tuuli.conf")));
    QVERIFY(loadWindow());
}

void tst_qmlload::cleanup()
{
    m_window.reset();
    m_engine.reset();
    m_core.reset();
    m_dir.reset();
}

bool tst_qmlload::loadWindow()
{
    Tuuli::registerQmlTypes(m_core.data());
    m_engine.reset(new QQmlEngine);
    m_engine->addImportPath(QStringLiteral(TUULI_STUBS_DIR));
    QQmlComponent component(m_engine.data(), QUrl::fromLocalFile(QLatin1String(RootQml)));
    if (component.isError()) {
        qWarning() << component.errorString();
        return false;
    }
    m_window.reset(component.create());
    return !m_window.isNull() && find(QStringLiteral("browserPage")) != nullptr;
}

namespace {

// The stub page stack destroys popped pages with QML's deferred destroy(); settle it
// before searching so stale pages are not found.
void settle()
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

// Delegates created by Repeater and ListView have no QObject parent, so the search
// walks the visual item tree as well as QObject children.
QList<QObject *> findObjects(QObject *root, const QString &name)
{
    settle();
    QList<QObject *> found;
    QSet<QObject *> seen;
    QList<QObject *> pending{root};
    while (!pending.isEmpty()) {
        QObject *object = pending.takeLast();
        if (object == nullptr || seen.contains(object)) {
            continue;
        }
        seen.insert(object);
        if (object->objectName() == name) {
            found.append(object);
        }
        // Item views batch model changes until the next frame; there is no frame here.
        if (object->inherits("QQuickItemView")) {
            QMetaObject::invokeMethod(object, "forceLayout");
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
        QList<QObject *> children = object->children();
        auto *item = qobject_cast<QQuickItem *>(object);
        if (item != nullptr) {
            for (QQuickItem *child : item->childItems()) {
                children.append(child);
            }
        }
        // Reverse so the traversal keeps document order.
        for (int i = children.count() - 1; i >= 0; --i) {
            pending.append(children.at(i));
        }
    }
    return found;
}

} // namespace

QObject *tst_qmlload::find(const QString &name) const
{
    const QList<QObject *> found = findObjects(m_window.data(), name);
    return found.isEmpty() ? nullptr : found.first();
}

QList<QObject *> tst_qmlload::findAll(const QString &name) const
{
    return findObjects(m_window.data(), name);
}

QObject *tst_qmlload::pageStack() const
{
    return m_window->property("pageStack").value<QObject *>();
}

QObject *tst_qmlload::currentPage() const
{
    settle();
    return pageStack()->property("currentPage").value<QObject *>();
}

QObject *tst_qmlload::currentWebView() const
{
    return find(QStringLiteral("browserPage"))->property("currentView").value<QObject *>();
}

QVariant tst_qmlload::evaluate(QObject *scope, const QString &expression) const
{
    QQmlExpression script(qmlContext(scope), scope, expression);
    const QVariant result = script.evaluate();
    if (script.hasError()) {
        qWarning() << script.error().toString();
    }
    return result;
}

void tst_qmlload::click(QObject *object)
{
    QMetaObject::invokeMethod(object, "clicked");
}

void tst_qmlload::enterKey(QObject *field)
{
    auto *attached = field->findChild<QObject *>(QStringLiteral("EnterKeyAttached"));
    QVERIFY2(attached != nullptr, "field has no EnterKey handler");
    QMetaObject::invokeMethod(attached, "clicked");
}

void tst_qmlload::popPage() const
{
    QVariant result;
    QMetaObject::invokeMethod(pageStack(), "pop", Q_RETURN_ARG(QVariant, result),
                              Q_ARG(QVariant, QVariant()), Q_ARG(QVariant, QVariant()));
}

QObject *tst_qmlload::openMenuItem(const QString &itemName)
{
    click(find(QStringLiteral("menuButton")));
    QObject *item = find(itemName);
    if (item == nullptr) {
        return nullptr;
    }
    click(item);
    return currentPage();
}

void tst_qmlload::rootWindowLoads()
{
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(m_core->tabs()->count(), 1);
    QCOMPARE(m_core->tabs()->activeUrl(), Settings::defaultHomePage());

    QObject *webView = find(QStringLiteral("webView"));
    QVERIFY(webView != nullptr);
    QCOMPARE(currentWebView(), webView);
    QCOMPARE(webView->property("url").toUrl().toString(), Settings::defaultHomePage());
    QVERIFY(!webView->property("privateMode").toBool());
    QVERIFY(webView->property("downloadsEnabled").toBool());
    QVERIFY(!webView->property("desktopMode").toBool());

    // The engine reporting the first url is the first visit.
    QCOMPARE(m_core->history()->count(), 1);
    QCOMPARE(find(QStringLiteral("addressField"))->property("text").toString(),
             Settings::defaultHomePage());
}

void tst_qmlload::addressBarNavigates()
{
    QObject *field = find(QStringLiteral("addressField"));
    QObject *webView = currentWebView();

    field->setProperty("text", QStringLiteral("example.org"));
    enterKey(field);
    QCOMPARE(webView->property("url").toUrl().toString(), QStringLiteral("https://example.org"));
    QCOMPARE(m_core->tabs()->activeUrl(), QStringLiteral("https://example.org"));
    QCOMPARE(m_core->history()->count(), 2);
    QCOMPARE(field->property("text").toString(), QStringLiteral("https://example.org"));

    const QUrl searchUrl(QStringLiteral("https://duckduckgo.com/?q=sailfish%20os"));
    field->setProperty("text", QStringLiteral("sailfish os"));
    enterKey(field);
    QCOMPARE(webView->property("url").toUrl(), searchUrl);

    field->setProperty("text", QString());
    enterKey(field);
    QCOMPARE(webView->property("url").toUrl(), searchUrl);
    QCOMPARE(m_core->tabs()->count(), 1);
}

void tst_qmlload::toolbarDrivesWebView()
{
    QObject *webView = currentWebView();
    QObject *back = find(QStringLiteral("backButton"));
    QObject *forward = find(QStringLiteral("forwardButton"));
    QObject *reload = find(QStringLiteral("reloadButton"));

    QVERIFY(!back->property("enabled").toBool());
    webView->setProperty("canGoBack", true);
    webView->setProperty("canGoForward", true);
    QVERIFY(back->property("enabled").toBool());
    QVERIFY(forward->property("enabled").toBool());

    click(back);
    click(forward);
    click(reload);
    QCOMPARE(webView->property("calls").toStringList(),
             QStringList({QStringLiteral("goBack"), QStringLiteral("goForward"),
                          QStringLiteral("reload")}));

    QObject *progress = find(QStringLiteral("loadProgress"));
    QVERIFY(!progress->property("visible").toBool());
    webView->setProperty("loading", true);
    webView->setProperty("loadProgress", 50);
    QVERIFY(progress->property("visible").toBool());
    click(reload);
    QCOMPARE(webView->property("calls").toStringList().last(), QStringLiteral("stop"));

    QCOMPARE(find(QStringLiteral("tabCountLabel"))->property("text").toString(),
             QStringLiteral("1"));
    click(find(QStringLiteral("tabsButton")));
    QCOMPARE(currentPage()->objectName(), QStringLiteral("tabsPage"));
}

void tst_qmlload::faviconResolvedAfterLoad()
{
    QObject *webView = currentWebView();
    webView->setProperty("scriptResult", QStringLiteral("/icon.png"));
    webView->setProperty("loading", true);
    webView->setProperty("loading", false);
    QCOMPARE(webView->property("lastScript").toString(), m_core->engineMessages()->faviconScript());
    QCOMPARE(m_core->tabs()->activeFavicon(), QStringLiteral("https://duckduckgo.com/icon.png"));

    webView->setProperty("scriptFails", true);
    webView->setProperty("loading", true);
    webView->setProperty("loading", false);
    QCOMPARE(m_core->tabs()->activeFavicon(), QStringLiteral("https://duckduckgo.com/favicon.ico"));
}

void tst_qmlload::tabsPage()
{
    m_core->tabs()->newTab(QStringLiteral("https://two.example/"));
    QCOMPARE(findAll(QStringLiteral("webView")).count(), 2);
    QCOMPARE(currentWebView()->property("url").toUrl().toString(),
             QStringLiteral("https://two.example/"));

    click(find(QStringLiteral("tabsButton")));
    QList<QObject *> delegates = findAll(QStringLiteral("tabDelegate"));
    QCOMPARE(delegates.count(), 2);
    QCOMPARE(delegates.at(1)
                 ->findChild<QObject *>(QStringLiteral("tabTitle"))
                 ->property("text")
                 .toString(),
             QStringLiteral("https://two.example/"));
    QVERIFY(delegates.at(1)->property("highlighted").toBool());

    click(delegates.at(0));
    QCOMPARE(m_core->tabs()->activeTabIndex(), 0);
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(currentWebView()->property("url").toUrl().toString(), Settings::defaultHomePage());

    click(find(QStringLiteral("tabsButton")));
    delegates = findAll(QStringLiteral("tabDelegate"));
    click(findObjects(delegates.at(1), QStringLiteral("closeTabButton")).first());
    QCOMPARE(m_core->tabs()->count(), 1);
    QCOMPARE(findAll(QStringLiteral("tabDelegate")).count(), 1);

    click(find(QStringLiteral("newPrivateTabMenu")));
    QCOMPARE(m_core->tabs()->count(), 2);
    QVERIFY(m_core->tabs()->activeIsPrivate());
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QVERIFY(currentWebView()->property("privateMode").toBool());
    QCOMPARE(find(QStringLiteral("addressField"))->property("label").toString(),
             QStringLiteral("Private tab"));

    click(find(QStringLiteral("tabsButton")));
    click(find(QStringLiteral("newTabMenu")));
    QCOMPARE(m_core->tabs()->count(), 3);

    click(find(QStringLiteral("tabsButton")));
    delegates = findAll(QStringLiteral("tabDelegate"));
    click(findObjects(delegates.at(0), QStringLiteral("closeTabMenu")).first());
    QCOMPARE(m_core->tabs()->count(), 2);

    click(find(QStringLiteral("closeAllTabsMenu")));
    QCOMPARE(m_core->tabs()->count(), 1);
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(m_core->tabs()->activeUrl(), Settings::defaultHomePage());
}

void tst_qmlload::restoredTabsLoadLazily()
{
    m_core->tabs()->newTab(QStringLiteral("https://two.example/"));
    m_core->tabs()->newTab(QStringLiteral("https://three.example/"));
    m_core->tabs()->activateTab(1);

    m_window.reset();
    m_engine.reset();
    m_core.reset(new Core(m_dir->path(), m_dir->path() + QStringLiteral("/tuuli.conf")));
    QVERIFY(loadWindow());

    QCOMPARE(m_core->tabs()->count(), 3);
    QCOMPARE(findAll(QStringLiteral("webViewLoader")).count(), 3);
    QCOMPARE(findAll(QStringLiteral("webView")).count(), 1);
    QCOMPARE(currentWebView()->property("url").toUrl().toString(),
             QStringLiteral("https://two.example/"));
    // The three pages were visited in the first session; restoring adds no visits.
    QCOMPARE(m_core->history()->count(), 3);

    m_core->tabs()->activateTab(2);
    QCOMPARE(findAll(QStringLiteral("webView")).count(), 2);
    QCOMPARE(currentWebView()->property("url").toUrl().toString(),
             QStringLiteral("https://three.example/"));
    QCOMPARE(m_core->history()->count(), 3);
}

void tst_qmlload::menuPage()
{
    QObject *page = openMenuItem(QStringLiteral("newTabItem"));
    QCOMPARE(page->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(m_core->tabs()->count(), 2);

    openMenuItem(QStringLiteral("newPrivateTabItem"));
    QCOMPARE(m_core->tabs()->count(), 3);
    QVERIFY(m_core->tabs()->activeIsPrivate());

    click(find(QStringLiteral("menuButton")));
    auto *bookmarkLabel = find(QStringLiteral("bookmarkItem"))->findChild<QObject *>();
    QCOMPARE(bookmarkLabel->property("text").toString(), QStringLiteral("Bookmark this page"));
    click(find(QStringLiteral("bookmarkItem")));
    QCOMPARE(m_core->bookmarks()->count(), 1);
    QVERIFY(m_core->bookmarks()->activeUrlBookmarked());

    click(find(QStringLiteral("menuButton")));
    bookmarkLabel = find(QStringLiteral("bookmarkItem"))->findChild<QObject *>();
    QCOMPARE(bookmarkLabel->property("text").toString(), QStringLiteral("Remove bookmark"));
    click(find(QStringLiteral("bookmarkItem")));
    QCOMPARE(m_core->bookmarks()->count(), 0);

    click(find(QStringLiteral("menuButton")));
    QObject *share = find(QStringLiteral("shareAction"));
    click(find(QStringLiteral("shareItem")));
    QCOMPARE(share->property("triggerCount").toInt(), 1);
    QCOMPARE(share->property("mimeType").toString(), QStringLiteral("text/x-url"));
    const QVariantMap resource = share->property("resources").toList().first().toMap();
    QCOMPARE(resource.value(QStringLiteral("status")).toString(), m_core->tabs()->activeUrl());
    popPage();

    QCOMPARE(openMenuItem(QStringLiteral("bookmarksItem"))->objectName(),
             QStringLiteral("bookmarksPage"));
    popPage();
    QCOMPARE(openMenuItem(QStringLiteral("historyItem"))->objectName(),
             QStringLiteral("historyPage"));
    popPage();
    QCOMPARE(openMenuItem(QStringLiteral("settingsItem"))->objectName(),
             QStringLiteral("settingsPage"));
    popPage();
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
}

void tst_qmlload::historyPage()
{
    m_core->history()->visit(QStringLiteral("https://one.example/"), QStringLiteral("One"));
    m_core->history()->visit(QStringLiteral("https://two.example/"), QStringLiteral("Two"));

    openMenuItem(QStringLiteral("historyItem"));
    QCOMPARE(findAll(QStringLiteral("historyDelegate")).count(), 3);
    QObject *search = find(QStringLiteral("historySearch"));
    search->setProperty("text", QStringLiteral("two"));
    QCOMPARE(m_core->history()->count(), 1);
    QCOMPARE(findAll(QStringLiteral("historyDelegate")).count(), 1);
    search->setProperty("text", QString());
    QCOMPARE(findAll(QStringLiteral("historyDelegate")).count(), 3);

    QList<QObject *> delegates = findAll(QStringLiteral("historyDelegate"));
    QCOMPARE(delegates.at(1)
                 ->findChild<QObject *>(QStringLiteral("historyTitle"))
                 ->property("text")
                 .toString(),
             QStringLiteral("One"));
    click(delegates.at(1));
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(currentWebView()->property("url").toUrl().toString(),
             QStringLiteral("https://one.example/"));
    QCOMPARE(m_core->tabs()->count(), 1);

    openMenuItem(QStringLiteral("historyItem"));
    delegates = findAll(QStringLiteral("historyDelegate"));
    click(findObjects(delegates.at(0), QStringLiteral("openInNewTabMenu")).first());
    QCOMPARE(m_core->tabs()->count(), 2);
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));

    openMenuItem(QStringLiteral("historyItem"));
    const int before = m_core->history()->count();
    delegates = findAll(QStringLiteral("historyDelegate"));
    click(findObjects(delegates.at(0), QStringLiteral("removeHistoryMenu")).first());
    QCOMPARE(m_core->history()->count(), before - 1);

    click(find(QStringLiteral("clearHistoryMenu")));
    QCOMPARE(m_core->history()->count(), 0);
    QCOMPARE(findAll(QStringLiteral("historyDelegate")).count(), 0);
}

void tst_qmlload::bookmarksPage()
{
    m_core->bookmarks()->add(QStringLiteral("https://b1.example/"), QStringLiteral("B1"));
    m_core->bookmarks()->add(QStringLiteral("https://b2.example/"), QStringLiteral("B2"));

    openMenuItem(QStringLiteral("bookmarksItem"));
    QList<QObject *> delegates = findAll(QStringLiteral("bookmarkDelegate"));
    QCOMPARE(delegates.count(), 2);
    click(delegates.at(0));
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
    QCOMPARE(currentWebView()->property("url").toUrl().toString(),
             QStringLiteral("https://b1.example/"));

    openMenuItem(QStringLiteral("bookmarksItem"));
    delegates = findAll(QStringLiteral("bookmarkDelegate"));
    click(findObjects(delegates.at(1), QStringLiteral("editBookmarkMenu")).first());
    QObject *dialog = currentPage();
    QCOMPARE(dialog->objectName(), QStringLiteral("bookmarkEditDialog"));
    QCOMPARE(dialog->property("bookmarkIndex").toInt(), 1);
    QCOMPARE(dialog->property("url").toString(), QStringLiteral("https://b2.example/"));
    find(QStringLiteral("bookmarkUrlField"))
        ->setProperty("text", QStringLiteral("https://b2.example/x"));
    find(QStringLiteral("bookmarkTitleField"))->setProperty("text", QStringLiteral("B2x"));
    QMetaObject::invokeMethod(dialog, "accept");
    QCOMPARE(m_core->bookmarks()
                 ->data(m_core->bookmarks()->index(1, 0), BookmarkModel::UrlRole)
                 .toString(),
             QStringLiteral("https://b2.example/x"));
    QCOMPARE(m_core->bookmarks()
                 ->data(m_core->bookmarks()->index(1, 0), BookmarkModel::TitleRole)
                 .toString(),
             QStringLiteral("B2x"));
    popPage();

    delegates = findAll(QStringLiteral("bookmarkDelegate"));
    click(findObjects(delegates.at(0), QStringLiteral("removeBookmarkMenu")).first());
    QCOMPARE(m_core->bookmarks()->count(), 1);
    QCOMPARE(delegates.at(0)->property("remorseCount").toInt(), 1);

    QObject *addMenu = find(QStringLiteral("addBookmarkMenu"));
    QVERIFY(addMenu->property("enabled").toBool());
    click(addMenu);
    QCOMPARE(m_core->bookmarks()->count(), 2);
    QVERIFY(m_core->bookmarks()->contains(QStringLiteral("https://b1.example/")));
    QVERIFY(!addMenu->property("enabled").toBool());

    delegates = findAll(QStringLiteral("bookmarkDelegate"));
    click(findObjects(delegates.at(0), QStringLiteral("openInNewTabMenu")).first());
    QCOMPARE(m_core->tabs()->count(), 2);
    QCOMPARE(currentPage()->objectName(), QStringLiteral("browserPage"));
}

void tst_qmlload::settingsPage()
{
    QObject *page = openMenuItem(QStringLiteral("settingsItem"));
    QCOMPARE(page->objectName(), QStringLiteral("settingsPage"));

    QObject *home = find(QStringLiteral("homePageField"));
    QCOMPARE(home->property("text").toString(), Settings::defaultHomePage());
    home->setProperty("text", QStringLiteral("sailfishos.org"));
    enterKey(home);
    QCOMPARE(m_core->settings()->homePage(), QStringLiteral("https://sailfishos.org"));

    find(QStringLiteral("searchEngineCombo"))->setProperty("currentIndex", 1);
    QCOMPARE(m_core->settings()->searchEngineIndex(), 1);

    find(QStringLiteral("desktopModeSwitch"))->setProperty("checked", true);
    QVERIFY(m_core->settings()->desktopMode());
    QVERIFY(currentWebView()->property("desktopMode").toBool());

    click(find(QStringLiteral("clearHistoryButton")));
    QCOMPARE(m_core->history()->count(), 0);

    click(find(QStringLiteral("clearSiteDataButton")));
    click(find(QStringLiteral("clearCacheButton")));
    QCOMPARE(evaluate(page, QStringLiteral("WebEngine.notifications.length")).toInt(), 2);
    QCOMPARE(evaluate(page, QStringLiteral("WebEngine.notifications[0].topic")).toString(),
             QStringLiteral("clear-private-data"));
    QCOMPARE(evaluate(page, QStringLiteral("WebEngine.notifications[0].value")).toString(),
             QStringLiteral("cookies-and-site-data"));
    QCOMPARE(evaluate(page, QStringLiteral("WebEngine.notifications[1].value")).toString(),
             QStringLiteral("cache"));

    click(find(QStringLiteral("closeAllTabsButton")));
    QCOMPARE(m_core->tabs()->count(), 1);
    QCOMPARE(m_core->tabs()->activeUrl(), QStringLiteral("https://sailfishos.org"));
}

void tst_qmlload::cover()
{
    auto *coverItem = m_window->property("coverItem").value<QObject *>();
    QVERIFY(coverItem != nullptr);
    auto *title = coverItem->findChild<QObject *>(QStringLiteral("coverTitle"));
    QCOMPARE(title->property("text").toString(), Settings::defaultHomePage());
    m_core->tabs()->updateTitle(m_core->tabs()->activeTabId(), QStringLiteral("Home"));
    QCOMPARE(title->property("text").toString(), QStringLiteral("Home"));
    QVERIFY(coverItem->findChild<QObject *>(QStringLiteral("coverTabCount"))
                ->property("text")
                .toString()
                .startsWith(QStringLiteral("1")));

    QMetaObject::invokeMethod(coverItem->findChild<QObject *>(QStringLiteral("newTabCoverAction")),
                              "triggered");
    QCOMPARE(m_core->tabs()->count(), 2);
    QCOMPARE(m_window->property("activateCount").toInt(), 1);
}

QTEST_MAIN(tst_qmlload)
#include "tst_qmlload.moc"
