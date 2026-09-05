// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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
    // gnu++14: the SDK target's GCC and Qt 5.6 headers are happiest there.
    config.flag("-std=gnu++14");
    config.include(&qt_include_path);
    for module in ["QtCore", "QtGui", "QtQml", "QtQuick"] {
        config.include(format!("{qt_include_path}/{module}"));
    }
    let sailfish = std::env::var("CARGO_FEATURE_SAILFISH").is_ok();
    if sailfish {
        config.flag("-DTUULI_SAILFISH=1");
        // Cross builds (servo/build.sh) point this at the SDK target root.
        let include = std::env::var("SAILFISHAPP_INCLUDE_PATH")
            .unwrap_or_else(|_| "/usr/include/sailfishapp".into());
        config.include(include);
        println!("cargo:rustc-link-lib=sailfishapp");
    }
    config.build("src/lib.rs");
    println!("cargo:rustc-link-search={qt_library_path}");
    harbour_link_args();
    println!("cargo:rerun-if-env-changed=CARGO_FEATURE_SAILFISH");
    println!("cargo:rerun-if-env-changed=SAILFISHAPP_INCLUDE_PATH");
    println!("cargo:rerun-if-changed=src");
}

/// Two link arguments Harbour's validator insists on (docs/HARBOUR.md).
///
/// `--dynamic-list`: the silica-qt5 booster `dlopen()`s the binary and looks
/// `main` up dynamically, and rpmbuild strips `.symtab`, so `main` has to be
/// in `.dynsym`.  `--dynamic-list` rather than `--export-dynamic-symbol`,
/// which needs binutils 2.35, or `--export-dynamic`, which exports everything.
///
/// `--as-needed`: qttypes links Qt5Widgets unconditionally; with the vendored
/// qmetaobject no longer using QApplication nothing refers to it, and this
/// drops the DT_NEEDED entry that would fail the allowed-libraries check.
fn harbour_link_args() {
    let list = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("main.dynlist");
    println!("cargo:rerun-if-changed={}", list.display());
    println!(
        "cargo:rustc-link-arg-bins=-Wl,--dynamic-list={}",
        list.display()
    );
    println!("cargo:rustc-link-arg-bins=-Wl,--as-needed");
}
