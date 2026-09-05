// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::path::PathBuf;

/// The qmetaobject crate ships the C++ support header its `RustObject<T>`
/// subclasses need.  Prefer the header of the crate cargo resolved; fall
/// back to the copy in cpp/ (vendored / offline builds).
fn qmetaobject_include_dir() -> PathBuf {
    let fallback = PathBuf::from(std::env::var("CARGO_MANIFEST_DIR").unwrap()).join("cpp");
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
    config.flag("-std=c++17");
    config.include(qmetaobject_include_dir());
    config.include(&qt_include_path);
    for module in ["QtCore", "QtGui", "QtQml", "QtQuick", "QtDBus"] {
        config.include(format!("{qt_include_path}/{module}"));
    }
    config.build("src/lib.rs");
    println!("cargo:rustc-link-search={qt_library_path}");
    println!("cargo:rustc-link-lib=Qt5DBus");
    println!("cargo:rerun-if-changed=cpp/qmetaobject_rust.hpp");
    // cpp_build extracts the C++ from the sources: any edit must re-run it.
    println!("cargo:rerun-if-changed=src");
}
