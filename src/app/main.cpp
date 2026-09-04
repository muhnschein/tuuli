/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * tuuli-browser entry point.  Deliberately thin: the Tuuli QML plugin
 * creates the engine and the Browser singleton on first import, so the
 * same binary runs with the Servo engine or the mock one.
 */

#include <QGuiApplication>
#include <QQuickView>
#include <QScopedPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QtQml>

#include <cstdlib>

#ifdef TUULI_HAVE_SAILFISHAPP
#include <sailfishapp.h>
#endif

static void applyEarlyEnvironment()
{
    // Sailfish audio policy: register as a media role so playback ducks and
    // pauses on calls (spec 8.2).  Must precede QGuiApplication.
    setenv("PULSE_PROP_media.role", "x-maemo", 0);
    setenv("PULSE_PROP_application.process.binary", "tuuli-browser", 0);

    // Developer fallback (spec 4.2, docs/ARCHITECTURE.md): run the scene graph
    // single-threaded so Servo and Qt share the GUI thread.
    QCoreApplication::setOrganizationName(QStringLiteral("org.tuuli"));
    QCoreApplication::setApplicationName(QStringLiteral("browser"));
    const QString conf = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/tuuli.conf");
    QSettings settings(conf, QSettings::IniFormat);
    if (settings.value(QStringLiteral("developer/basicRenderLoop"), false).toBool())
        setenv("QSG_RENDER_LOOP", "basic", 0);
    if (settings.value(QStringLiteral("developer/engineLogging"), false).toBool())
        setenv("RUST_LOG", "info", 0);
}

int main(int argc, char* argv[])
{
    applyEarlyEnvironment();

#ifdef TUULI_HAVE_SAILFISHAPP
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
#else
    QScopedPointer<QGuiApplication> app(new QGuiApplication(argc, argv));
#endif
    // Must match the sailjail profile (spec 9.1) so data/config/cache dirs
    // are the ones the sandbox permits.
    app->setOrganizationName(QStringLiteral("org.tuuli"));
    app->setApplicationName(QStringLiteral("browser"));
    app->setApplicationVersion(QStringLiteral(TUULI_VERSION));

#ifdef TUULI_HAVE_SAILFISHAPP
    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/tuuli-browser.qml")));
#else
    QScopedPointer<QQuickView> view(new QQuickView);
    view->setResizeMode(QQuickView::SizeRootObjectToView);
    view->engine()->addImportPath(QStringLiteral(TUULI_QML_IMPORT_PATH));
    view->setSource(QUrl::fromLocalFile(QStringLiteral(TUULI_QML_DIR "/tuuli-browser.qml")));
    view->resize(540, 1130);
#endif
    view->show();
    return app->exec();
}
