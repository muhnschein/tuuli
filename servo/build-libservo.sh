#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Cross-compiles libservo (servo_capi) for aarch64 Sailfish OS 5.2 from the
# pinned tag (spec 3.3, 12.1) and packs it as the prebuilt tarball
# rpm/libservo.spec installs.
#
# Run this on the host, NOT inside the SDK target: SpiderMonkey needs a
# recent Clang and Servo's pinned Rust toolchain, and the target's GCC is
# too old for either (spec 12.1).  The SDK target root is used only as the
# sysroot for the C/C++ dependencies and the link.
#
# Prerequisites (host):
#   - Sailfish Platform SDK with an aarch64 target installed, e.g.
#       sfdk tools target list  ->  SailfishOS-5.2.0.x-aarch64
#   - clang >= 17, lld, python3, pkg-config, cmake, git
#   - rustup (the toolchain is pinned by Servo's rust-toolchain.toml)
#
# Usage:
#   servo/build-libservo.sh [--target SailfishOS-5.2.0.x-aarch64] [--jobs N]
#
# Outputs:
#   servo/out/libservo-<tag>-aarch64.tar.xz   (Source1 for rpm/libservo.spec)
#   servo/out/servo-<tag>.tar.xz              (Source0, git archive of the tag)
#   servo/out/servo-<tag>-vendor.tar.xz       (Source2, cargo vendor)
#
# Exit criteria this script checks (spec 10, M0.1): the crate builds, links
# against the target sysroot only, and exports the servo_capi symbols.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TAG="$(tr -d '[:space:]' < "$HERE/SERVO_TAG")"
SFDK_TARGET="${SFDK_TARGET:-}"
JOBS="${JOBS:-$(nproc)}"
SRC="$HERE/src/servo"
OUT="$HERE/out"
RUST_TARGET="aarch64-unknown-linux-gnu"

while [ $# -gt 0 ]; do
    case "$1" in
        --target) SFDK_TARGET="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --help|-h) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

log() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

command -v clang >/dev/null || die "clang not found"
command -v cargo >/dev/null || die "cargo not found (install rustup)"
command -v sfdk >/dev/null || die "sfdk not found; run inside the Sailfish SDK host shell"

if [ -z "$SFDK_TARGET" ]; then
    SFDK_TARGET="$(sfdk tools target list 2>/dev/null | grep -i aarch64 | head -1 | awk '{print $1}')"
    [ -n "$SFDK_TARGET" ] || die "no aarch64 target installed; sfdk tools target install ..."
fi
log "SDK target: $SFDK_TARGET"

# The target root as seen from the host.
SYSROOT="$(sfdk tools exec "$SFDK_TARGET" sb2-config -t 2>/dev/null || true)"
if [ -z "$SYSROOT" ] || [ ! -d "$SYSROOT" ]; then
    SYSROOT="$HOME/SailfishOS/mersdk/targets/$SFDK_TARGET"
fi
[ -d "$SYSROOT/usr/include" ] || die "target sysroot not found at $SYSROOT"
log "sysroot: $SYSROOT"

# ---- Sources -----------------------------------------------------------
mkdir -p "$OUT" "$HERE/src"
if [ ! -d "$SRC/.git" ]; then
    log "cloning servo at tag $TAG"
    git clone --depth 1 --branch "$TAG" https://github.com/servo/servo.git "$SRC"
else
    (cd "$SRC" && git fetch --depth 1 origin "refs/tags/$TAG:refs/tags/$TAG" && git checkout -q "$TAG")
fi

# ---- Patch queue (docs/UPSTREAM.md) -------------------------------------
if [ -s "$HERE/patches/series" ]; then
    (cd "$SRC" && git checkout -q -- . && while read -r p; do
        [ -z "$p" ] || [ "${p#\#}" != "$p" ] && continue
        log "applying $p"
        git apply --3way "$HERE/patches/$p"
    done < "$HERE/patches/series")
fi

# ---- Toolchain ----------------------------------------------------------
rustup target add "$RUST_TARGET" --toolchain "$(cd "$SRC" && rustup show active-toolchain | awk '{print $1}')"

