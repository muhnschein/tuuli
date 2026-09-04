/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "tuuliplugin.h"

#include "browsercontext.h"
#include "engine/engine.h"
#include "input/inputmethodproxy.h"
#include "model/bookmarkmodel.h"
#include "model/downloadmanager.h"
#include "model/historymodel.h"
#include "model/permissionstore.h"
#include "model/tab.h"
#include "model/tabmodel.h"
#include "platform/clipboardbridge.h"
#include "prefs/preferences.h"
#include "view/imageprovider.h"
#include "view/tuuliwebview.h"
#include <QQmlEngine>
#include <qqml.h>

using namespace Tuuli;

static QObject* browserSingletonProvider(QQmlEngine* qml, QJSEngine*)
{
    BrowserContext* ctx = BrowserContext::ensureCreated();
    if (qml && !qml->imageProvider(QLatin1String(TuuliImageProvider::providerId())))
        qml->addImageProvider(QLatin1String(TuuliImageProvider::providerId()), ctx->imageProvider());
    QQmlEngine::setObjectOwnership(ctx, QQmlEngine::CppOwnership);
    return ctx;
}

void TuuliPlugin::registerTypes(const char* uri)
{
    Q_ASSERT(QLatin1String(uri) == QLatin1String("Tuuli"));
    qmlRegisterType<TuuliWebView>(uri, 1, 0, "WebView");
    qmlRegisterSingletonType<BrowserContext>(uri, 1, 0, "Browser", browserSingletonProvider);
    qmlRegisterUncreatableType<Tab>(uri, 1, 0, "Tab", QStringLiteral("Created by Browser.tabs"));
    qmlRegisterUncreatableType<TabModel>(uri, 1, 0, "TabModel", QStringLiteral("Use Browser.tabs"));
    qmlRegisterUncreatableType<HistoryModel>(uri, 1, 0, "HistoryModel", QStringLiteral("Use Browser.history"));
    qmlRegisterUncreatableType<BookmarkModel>(uri, 1, 0, "BookmarkModel", QStringLiteral("Use Browser.bookmarks"));
    qmlRegisterUncreatableType<DownloadManager>(uri, 1, 0, "DownloadManager", QStringLiteral("Use Browser.downloads"));
    qmlRegisterUncreatableType<PermissionStore>(uri, 1, 0, "PermissionStore", QStringLiteral("Use Browser.permissions"));
    qmlRegisterUncreatableType<Preferences>(uri, 1, 0, "Preferences", QStringLiteral("Use Browser.prefs"));
    qmlRegisterUncreatableType<ClipboardBridge>(uri, 1, 0, "Clipboard", QStringLiteral("Use Browser.clipboard"));
    qmlRegisterUncreatableType<InputMethodProxy>(uri, 1, 0, "InputMethodProxy", QStringLiteral("Use WebView.inputMethod"));
    qmlRegisterUncreatableType<PermissionRequest>(uri, 1, 0, "PermissionRequest", QStringLiteral("Delivered by Browser"));
    qmlRegisterUncreatableType<SimpleDialogRequest>(uri, 1, 0, "SimpleDialogRequest", QStringLiteral("Delivered by Browser"));
    qmlRegisterUncreatableType<DownloadRequest>(uri, 1, 0, "DownloadRequest", QStringLiteral("Delivered by Browser"));
}

void TuuliPlugin::initializeEngine(QQmlEngine* engine, const char* uri)
{
    QQmlExtensionPlugin::initializeEngine(engine, uri);
}
