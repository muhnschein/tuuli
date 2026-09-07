// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The application shell: creates the (Sailfish) application and view,
//! installs the engine into the QML layer and runs the event loop.  The
//! mock binary (`src/main.rs`) and the Servo-linked binary (`servo/app`)
//! both call [`run`].

#![recursion_limit = "1024"]

use std::ffi::{c_void, CString};
use std::os::raw::c_char;
use std::rc::Rc;

use cpp::cpp;
use qttypes::QString;
use tuuli_core::engine::Engine;
use tuuli_core::paths::AppPaths;
use tuuli_core::prefs::Preferences;

cpp! {{
    #include <QtCore/QCoreApplication>
    #include <QtCore/QLocale>
    #include <QtCore/QTranslator>
    #include <QtCore/QDir>
    #include <QtCore/QUrl>
    #include <QtGui/QGuiApplication>
    #include <QtQml/QQmlEngine>
    #include <QtQml/QQmlComponent>
    #include <QtQuick/QQuickView>
    #ifdef TUULI_SAILFISH
    #include <sailfishapp.h>
    #endif

    static int s_argc = 0;
    static char **s_argv = nullptr;
}}

/// Environment that must be in place before the QGuiApplication exists.
fn apply_early_environment(prefs: &Preferences) {
    // libservo's types are single-thread only: the scene graph renders on
    // the GUI thread (docs/ARCHITECTURE.md).  Not a developer toggle.
    std::env::set_var("QSG_RENDER_LOOP", "basic");
    // Sailfish audio policy: a media role so playback ducks and pauses on
    // calls (spec 8.2).
    if std::env::var_os("PULSE_PROP_media.role").is_none() {
        std::env::set_var("PULSE_PROP_media.role", "x-maemo");
    }
    if std::env::var_os("PULSE_PROP_application.process.binary").is_none() {
        std::env::set_var(
            "PULSE_PROP_application.process.binary",
            tuuli_core::paths::APPLICATION,
        );
    }
    if prefs.engine_logging && std::env::var_os("RUST_LOG").is_none() {
        std::env::set_var("RUST_LOG", "info");
    }
}

fn create_application(args: &[String]) -> *mut c_void {
    let cargs: Vec<CString> = args
        .iter()
        .map(|a| CString::new(a.as_str()).unwrap_or_default())
        .collect();
    let mut argv: Vec<*mut c_char> = cargs.iter().map(|c| c.as_ptr() as *mut c_char).collect();
    argv.push(std::ptr::null_mut());
    let argc = cargs.len() as i32;
    let argv_ptr = argv.as_mut_ptr();
    let org = QString::from(tuuli_core::paths::ORGANIZATION);
    let app_name = QString::from(tuuli_core::paths::APPLICATION);
    cpp!(unsafe [argc as "int", argv_ptr as "char **", org as "QString", app_name as "QString"] -> *mut c_void as "QGuiApplication *" {
        // QGuiApplication keeps referring to argc/argv: copy them.
        s_argc = argc;
        s_argv = new char *[argc + 1];
        for (int i = 0; i < argc; ++i) s_argv[i] = strdup(argv_ptr[i]);
        s_argv[argc] = nullptr;
        #ifdef TUULI_SAILFISH
        QGuiApplication *app = SailfishApp::application(s_argc, s_argv);
        #else
        QGuiApplication *app = new QGuiApplication(s_argc, s_argv);
        #endif
        // Must match the sailjail profile (spec 9.1) so data/config/cache
        // dirs are the ones the sandbox permits: both are the package name,
        // which is also what SailfishApp::application() sets
        // (crates/tuuli-core/src/paths.rs, ci/harbour-check.sh 2.5).
        QCoreApplication::setOrganizationName(org);
        QCoreApplication::setApplicationName(app_name);

        // The catalogs (translations/): the engineering English one, which
        // turns qsTrId() ids into the //% texts, then the locale's on top.
        // libsailfishapp installs the same pair itself; doing it here too
        // costs nothing and keeps a host build readable.
        QString translations;
        #ifdef TUULI_SAILFISH
        translations = SailfishApp::pathTo(QStringLiteral("translations")).toLocalFile();
        #else
        translations = QString::fromLocal8Bit(qgetenv("TUULI_TRANSLATIONS_DIR"));
        #endif
        if (!translations.isEmpty() && QDir(translations).exists()) {
            QTranslator *engineering = new QTranslator(app);
            if (engineering->load(app_name, translations)) app->installTranslator(engineering);
            QTranslator *locale = new QTranslator(app);
            if (locale->load(QLocale(), app_name, QStringLiteral("-"), translations)) app->installTranslator(locale);
        }
        QObject::connect(app, &QGuiApplication::applicationStateChanged, [](Qt::ApplicationState state) {
            int s = int(state);
            rust!(Tuuli_appStateChanged [s: i32 as "int"] { on_application_state(s) });
        });
        QObject::connect(app, &QCoreApplication::aboutToQuit, []() {
            rust!(Tuuli_aboutToQuit [] { on_about_to_quit() });
        });
        return app;
    })
}

