// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "enterkey.h"
#include "enums.h"

#include <QQmlExtensionPlugin>
#include <QtQml>

class SilicaStubsPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QQmlExtensionInterface")

public:
    void registerTypes(const char *uri) override
    {
        const QString reason = QStringLiteral("enum holder");
        qmlRegisterUncreatableType<EnterKey>(uri, 1, 0, "EnterKey",
                                             QStringLiteral("attached property"));
        qmlRegisterUncreatableType<Orientation>(uri, 1, 0, "Orientation", reason);
        qmlRegisterUncreatableType<PageStatus>(uri, 1, 0, "PageStatus", reason);
        qmlRegisterUncreatableType<TruncationMode>(uri, 1, 0, "TruncationMode", reason);
    }
};

#include "plugin.moc"
