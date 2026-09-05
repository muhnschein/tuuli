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
    #include <QtCore/QUrl>
    #include <QtGui/QGuiApplication>
    #include <QtQml/QQmlEngine>
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
        std::env::set_var("PULSE_PROP_application.process.binary", "tuuli-browser");
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
    cpp!(unsafe [argc as "int", argv_ptr as "char **"] -> *mut c_void as "QGuiApplication *" {
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
        // dirs are the ones the sandbox permits.
        QCoreApplication::setOrganizationName(QStringLiteral("org.tuuli"));
        QCoreApplication::setApplicationName(QStringLiteral("browser"));
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
    // On Sailfish the QML is installed under /usr/share/tuuli-browser/;
    // elsewhere it is read from the source tree (or TUULI_QML_DIR).
    let installed: QString = cpp!(unsafe [] -> QString as "QString" {
        #ifdef TUULI_SAILFISH
        return SailfishApp::pathTo(QStringLiteral("qml/tuuli-browser.qml")).toString();
        #else
        return QString();
        #endif
    });
    if !installed.is_empty() {
        return installed;
    }
    let dir = std::env::var("TUULI_QML_DIR")
        .unwrap_or_else(|_| concat!(env!("CARGO_MANIFEST_DIR"), "/../../src/qml").to_string());
    QString::from(format!("file://{dir}/tuuli-browser.qml"))
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
pub fn run(engine: Rc<dyn Engine>, args: Vec<String>) -> i32 {
    let _ = env_logger::try_init();
    let paths = AppPaths::xdg();
    let prefs = Preferences::load(&paths.prefs_file());
    apply_early_environment(&prefs);

    let app = create_application(&args);
    tuuli_qml::register_types();
    if let Err(e) = tuuli_qml::install(engine, paths, args) {
        eprintln!("tuuli-browser: {e}");
        return 1;
    }
    if let Some(proxy) = tuuli_qml::platform::connman_read_proxy() {
        tuuli_qml::with_core(|b| b.set_proxy(proxy));
        tuuli_qml::pump();
    }

    let view = create_view();
    tuuli_qml::platform::add_image_provider(view_engine(view));
    show_view(view, main_qml_url());
    exec_application(app)
}
