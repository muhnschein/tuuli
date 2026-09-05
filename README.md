# Tuuli

A Servo-based web browser for Sailfish OS, written in Rust.  A Silica QML
chrome drives a Rust core that embeds the Servo engine through libservo's
own Rust API; the Qt objects the QML sees are Rust structs exported with
qmetaobject-rs.  C++ is limited to the scene-graph node that paints the
engine's framebuffer, and lives inside `cpp!` blocks in the Rust sources.

Tuuli is a **second browser**.  It ships alongside Sailfish Browser and does
not replace it.  Read [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) before
using it for anything that matters: web content is not sandboxed from the
application.

| | |
|---|---|
| Target | Sailfish OS 5.2+, aarch64, Jolla Phone (2026) and newer |
| Engine | Servo, pinned release ([servo/SERVO_TAG](servo/SERVO_TAG)), consumed as the `libservo` crate, MPL-2.0 |
| Toolkit | Qt 5.6 + Silica through qmetaobject-rs (no newer Qt API is used anywhere) |
| Language | Rust 2021, `rust-version = 1.80` for the workspace; Servo's pinned toolchain for the engine build |
| Licence | MPL-2.0 |
| Distribution | Jolla's Harbour store ([docs/HARBOUR.md](docs/HARBOUR.md)); the package is `harbour-tuuli` |
| Spec | [docs/spec.md](docs/spec.md) (amended for the Rust design: §3.3, §4, §12, §13, §15) |

## Status

This tree implements the spec's M0–M2 structure plus the M3 pieces that
are pure logic, and ships the whole thing with an in-process **mock
engine** so the chrome, models and Qt layer can be built, unit-tested and
iterated on any Linux host and in the emulator without a Servo build.

What exists and is tested on the host:

- the engine seam (`tuuli_core::engine::{Engine, WebView,
  RenderingContext}`) that every layer above the engine is written against
  (spec 4.1), with the `MockEngine` behind it;
- the libservo backend (`servo/backend`), written against the pinned
  tag's `ServoBuilder` / `WebViewBuilder` / delegate API and type-checked
  by a separate CI job so host builds never clone Servo;
- the `QQuickFramebufferObject` item (`WebView` in QML), its renderer and
  the rendering-context wrapper around Qt's scene-graph context (spec 5.1,
  5.2), exercised under Xvfb + Mesa;
- input: DPR maths, touch conversion, gesture arbitration (lipstick
  edges, bottom-edge toolbar reveal, long-press without movement, pulley
  handoff at content edges), Maliit text-input proxy with edit-to-key
  planning (spec 6);
- tabs, session persistence with 5 s debounce and crash detection, history
  and bookmarks (SQLite), per-origin permissions, downloads via Transfer
  Engine, clipboard, connman proxy, search engines, preferences with the
  spec 9.4 defaults, cosmetic filtering from EasyList-style lists;
- the complete Silica chrome: start view, page view with pulleys and
  auto-hiding toolbar, tab overview, settings, downloads, history, page
  info, permissions, about, cover;
- Harbour packaging: one `harbour-tuuli` package built either with the
  mock engine (inside the SDK, with its Rust 1.75) or with Servo
  (cross-compiled against the SDK target root); a source-tree check of
  Harbour's rules on every push, Jolla's own validator on every RPM, and
  a GitHub Actions workflow that builds the RPM on demand; perf-budget
  and screenshot tooling for the ten-page corpus.

Verified on a Jolla Phone (2026) with the mock-engine RPM from the `rpm`
workflow: the chrome, the sailjail profile, the data paths, the
translations, and the basic render loop under the booster.  Jolla's
Harbour validator accepts that package.

What does **not** exist yet, because it needs the engine build (M0 exit
criteria, [docs/M0-CHECKLIST.md](docs/M0-CHECKLIST.md)):

- a Servo-linked `harbour-tuuli` built for the SFOS 5.2 aarch64 sysroot
  (the `rpm.yml` workflow with engine `servo` is the spike);
- proof that WebRender's shaders run on Mali-G610 through libhybris;
- reconciliation of `servo/backend` against what the pinned tag's
  `libservo` actually exports (it is compile-checked, not yet run);
- everything in spec 11 (budgets) and 13 (device matrix).

## Building on a host (mock engine)

    sudo apt install qtbase5-dev qtdeclarative5-dev qt5-qmake libqt5opengl5-dev g++ pkg-config
    cargo build --workspace
    QT_QPA_PLATFORM=offscreen cargo test --workspace

With Xvfb and Mesa installed, the smoke test also drives the real FBO
render path:

    xvfb-run -a -s "-screen 0 1080x2260x24" \
        env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
        cargo test -p tuuli-browser --test smoke

`cargo run -p tuuli-browser` starts the chrome with the mock engine in a
bare `QQuickView`; it needs Silica to show anything, so on a host it only
proves that the Rust-exported types load.

To type-check the libservo backend against the pinned tag (this clones
Servo and builds its dependency tree; expect a long first run):

    cargo check --manifest-path servo/backend/Cargo.toml

`make check` runs everything CI runs that needs no SDK: format, clippy,
tests, the Rust 1.75 build, the Harbour source check and its selftest,
the packaging lint and the vendored-crate check.

## Building for the device

Dispatch the `rpm` workflow from the Actions tab (engine `mock` or
`servo`); it builds inside the Sailfish SDK image and uploads the RPM.
See [docs/BUILDING.md](docs/BUILDING.md) for the same steps locally,
[docs/PACKAGING.md](docs/PACKAGING.md) for what the package contains and
[docs/HARBOUR.md](docs/HARBOUR.md) for the store's rules and the open
questions for Jolla.

## Layout

    Cargo.toml              workspace: tuuli-core, tuuli-qml, tuuli-browser (rust-version 1.75)
    Makefile                `make check`: what CI runs without the SDK
    crates/tuuli-core/      Qt-free core: engine seam + mock engine, tabs, session, history,
                            bookmarks, permissions, downloads, gestures, IME planning, prefs,
                            cosmetic filter, search, proxy, perf log  (`cargo test` lives here)
    crates/tuuli-qml/       Qt layer (qmetaobject-rs): Browser singleton, models, the WebView
                            FBO item, IME proxy, D-Bus bridges, image provider — `import Tuuli 1.0`
    crates/tuuli-browser/   the binary: application/sailfishapp setup, run(); host smoke test
    servo/backend/          engine backend over the libservo crate (outside the workspace)
    servo/app/              the Servo-linked harbour-tuuli binary (outside the workspace)
    servo/                  pinned tag, cross-build script, patch queue
    src/qml/                Silica chrome
    src/app/                harbour-tuuli.desktop with the sailjail profile
    ci/                     Harbour source check + selftest, validator runner, lint scripts,
                            the validator's rule files (ci/harbour/)
    scripts/                refresh the vendored Harbour rules
    third_party/            qmetaobject 0.2.10 + the QGuiApplication patch (docs/HARBOUR.md)
    tools/                  corpus, budgets, perf and screenshot scripts, icons
    rpm/                    harbour-tuuli.spec
    .github/workflows/      ci.yml (every push), rpm.yml (device RPM on demand)
    docs/                   spec, architecture, Harbour, threat model, checklists, upstream policy

## Contributing

Anything the engine lacks is added upstream in libservo, not patched
locally; see [docs/UPSTREAM.md](docs/UPSTREAM.md).  Keep the backend thin,
keep everything above the engine seam free of Qt and unit-tested in
`tuuli-core`, and keep the Qt layer free of logic.  CI enforces
`cargo fmt --all --check` and `cargo clippy --workspace --all-targets -D
warnings`.
