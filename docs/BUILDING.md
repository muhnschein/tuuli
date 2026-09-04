# Building

Three builds exist:

| Build | Where | Engine | Purpose |
|---|---|---|---|
| host, mock | any Linux with Qt 5 dev | `MockEngine` | unit tests, shim and model work |
| host, servo-stub | any Linux with Qt 5 dev | `ServoEngine` over a no-op `libservo` | compile/link check of the shim against the ABI |
| device | Sailfish Platform SDK, aarch64 target | `ServoEngine` over the real `libservo` | the product |

Only Qt 5.6 API is used.  Newer host Qt builds it fine; if a host build
uses something 5.6 lacks, the SDK target build (Qt 5.6) will fail, which is
why CI builds on the SDK target too.

## Host

    cmake -S . -B build -G Ninja -DTUULI_ENGINE=mock
    ninja -C build && ctest --test-dir build

    cmake -S . -B build-servo -G Ninja -DTUULI_ENGINE=servo -DTUULI_SERVO_STUB=ON
    ninja -C build-servo

The mock build also produces a runnable `tuuli-browser` that opens a bare
`QQuickView`; it needs Silica to show anything, so on a host it is only
useful for checking that the plugin loads.  On the emulator or a device the
mock-engine RPM (below) runs the full chrome with placeholder content.

## Engine (libservo)

`servo/build-libservo.sh` cross-compiles the `servo_capi` crate at the
pinned tag (`servo/SERVO_TAG`) for `aarch64-unknown-linux-gnu` against the
SDK target root as sysroot, applies the patch queue, checks the exported
symbols and that the cbindgen header matches `servo/capi/servo_capi.h`,
and packs `servo/out/libservo-<tag>-aarch64.tar.xz` plus the source and
vendored-crate tarballs for `rpm/libservo.spec`.

It runs on the SDK **host**, not in the target: SpiderMonkey needs a
recent Clang and Servo's pinned Rust toolchain (spec 12.1).  Requirements
and environment variables are documented at the top of the script.  Expect
this to be where M0's time goes; see [M0-CHECKLIST.md](M0-CHECKLIST.md).

The header check (`tools/check-capi-header.sh`) fails the build when the
ABI Servo generated differs from the one the shim was written against.
When it fails, update `servo/capi/servo_capi.h`, `servo/capi/stub/` and
`src/lib/engine/servo*.cpp` together, in one commit.

## Device

Inside the Platform SDK, with `libservo` and `libservo-devel` installed in
the target (from the RPMs built out of the tarball above):

    sfdk config target=SailfishOS-5.2.0.x-aarch64
    sfdk build -p rpm/tuuli-browser.spec

Without an engine build:

    sfdk build -p rpm/tuuli-browser.spec --with mock_engine

installs on the device and runs the chrome with the mock engine.  Setting
`TUULI_ENGINE=mock` in the environment of a servo build does the same at
runtime.

Sailjail: the `.desktop` file's `[X-Sailjail]` section is the sandbox
profile.  `Location`, `Camera` and `Microphone` are not declared until the
milestone that uses them (spec 9.1).

## Developer toggles

Settings → Developer:

- content pixel-ratio override (spec 6.1: DPR is derived, not hard-coded);
- live web views kept in memory (spec 11 memory budget);
- frame statistics overlay;
- single-threaded render loop (`QSG_RENDER_LOOP=basic`; restart);
- engine logging (`RUST_LOG=info`; restart);
- performance logging → `<cache>/perf.log`, consumed by
  `tools/perf/run-budgets.py`.
