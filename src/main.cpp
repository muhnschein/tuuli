// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 tuuli contributors
#include "Core.h"
#include "QmlTypes.h"

#include <QGuiApplication>
#include <QLocale>
#include <QQuickView>
#include <QScopedPointer>
#include <QTranslator>
#include <sailfishapp.h>

// Exported so the Silica booster can dlopen() the binary and call main().
Q_DECL_EXPORT int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    app->setApplicationVersion(QStringLiteral(TUULI_VERSION));

    QTranslator translator;
    const QString translationDir =
        SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
    if (translator.load(QLocale(), QStringLiteral("harbour-tuuli"), QStringLiteral("-"),
                        translationDir)) {
        QGuiApplication::installTranslator(&translator);
    }

    Tuuli::Core core(Tuuli::Storage::defaultDataDirectory(),
                     Tuuli::Storage::defaultConfigFilePath());
    Tuuli::registerQmlTypes(&core);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->setSource(SailfishApp::pathToMainQml());
    view->show();
    return QGuiApplication::exec();
}
