// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "sailfishapp.h"

#include <QGuiApplication>
#include <QQuickView>

namespace SailfishApp {

QGuiApplication *application(int &argc, char **argv)
{
    auto *app = new QGuiApplication(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("io.github.muhnschein"));
    QGuiApplication::setApplicationName(QStringLiteral("tuuli"));
    return app;
}

QQuickView *createView()
{
    return new QQuickView;
}

QUrl pathTo(const QString &filename)
{
    return QUrl::fromLocalFile(QStringLiteral(TUULI_SOURCE_DIR "/") + filename);
}

QUrl pathToMainQml()
{
    return pathTo(QStringLiteral("qml/harbour-tuuli.qml"));
}

} // namespace SailfishApp
