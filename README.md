# Tuuli

A Servo-based web browser for Sailfish OS.  Silica QML chrome driving the
Servo engine (`libservo`) through the `servo_capi` C ABI and a Qt Quick
scene-graph integration layer.

Tuuli is a **second browser**.  It ships alongside Sailfish Browser and does
not replace it.  Read [docs/THREAT_MODEL.md](docs/THREAT_MODEL.md) before
using it for anything that matters: web content is not sandboxed from the
application.

| | |
|---|---|
| Target | Sailfish OS 5.2+, aarch64, Jolla Phone (2026) and newer |
| Engine | Servo, pinned release ([servo/SERVO_TAG](servo/SERVO_TAG)), MPL-2.0 |
| Toolkit | Qt 5.6 + Silica (no newer Qt API is used anywhere) |
| Licence | MPL-2.0 |
| Distribution | Chum (primary), OpenRepos (secondary).  Not Harbour. |
| Spec | [docs/spec.md](docs/spec.md) |

## Status

This tree implements the spec's M0–M2 structure plus the M3 pieces that
are pure logic, and ships the whole thing with an in-process **mock
engine** so the chrome, models and shim can be built, unit-tested and
iterated on any Linux host and in the emulator without a Servo build.

What exists and is tested on the host:

- the engine seam (`Tuuli::Engine`, `WebViewHandle`, `WebViewClient`) that
  every layer above the engine is written against (spec 4.1);
- the `servo_capi` shim (`ServoEngine`), compile-checked against
  [servo/capi/servo_capi.h](servo/capi/servo_capi.h) through a no-op stub;
- the `QQuickFramebufferObject` view, renderer and `QOpenGLContext`
  rendering-context wrapper (spec 5.1, 5.2), exercised under Xvfb + Mesa;
- input: DPR maths, touch conversion, gesture arbitration (lipstick edges,
  bottom-edge toolbar reveal, long-press without movement, pulley handoff
  at content edges), Maliit text-input proxy with edit-to-key planning
  (spec 6);
- tabs, session persistence with 5 s debounce and crash detection, history
  and bookmarks (SQLite), per-origin permissions, downloads via Transfer
  Engine, clipboard, connman proxy, search engines, preferences with the
  spec 9.4 defaults, cosmetic filtering from EasyList-style lists;
- the complete Silica chrome: start view, page view with pulleys and
  auto-hiding toolbar, tab overview, settings, downloads, history, page
  info, permissions, about, cover;
- RPM packaging split into `tuuli-browser`, `libtuuli-qml` and a separate
  `libservo` source package; the Servo cross-build script; perf-budget
  and screenshot tooling for the ten-page corpus.

What does **not** exist yet, because it needs the device and the engine
build (M0 exit criteria, [docs/M0-CHECKLIST.md](docs/M0-CHECKLIST.md)):

- a libservo built for the SFOS 5.2 aarch64 sysroot;
- proof that WebRender's shaders run on Mali-G610 through libhybris;
- validation of `servo_capi.h` against the pinned tag's cbindgen output;
- everything in spec 11 (budgets) and 13 (device matrix).

## Building on a host (mock engine)

    sudo apt install qtbase5-dev qtdeclarative5-dev libqt5sql5-sqlite cmake ninja-build
    cmake -S . -B build -G Ninja -DTUULI_ENGINE=mock
    ninja -C build
    ctest --test-dir build

`tst_plugin` drives the real render path when `xvfb-run` and Mesa are
present and falls back to a headless mode otherwise.

To compile the real shim against the ABI without an engine:

    cmake -S . -B build-servo -G Ninja -DTUULI_ENGINE=servo -DTUULI_SERVO_STUB=ON

## Building for the device

See [docs/BUILDING.md](docs/BUILDING.md) (engine cross-build, SDK target
build) and [docs/PACKAGING.md](docs/PACKAGING.md) (RPMs, Chum).

## Layout

    servo/          pinned tag, C ABI header, no-op stub, cross-build script, patch queue
    src/lib/        libtuuli: engine seam, shim, input, models, platform, prefs, view
    src/plugin/     `import Tuuli 1.0` Qt Quick plugin
    src/app/        tuuli-browser entry point, .desktop with sailjail profile
    src/qml/        Silica chrome
    tests/          host-side Qt tests (spec 13)
    tools/          corpus, budgets, perf and screenshot scripts, icon generator
    rpm/            tuuli-browser.spec, libservo.spec
    docs/           spec, architecture, threat model, checklists, upstream policy

## Contributing

Anything the engine lacks is added upstream in `servo_capi`, not patched
locally; see [docs/UPSTREAM.md](docs/UPSTREAM.md).  Keep the shim thin and
keep every new piece of shim logic host-testable.
