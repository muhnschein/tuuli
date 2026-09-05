// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The same two link arguments as crates/tuuli-browser/build.rs, for the
//! Servo-linked binary: `main` exported for the silica-qt5 booster, and
//! `--as-needed` so Qt5Widgets is not recorded as a dependency
//! (docs/HARBOUR.md).

fn main() {
    let list = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("main.dynlist");
    println!("cargo:rerun-if-changed={}", list.display());
    println!(
        "cargo:rustc-link-arg-bins=-Wl,--dynamic-list={}",
        list.display()
    );
    println!("cargo:rustc-link-arg-bins=-Wl,--as-needed");
}
