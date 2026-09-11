// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "QmlTypes.h"

#include "Core.h"

#include <QQmlEngine>

namespace Tuuli {

namespace {

const char *const ModuleUri = "harbour.tuuli";

Core *coreInstance = nullptr;

QObject *keepOwnership(QObject *object)
{
    QQmlEngine::setObjectOwnership(object, QQmlEngine::CppOwnership);
    return object;
}

QObject *tabModelProvider(QQmlEngine * /*engine*/, QJSEngine * /*scriptEngine*/)
{
    return keepOwnership(coreInstance->tabs());
}

QObject *historyModelProvider(QQmlEngine * /*engine*/, QJSEngine * /*scriptEngine*/)
{
    return keepOwnership(coreInstance->history());
}

QObject *bookmarkModelProvider(QQmlEngine * /*engine*/, QJSEngine * /*scriptEngine*/)
{
    return keepOwnership(coreInstance->bookmarks());
}

QObject *settingsProvider(QQmlEngine * /*engine*/, QJSEngine * /*scriptEngine*/)
{
    return keepOwnership(coreInstance->settings());
}

QObject *engineMessagesProvider(QQmlEngine * /*engine*/, QJSEngine * /*scriptEngine*/)
{
    return keepOwnership(coreInstance->engineMessages());
}

} // namespace

void registerQmlTypes(Core *core)
{
    static bool registered = false;
    coreInstance = core;
    if (registered) {
        return;
    }
    registered = true;
    qmlRegisterSingletonType<TabModel>(ModuleUri, 1, 0, "TabModel", &tabModelProvider);
    qmlRegisterSingletonType<HistoryModel>(ModuleUri, 1, 0, "HistoryModel", &historyModelProvider);
    qmlRegisterSingletonType<BookmarkModel>(ModuleUri, 1, 0, "BookmarkModel",
                                            &bookmarkModelProvider);
    qmlRegisterSingletonType<Settings>(ModuleUri, 1, 0, "Settings", &settingsProvider);
    qmlRegisterSingletonType<EngineMessages>(ModuleUri, 1, 0, "EngineMessages",
                                             &engineMessagesProvider);
}

} // namespace Tuuli
