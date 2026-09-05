// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Loads the `Tuuli 1.0` types into a bare QML engine (no Silica) with the
//! mock engine and drives the Browser singleton and a WebView item,
//! including the real QQuickFramebufferObject render path when a display
//! is available (CI runs this under Xvfb with Mesa).

use std::time::Duration;

use qmetaobject::qtcore::core_application::QCoreApplication;
use qmetaobject::{single_shot, QQuickView};
use qttypes::QString;
use tuuli_core::mock::MockEngine;
use tuuli_core::paths::AppPaths;
use tuuli_core::Engine;

#[test]
fn singleton_and_webview_work() {
    // What tuuli_browser::run() does before creating the application: the
    // engine lives on the GUI thread, so the scene graph must render there.
    std::env::set_var("QSG_RENDER_LOOP", "basic");
    let dir = tempfile::tempdir().unwrap();
    let paths = AppPaths::under(dir.path());
    let engine = MockEngine::new();
    tuuli_qml::register_types();
    tuuli_qml::install(engine.clone(), paths, vec!["tuuli-browser".into()]).unwrap();

    let qml = r#"
import QtQuick 2.2
import Tuuli 1.0
Item {
    width: 540; height: 1130
    property int tabCount: Browser.tabs.count
    WebView { id: wv; anchors.fill: parent; tab: Browser.tabs.currentTab }
    Component.onCompleted: {
        Browser.tabs.newTab("https://example.org/", false, true)
        Browser.prefs.sendDoNotTrack = false
    }
}
"#;
    let qml_path = dir.path().join("smoke.qml");
    std::fs::write(&qml_path, qml).unwrap();

    let headless =
        std::env::var_os("DISPLAY").is_none() && std::env::var_os("WAYLAND_DISPLAY").is_none();
    if headless {
        // No window: exercise the wiring without the scene graph.
        engine.initialize_for_tests();
        tuuli_qml::with_core(|b| b.open_url("https://example.org/", false, true));
        tuuli_qml::pump();
        engine.spin_event_loop();
        tuuli_qml::pump();
        tuuli_qml::with_core(|b| {
            assert_eq!(b.tabs.borrow().len(), 1);
            assert_eq!(b.tabs.borrow().current().unwrap().title, "example.org");
            assert_eq!(b.history.total_count(), 1);
        });
        return;
    }

    let mut view = QQuickView::new();
    tuuli_qml::platform::add_image_provider(view.engine().cpp_ptr());
    view.set_source(QString::from(format!("file://{}", qml_path.display())));
    view.show();
    let engine2 = engine.clone();
    single_shot(Duration::from_millis(2500), move || {
        tuuli_qml::with_core(|b| {
            let tabs = b.tabs.borrow();
            assert_eq!(tabs.len(), 1, "QML created a tab");
            assert!(b.engine.is_initialized(), "render initialised the engine");
            let tab = tabs.current().expect("current tab");
            assert!(tab.has_webview(), "current tab got a webview");
            assert_eq!(tab.title, "example.org", "mock load completed through spin");
            assert!(!b.prefs.send_do_not_track, "QML wrote a preference");
            drop(tabs);
            assert_eq!(b.history.total_count(), 1, "history recorded the visit");
            assert!(b.session.exists());
        });
        let painted = engine2
            .webviews()
            .iter()
            .any(|w| w.state.borrow().paint_count > 0);
        assert!(painted, "the FBO renderer painted the webview");
        QCoreApplication::quit();
    });
    view.engine().exec();
}