fn on_application_state(state: i32) {
    // Qt::ApplicationActive == 4; anything else is a backgrounding (spec 8.4).
    if state != 4 {
        tuuli_qml::with_core_opt(|b| b.on_application_inactive());
    }
}

fn on_about_to_quit() {
    tuuli_qml::with_core_opt(|b| b.on_about_to_quit());
}

fn create_view() -> *mut c_void {
    cpp!(unsafe [] -> *mut c_void as "QQuickView *" {
        #ifdef TUULI_SAILFISH
        return SailfishApp::createView();
        #else
        QQuickView *view = new QQuickView();
        view->setResizeMode(QQuickView::SizeRootObjectToView);
        view->resize(540, 1130);
        return view;
        #endif
    })
}

fn view_engine(view: *mut c_void) -> *mut c_void {
    cpp!(unsafe [view as "QQuickView *"] -> *mut c_void as "QQmlEngine *" { return view->engine(); })
}

fn main_qml_url() -> QString {
    // On Sailfish the QML is installed under /usr/share/harbour-tuuli/
    // (the package's own data directory, docs/HARBOUR.md); elsewhere it is
    // read from the source tree (or TUULI_QML_DIR).
    let installed: QString = cpp!(unsafe [] -> QString as "QString" {
        #ifdef TUULI_SAILFISH
        return SailfishApp::pathTo(QStringLiteral("qml/harbour-tuuli.qml")).toString();
        #else
        return QString();
        #endif
    });
    if !installed.is_empty() {
        return installed;
    }
    let dir = std::env::var("TUULI_QML_DIR")
        .unwrap_or_else(|_| concat!(env!("CARGO_MANIFEST_DIR"), "/../../src/qml").to_string());
    QString::from(format!("file://{dir}/harbour-tuuli.qml"))
}

fn show_view(view: *mut c_void, url: QString) {
    cpp!(unsafe [view as "QQuickView *", url as "QString"] {
        view->setSource(QUrl(url));
        view->show();
    });
}

fn exec_application(app: *mut c_void) -> i32 {
    cpp!(unsafe [app as "QGuiApplication *"] -> i32 as "int" { return app->exec(); })
}

/// Runs the browser with `engine` and returns the process exit code.
/// Startup progress, on stderr, unconditionally.
///
/// A device launch has no other way to say where it stopped.  The journal
/// shows the Qt platform plugin's line and then whatever the app prints,
/// and until this existed the app printed nothing at all before its first
/// frame -- so a launch that died in QML loading, in engine creation or in
/// a static initialiser all looked identical, and identically silent.  The
/// last stage printed is the one that did not finish.
///
/// Deliberately not behind a level or an environment variable: sailjail
/// does not promise to carry either into the sandbox, and this has to work
/// from the app grid, where there is no shell to set one.
fn stage(what: &str) {
    eprintln!("tuuli: startup {what}");
}

pub fn run(engine: Rc<dyn Engine>, args: Vec<String>) -> i32 {
    let _ = env_logger::try_init();
    stage(&format!(
        "begin, engine {} {}",
        engine.name(),
        engine.version()
    ));
    let paths = AppPaths::xdg();
    let prefs = Preferences::load(&paths.prefs_file());
    apply_early_environment(&prefs);
    stage("preferences read");

    let app = create_application(&args);
    stage("QGuiApplication up");
    tuuli_qml::register_types();
    stage("QML types registered");
    if let Err(e) = tuuli_qml::install(engine, paths, args) {
        eprintln!("harbour-tuuli: {e}");
        return 1;
    }
    stage("browser object installed");
    if let Some(proxy) = tuuli_qml::platform::connman_read_proxy() {
        tuuli_qml::with_core(|b| b.set_proxy(proxy));
        tuuli_qml::pump();
    }

    let view = create_view();
    tuuli_qml::platform::add_image_provider(view_engine(view));
    stage("view created");
    show_view(view, main_qml_url());
    stage("QML shown, entering the event loop");
    let code = exec_application(app);
    stage(&format!("event loop returned {code}"));
    code
}

/// Test support: instantiate the QML document at `url` in `engine` (a
/// `QQmlEngine *`) and report the component's errors, or `Ok` when the
/// root object was created.  This is how `tests/qml_loads.rs` proves every
/// file of the chrome loads against the Silica stubs in `tests/silica-stubs`,
/// which a device would otherwise be the first to find out.
pub fn probe_qml(engine: *mut c_void, url: &str) -> Result<(), String> {
    let url = QString::from(url);
    let error: QString = cpp!(unsafe [engine as "QQmlEngine *", url as "QString"] -> QString as "QString" {
        QQmlComponent component(engine, QUrl(url));
        if (component.isError()) return component.errorString();
        QObject *object = component.create();
        if (!object) return component.errorString();
        delete object;
        return QString();
    });
    if error.is_empty() {
        Ok(())
    } else {
        Err(error.to_string())
    }
}