CLANG_TARGET="aarch64-linux-gnu"
export CC_aarch64_unknown_linux_gnu="clang --target=$CLANG_TARGET --sysroot=$SYSROOT"
export CXX_aarch64_unknown_linux_gnu="clang++ --target=$CLANG_TARGET --sysroot=$SYSROOT"
export AR_aarch64_unknown_linux_gnu="llvm-ar"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER="clang"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS="-C link-arg=--target=$CLANG_TARGET -C link-arg=--sysroot=$SYSROOT -C link-arg=-fuse-ld=lld -C link-arg=-Wl,--as-needed"
# C/C++ deps (gstreamer, fontconfig, freetype, harfbuzz, egl, dbus, ...) via
# the target's pkg-config files.
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/share/pkgconfig"
# bindgen (mozjs, gstreamer-sys) must see the sysroot headers, not the host's.
export BINDGEN_EXTRA_CLANG_ARGS_aarch64_unknown_linux_gnu="--target=$CLANG_TARGET --sysroot=$SYSROOT"
# SpiderMonkey: build from source with clang against the sysroot.
export MOZJS_FROM_SOURCE=1
export MOZJS_CREATE_ARCHIVE=0
export CMAKE_TOOLCHAIN_FILE="$HERE/cmake-toolchain-aarch64-sfos.cmake"
cat > "$CMAKE_TOOLCHAIN_FILE" <<CM
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_SYSROOT "$SYSROOT")
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET $CLANG_TARGET)
set(CMAKE_CXX_COMPILER_TARGET $CLANG_TARGET)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
CM

# ---- Build ---------------------------------------------------------------
log "building servo_capi for $RUST_TARGET (jobs=$JOBS)"
(cd "$SRC" && cargo build --release -j "$JOBS" \
    --target "$RUST_TARGET" \
    -p servo_capi \
    --no-default-features \
    --features "media-gstreamer,mobile")

LIB="$SRC/target/$RUST_TARGET/release/libservo_capi.so"
[ -f "$LIB" ] || die "build produced no $LIB"

# ---- Checks --------------------------------------------------------------
log "checking exported symbols"
for sym in servo_init servo_spin_event_loop servo_webview_new servo_webview_paint servo_capi_version_check; do
    llvm-nm -D --defined-only "$LIB" | grep -q " T $sym\$" || die "missing symbol $sym"
done
log "checking the link is against the sysroot only"
if llvm-readelf -d "$LIB" | grep -q RUNPATH; then
    die "unexpected RUNPATH in libservo (host paths leaking?)"
fi

# ---- Header --------------------------------------------------------------
HDR="$SRC/target/$RUST_TARGET/release/servo_capi.h"
if [ ! -f "$HDR" ]; then
    HDR="$(find "$SRC/target" -name servo_capi.h | head -1 || true)"
fi
[ -n "$HDR" ] && [ -f "$HDR" ] || die "cbindgen header servo_capi.h not found in the build output"
"$HERE/../tools/check-capi-header.sh" "$HDR" "$HERE/capi/servo_capi.h" || \
    die "servo_capi.h from the build does not match servo/capi/servo_capi.h; update the shim"

# ---- Pack ----------------------------------------------------------------
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/lib64" "$STAGE/include"
cp "$LIB" "$STAGE/lib64/libservo.so.$TAG"
llvm-strip --strip-debug "$STAGE/lib64/libservo.so.$TAG"
cp "$HDR" "$STAGE/include/servo_capi.h"
cp "$HERE/capi/servo_capi.pc.in" "$STAGE/servo_capi.pc.in"
tar -C "$STAGE" -cJf "$OUT/libservo-$TAG-aarch64.tar.xz" .
log "wrote $OUT/libservo-$TAG-aarch64.tar.xz"

log "archiving sources and vendored crates for the from-source spec"
(cd "$SRC" && git archive --format=tar --prefix="servo-$TAG/" "$TAG" | xz -T0 > "$OUT/servo-$TAG.tar.xz")
(cd "$SRC" && cargo vendor --locked vendor >/dev/null && tar -cJf "$OUT/servo-$TAG-vendor.tar.xz" vendor)
sed "s|@SYSROOT@|/|" "$HERE/cargo-config.toml.in" > "$OUT/cargo-config.toml"
log "done"
