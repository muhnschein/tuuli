// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tuuli's Qt/QML layer: the `import Tuuli 1.0` types over
//! [`tuuli_core`], built with qmetaobject-rs.
//!
//! The only C++ in the project lives in this crate's `cpp!` blocks: the
//! `QQuickFramebufferObject` subclass that hands the engine an FBO
//! ([`webview`]), the image provider and the D-Bus clients ([`platform`]).
//!
//! Everything runs on the Qt GUI thread with the basic scene-graph render
//! loop (`tuuli_browser::run` sets `QSG_RENDER_LOOP=basic`), see
//! `docs/ARCHITECTURE.md`.

#![recursion_limit = "1024"]
// `#[derive(QObject)]` transmutes references for object-typed properties.
#![allow(clippy::useless_transmute)]

pub mod core;
pub mod objects;
pub mod platform;
pub mod webview;

pub use core::{install, pump, with_core, with_core_opt};

use cstr::cstr;
use qmetaobject::prelude::*;
use qmetaobject::qml_register_singleton_type;

/// Registers every `Tuuli 1.0` type.  Call once before loading QML.
pub fn register_types() {
    qml_register_singleton_type::<objects::BrowserObject>(cstr!("Tuuli"), 1, 0, cstr!("Browser"));
    qml_register_type::<webview::WebViewItem>(cstr!("Tuuli"), 1, 0, cstr!("WebView"));
    qml_register_type::<objects::TabObject>(cstr!("Tuuli"), 1, 0, cstr!("Tab"));
    qml_register_type::<objects::TabModel>(cstr!("Tuuli"), 1, 0, cstr!("TabModel"));
    qml_register_type::<objects::HistoryModel>(cstr!("Tuuli"), 1, 0, cstr!("HistoryModel"));
    qml_register_type::<objects::BookmarkModel>(cstr!("Tuuli"), 1, 0, cstr!("BookmarkModel"));
    qml_register_type::<objects::DownloadModel>(cstr!("Tuuli"), 1, 0, cstr!("DownloadManager"));
    qml_register_type::<objects::PermissionsObject>(cstr!("Tuuli"), 1, 0, cstr!("PermissionStore"));
    qml_register_type::<objects::PrefsObject>(cstr!("Tuuli"), 1, 0, cstr!("Preferences"));
    qml_register_type::<objects::ClipboardObject>(cstr!("Tuuli"), 1, 0, cstr!("Clipboard"));
    qml_register_type::<objects::InputMethodProxyObject>(
        cstr!("Tuuli"),
        1,
        0,
        cstr!("InputMethodProxy"),
    );
    qml_register_type::<objects::PermissionRequestObject>(
        cstr!("Tuuli"),
        1,
        0,
        cstr!("PermissionRequest"),
    );
    qml_register_type::<objects::DialogRequestObject>(
        cstr!("Tuuli"),
        1,
        0,
        cstr!("SimpleDialogRequest"),
    );
}
