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
    config.flag("-std=c++17");
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
    println!("cargo:rerun-if-env-changed=CARGO_FEATURE_SAILFISH");
    println!("cargo:rerun-if-env-changed=SAILFISHAPP_INCLUDE_PATH");
    println!("cargo:rerun-if-changed=src");
}
