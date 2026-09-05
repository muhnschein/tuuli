// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::PathBuf;

/// The qmetaobject crate ships the C++ support header its `RustObject<T>`
/// subclasses need.  The workspace builds against the vendored copy in
/// third_party/ (docs/HARBOUR.md); the registry lookup is for a build that
/// has dropped the `[patch]`.
fn qmetaobject_include_dir() -> PathBuf {
    let manifest = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap());
    let vendored = manifest.join("../../third_party/qmetaobject");
    if vendored.join("qmetaobject_rust.hpp").exists() {
        return vendored;
    }
    let fallback = vendored;
    let Some(home) = std::env::var_os("CARGO_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|h| PathBuf::from(h).join(".cargo")))
    else {
        return fallback;
    };
    let registry = home.join("registry").join("src");
    let Ok(indexes) = std::fs::read_dir(&registry) else {
        return fallback;
    };
    for index in indexes.flatten() {
        let Ok(crates) = std::fs::read_dir(index.path()) else {
            continue;
        };
        for c in crates.flatten() {
            let name = c.file_name().to_string_lossy().to_string();
            if name.starts_with("qmetaobject-0.2.")
                && c.path().join("qmetaobject_rust.hpp").exists()
            {
                return c.path();
            }
        }
    }
    fallback
}

fn main() {
    let qt_include_path =
        std::env::var("DEP_QT_INCLUDE_PATH").expect("qttypes exports DEP_QT_INCLUDE_PATH");
    let qt_library_path =
        std::env::var("DEP_QT_LIBRARY_PATH").expect("qttypes exports DEP_QT_LIBRARY_PATH");
    let mut config = cpp_build::Config::new();
    for f in std::env::var("DEP_QT_COMPILE_FLAGS")
        .unwrap_or_default()
        .split_terminator(';')
    {
        config.flag(f);
    }
    config.flag("-std=gnu++14");
    config.include(qmetaobject_include_dir());
    config.include(&qt_include_path);
    for module in ["QtCore", "QtGui", "QtQml", "QtQuick", "QtDBus"] {
        config.include(format!("{qt_include_path}/{module}"));
    }
    config.build("src/lib.rs");
    println!("cargo:rustc-link-search={qt_library_path}");
    println!("cargo:rustc-link-lib=Qt5DBus");

    // On a GLES build of Qt (the Sailfish target), QOpenGLFunctions calls
    // the GLES 2 entry points directly instead of through resolved
    // pointers, so the renderer's glClear/glBindFramebuffer need
    // libGLESv2 at link time; a desktop-GL Qt (the host) needs nothing.
    // Read off Qt's own configuration: qconfig.h (Qt < 5.8) defines
    // QT_OPENGL_ES_2, qtgui-config.h (Qt >= 5.8) sets the feature.
    // TUULI_LINK_GLESV2=1 forces it (the spec and servo/build.sh set it).
    let gles = std::env::var("TUULI_LINK_GLESV2")
        .map(|v| v == "1")
        .unwrap_or(false)
        || ["QtCore/qconfig.h", "QtGui/qtgui-config.h"]
            .iter()
            .any(|h| {
                std::fs::read_to_string(format!("{qt_include_path}/{h}"))
                    .map(|s| {
                        s.contains("#define QT_OPENGL_ES_2") || s.contains("QT_FEATURE_opengles2 1")
                    })
                    .unwrap_or(false)
            });
    if gles {
        println!("cargo:rustc-link-lib=GLESv2");
    }
    println!("cargo:rerun-if-env-changed=TUULI_LINK_GLESV2");
    println!("cargo:rerun-if-changed=../../third_party/qmetaobject/qmetaobject_rust.hpp");
    // cpp_build extracts the C++ from the sources: any edit must re-run it.
    println!("cargo:rerun-if-changed=src");
}
