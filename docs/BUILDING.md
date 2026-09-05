# Building

Four builds exist:

| Build | Where | Engine | Purpose |
|---|---|---|---|
| host, mock | any Linux with Qt 5 dev packages | `MockEngine` | unit tests, Qt layer, chrome iteration |
| host, backend check | as above plus Servo's build deps | `cargo check` of `servo/backend` | API reconciliation against the pinned tag |
| device, mock | Sailfish Platform SDK, aarch64 target | `MockEngine` | the Qt 5.6 constraint, chrome on device |
| device, servo | SDK host, cross-compiled against the target root | `ServoEngine` | the product |

Only Qt 5.6 API is used.  Newer host Qt builds it fine; if a host build
uses something 5.6 lacks, the SDK target build (Qt 5.6) will fail, which is
why CI builds on the SDK target too.

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
    xvfb-run -a -s "-screen 0 1080x2260x24" \
        env QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 \
        cargo test -p tuuli-browser --test smoke -- --nocapture

The mock build's binary (`cargo run -p tuuli-browser`) opens a bare
`QQuickView` on the QML in `src/qml`; it needs Silica to show anything, so
on a host it only proves that the Rust-exported types load.  On the
emulator or a device the mock-engine RPM (below) runs the full chrome with
placeholder content.

Before pushing: `cargo fmt --all` and
`cargo clippy --workspace --all-targets -- -D warnings`; CI rejects both.

## Backend check

    cargo check --manifest-path servo/backend/Cargo.toml

resolves the pinned Servo tag (a clone of Servo plus its dependency tree;
expect a long first run and the build dependencies Servo needs: clang,
llvm, python3, gstreamer, fontconfig, freetype, harfbuzz, EGL/GLES, dbus,
openssl headers).  This is the M0 reconciliation step: it is where the
backend's use of `WebViewDelegate`, `ServoDelegate` and
`RenderingContext` is checked against what 0.5.0 actually exports.

## Device, mock engine

Inside the Platform SDK, from vendored crates so the target builds
offline:

    tools/vendor.sh ~/rpmbuild/SOURCES        # git archive + cargo vendor tarball
    sb2 -t SailfishOS-5.2.0.x-aarch64 -m sdk-install -R zypper in \
        rust cargo gcc-c++ qt5-qmake qt5-qtcore-devel qt5-qtgui-devel \
        qt5-qtdeclarative-devel qt5-qtdbus-devel qt5-qtwidgets-devel \
        libsailfishapp-devel desktop-file-utils
    sb2 -t SailfishOS-5.2.0.x-aarch64 rpmbuild -ba rpm/tuuli-browser.spec

(`sfdk build -p rpm/tuuli-browser.spec` works the same way once the two
tarballs are where sfdk looks for sources.)  The result installs on the
device and runs the chrome with the mock engine.

## Device, Servo engine

`servo/build.sh` cross-compiles `servo/app` for
`aarch64-unknown-linux-gnu` on the SDK **host**, with the SDK target root
as the sysroot: Qt and libsailfishapp headers and libraries come from it
(`QT_INCLUDE_PATH`/`QT_LIBRARY_PATH` for qttypes, `SAILFISHAPP_INCLUDE_PATH`
for the application crate), as do the C/C++ dependencies through the
target's pkg-config files.  It runs on the host and not in the target
because SpiderMonkey needs a recent Clang and Servo's pinned Rust toolchain
(spec 12.1); the script copies Servo's `rust-toolchain.toml` into
`servo/app` so cargo uses that toolchain for the whole build.  It applies
the patch queue if there is one, checks that the binary is aarch64, has
no host RUNPATH and needs only sysroot libraries, and packs
`servo/out/tuuli-browser-servo-<version>-aarch64.tar.xz` (plus the
vendored-crate tarball for the from-source spec).  Requirements and
environment variables are documented at the top of the script.  Expect
this to be where M0's time goes; see [M0-CHECKLIST.md](M0-CHECKLIST.md).

Then, in the SDK:

    cp servo/out/tuuli-browser-servo-*.tar.xz ~/rpmbuild/SOURCES/
    sb2 -t SailfishOS-5.2.0.x-aarch64 rpmbuild -ba rpm/tuuli-browser-servo.spec

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
