// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! tuuli-browser with the in-process mock engine: host builds, the
//! emulator, and UI iteration on a device without a libservo build.  The
//! Servo-linked binary is servo/app.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let engine = tuuli_core::mock::MockEngine::new();
    std::process::exit(tuuli_browser::run(engine, args));
}
