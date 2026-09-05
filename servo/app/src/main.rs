// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! harbour-tuuli with the libservo engine.  Everything but the engine
//! factory is `tuuli_browser::run`.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let engine = if args.iter().any(|a| a == "--mock-engine") || std::env::var_os("TUULI_ENGINE").map(|v| v == "mock").unwrap_or(false) {
        tuuli_core::mock::MockEngine::new() as std::rc::Rc<dyn tuuli_core::engine::Engine>
    } else {
        tuuli_servo_backend::create_engine()
    };
    std::process::exit(tuuli_browser::run(engine, args));
}
