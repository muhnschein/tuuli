# Building

Four builds exist:

| Build | Where | Engine | Purpose |
|---|---|---|---|
| host, mock | any Linux with Qt 5 dev packages | `MockEngine` | unit tests, Qt layer, chrome iteration |
| host, backend check | as above plus Servo's build deps | `cargo check` of `servo/backend` | API reconciliation against the pinned tag |
| device, mock | the Sailfish SDK image, aarch64 target, the SDK's Rust 1.75 | `MockEngine` | the Qt 5.6 constraint, the chrome and sailjail on a device |
| device, servo | cross-compiled against the SDK target root with Servo's toolchain | `ServoEngine` | the product |

Only Qt 5.6 API is used, and only Rust 1.75 language and library features
(`rust-version = "1.75"`, the toolchain the SDK ships).  Newer host
toolchains build it fine; CI's `msrv` job and the SDK build keep both
constraints honest.

## Host

    sudo apt install qtbase5-dev qtdeclarative5-dev qt5-qmake libqt5opengl5-dev g++ pkg-config
    cargo build --workspace
    QT_QPA_PLATFORM=offscreen cargo test --workspace

qttypes finds Qt through `qmake` on `PATH` (or `QMAKE=/path/to/qmake`).
The tests are Qt-free `cargo test`s in `tuuli-core` plus a smoke test in
`tuuli-browser` that registers the QML types, instantiates the chrome's
`WebView` and drives the core; headless it checks the wiring, and with a
display it renders through the real FBO path:

    sudo apt install xvfb libgl1-mesa-dri libglx-mesa0 libegl-mesa0
    make smoke

`make check` runs everything CI runs that needs no SDK: format, clippy,
the tests, the Rust 1.75 build, the lockfile format, the QML syntax, the
packaging lint, the Harbour source check with its selftest, and the
vendored-crate check.  The Makefile header lists the packages each wants.

The mock build's binary (`cargo run -p tuuli-browser`, named
`harbour-tuuli`) opens a bare `QQuickView` on the QML in `src/qml`; it
needs Silica to show anything, so on a host it only proves that the
Rust-exported types load.  On a device the mock-engine RPM runs the full
chrome with placeholder content.

## Backend check

    cargo check --manifest-path servo/backend/Cargo.toml

resolves the pinned Servo tag (a clone of Servo plus its dependency tree;
expect a long first run and the build dependencies Servo needs: clang,
llvm, python3, gstreamer, fontconfig, freetype, harfbuzz, EGL/GLES, dbus,
openssl headers).  This is the M0 reconciliation step: it is where the
backend's use of `WebViewDelegate`, `ServoDelegate` and
`RenderingContext` is checked against what 0.5.0 actually exports.

## Device: the supported path

`.github/workflows/rpm.yml` builds a device RPM unattended on a GitHub
runner from a `docker run` of `coderus/sailfishos-platform-sdk`, pinned by
digest.  Dispatch it from the Actions tab (engine, arch and SDK version
are inputs) or push a `v*` or `build-*` tag.  It follows Postivene's
workflow of the same name, whose comments record what each step cost to
learn; the essentials:

- **Engine `mock`**: `mb2 build` inside the SDK container, as the image's
  own `mersdk` user, with the checkout mounted under that user's home
  (scratchbox2 redirects other absolute paths into the target root).  The
  SDK's own Rust 1.75 builds the workspace; the i686 rustlib is lifted out
  of the tooling and mounted where build-script links look for it.
  Minutes.
- **Engine `servo`**: the SDK's Rust cannot build Servo and Rust does not
  mix compiler versions in one link, so the container is used only to
  install the development headers into the target and copy the target root
  out as a sysroot; `servo/build.sh --sysroot` then cross-compiles
  `servo/app` on the runner with Servo's pinned toolchain and clang, and
  `mb2 build -- --with servo` packages the result.  Hours, and the first
  runs are M0's engine spike.
- Every build is stamped `Release: 1.<run number>` so it installs over the
  last; the RPM is uploaded as an artifact; then Jolla's validator runs on
  it (`ci/harbour-validate-rpm.sh`), after the upload so that a package
  Harbour would reject can still be put on a phone.

Install the artifact on the device with `pkcon install-local` or
`rpm -U`, and launch it as `sailjail /usr/bin/harbour-tuuli` to run under
the same sandbox the launcher applies.

## Device: locally

The same steps with a local Platform SDK, mock engine:

    mb2 -t SailfishOS-5.2.0.15-aarch64 -X build-init
    mb2 -t SailfishOS-5.2.0.15-aarch64 -X build --no-check

(`sfdk build` wraps the same.)  Servo engine:

    servo/build.sh                                  # sfdk host shell, or --sysroot <target root>
    cp servo/out/harbour-tuuli-servo-*.tar.xz rpm/
    mb2 -t SailfishOS-5.2.0.15-aarch64 -X build --no-check -- --with servo

`servo/build.sh` needs clang ≥ 17, lld, the llvm tools, rustup and cmake;
it copies Servo's `rust-toolchain.toml` into `servo/app` so cargo uses
that toolchain for the whole build, applies the patch queue if there is
one, and checks that the binary is aarch64, has no host RUNPATH and needs
only sysroot libraries.  Which of those libraries Harbour allows is the
validator's call (`docs/HARBOUR.md`).

Spec constraints, each found the hard way in Postivene and carried here:
`-j1` for cargo under sb2; no `--target` for cargo (`SB2_RUST_TARGET_TRIPLE`
tells the accelerated rustc what to emit); `CARGO_TARGET_<HOST>_LINKER=host-gcc`
inside sb2; `QT_INCLUDE_PATH`/`QT_LIBRARY_PATH` exported so qttypes never
runs the target's `qmake`; no bare `%` in a spec comment.

Setting `TUULI_ENGINE=mock` in the environment (or passing
`--mock-engine`) makes the Servo binary run with the mock engine, which
separates engine problems from chrome problems on the device.

Sailjail: the `.desktop` file's `[X-Sailjail]` section is the sandbox
profile.  `Location`, `Camera` and `Microphone` are not declared until the
milestone that uses them (spec 9.1).

## Developer toggles

Settings → Developer:

- content pixel-ratio override (spec 6.1: DPR is derived, not hard-coded);
- live web views kept in memory (spec 11 memory budget);
- frame statistics overlay;
- engine logging (`RUST_LOG=info`; restart);
- performance logging → `<cache>/perf.log`, consumed by
  `tools/perf/run-budgets.py`.

The scene-graph render loop is not a toggle: Tuuli always runs with
`QSG_RENDER_LOOP=basic` (see [ARCHITECTURE.md](ARCHITECTURE.md), Threads).
