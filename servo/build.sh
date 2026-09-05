#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Cross-compiles the Servo-linked tuuli-browser (servo/app) for aarch64
# Sailfish OS 5.2 against the pinned Servo tag (spec 3.3, 12.1) and packs
# the tarball rpm/tuuli-browser-servo.spec installs.
#
# Run this on the SDK host, NOT inside the SDK target: SpiderMonkey needs a
# recent Clang and Servo's pinned Rust toolchain, and the target's GCC is
# too old for either (spec 12.1).  The SDK target root is used only as the
# sysroot: Qt, libsailfishapp and the C/C++ dependencies come from it, and
# the binary is linked against it.
#
# Prerequisites (host):
#   - Sailfish Platform SDK with an aarch64 target installed, e.g.
#       sfdk tools target list  ->  SailfishOS-5.2.0.x-aarch64
#   - clang >= 17, lld, llvm-ar, llvm-readelf, llvm-strip, llvm-objcopy
#   - rustup (the toolchain is Servo's rust-toolchain.toml at the tag,
#     copied to servo/app/ so cargo uses it for the whole build)
#   - python3, pkg-config, cmake, git, curl, xz
#
# Usage:
#   servo/build.sh [--target SailfishOS-5.2.0.x-aarch64] [--jobs N] [--no-vendor]
#
# Outputs (servo/out/):
#   tuuli-browser-servo-<ver>-aarch64.tar.xz  Source1 of rpm/tuuli-browser-servo.spec:
#                                             bin/tuuli-browser (stripped) + debug/tuuli-browser.debug
#   tuuli-browser-servo-<ver>-vendor.tar.xz   Source2: `cargo vendor` of servo/app, Servo's tree
#                                             included, plus the source-replacement config
#
# Exit criteria this script checks (spec 10, M0.1): the binary builds, is
# aarch64, has no host RUNPATH and needs only libraries the sysroot has.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
APP="$HERE/app"
TAG="$(tr -d '[:space:]' < "$HERE/SERVO_TAG")"
GIT_TAG="v$TAG"
VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' "$ROOT/Cargo.toml" | head -1)"
SFDK_TARGET="${SFDK_TARGET:-}"
JOBS="${JOBS:-$(nproc)}"
SRC="$HERE/src/servo"
OUT="$HERE/out"
RUST_TARGET="aarch64-unknown-linux-gnu"
CLANG_TARGET="aarch64-linux-gnu"
VENDOR=1

while [ $# -gt 0 ]; do
    case "$1" in
        --target) SFDK_TARGET="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --no-vendor) VENDOR=0; shift ;;
        --help|-h) sed -n '2,36p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

log() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

for tool in clang clang++ ld.lld llvm-ar llvm-readelf llvm-strip llvm-objcopy rustup cargo curl python3 pkg-config cmake git xz; do
    command -v "$tool" >/dev/null || die "$tool not found"
done
command -v sfdk >/dev/null || die "sfdk not found; run inside the Sailfish SDK host shell"

# The pins must agree: servo/SERVO_TAG and the git tag in servo/backend/Cargo.toml.
grep -q "tag = \"$GIT_TAG\"" "$HERE/backend/Cargo.toml" \
    || die "servo/backend/Cargo.toml does not pin tag \"$GIT_TAG\" (servo/SERVO_TAG says $TAG)"

# ---- SDK target ----------------------------------------------------------
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
[ -d "$SYSROOT/usr/include/qt5/QtCore" ] || die "target sysroot with Qt headers not found at $SYSROOT"
[ -d "$SYSROOT/usr/include/sailfishapp" ] || die "libsailfishapp-devel is not installed in the target"
LIBDIR="$SYSROOT/usr/lib64"; [ -d "$LIBDIR" ] || LIBDIR="$SYSROOT/usr/lib"
log "sysroot: $SYSROOT (libdir $LIBDIR)"

mkdir -p "$OUT" "$HERE/src" "$APP/.cargo"

# ---- Patch queue (servo/patches, docs/UPSTREAM.md) ------------------------
# With an empty queue cargo builds the tag straight from git; otherwise the
# tag is cloned, patched, and substituted through [patch] in cargo's config.
PATCHES=()
if [ -s "$HERE/patches/series" ]; then
    while read -r p; do
        [ -z "$p" ] && continue
        [ "${p#\#}" != "$p" ] && continue
        PATCHES+=("$p")
    done < "$HERE/patches/series"
fi
if [ "${#PATCHES[@]}" -gt 0 ]; then
    if [ ! -d "$SRC/.git" ]; then
        log "cloning servo at $GIT_TAG"
        git clone --depth 1 --branch "$GIT_TAG" https://github.com/servo/servo.git "$SRC"
    else
        (cd "$SRC" && git fetch --depth 1 origin "refs/tags/$GIT_TAG:refs/tags/$GIT_TAG" && git checkout -q -f "$GIT_TAG")
    fi
    (cd "$SRC" && for p in "${PATCHES[@]}"; do log "applying $p"; git apply --3way "$HERE/patches/$p"; done)
    cp "$SRC/rust-toolchain.toml" "$APP/rust-toolchain.toml"
    cat > "$APP/.cargo/config.toml" <<CFG
# Generated by servo/build.sh: the patched checkout replaces the git tag.
[patch."https://github.com/servo/servo.git"]
libservo = { path = "$SRC/components/servo" }
CFG
else
    rm -f "$APP/.cargo/config.toml"
    if [ ! -f "$APP/rust-toolchain.toml" ]; then
        log "fetching Servo's rust-toolchain.toml for $GIT_TAG"
        curl -fsSL "https://raw.githubusercontent.com/servo/servo/$GIT_TAG/rust-toolchain.toml" -o "$APP/rust-toolchain.toml"
    fi
fi

# ---- Toolchain -----------------------------------------------------------
(cd "$APP" && rustup target add "$RUST_TARGET")

export CC_aarch64_unknown_linux_gnu=clang
export CXX_aarch64_unknown_linux_gnu=clang++
export AR_aarch64_unknown_linux_gnu=llvm-ar
export CFLAGS_aarch64_unknown_linux_gnu="--target=$CLANG_TARGET --sysroot=$SYSROOT"
export CXXFLAGS_aarch64_unknown_linux_gnu="--target=$CLANG_TARGET --sysroot=$SYSROOT"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=clang
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS="-C link-arg=--target=$CLANG_TARGET -C link-arg=--sysroot=$SYSROOT -C link-arg=-fuse-ld=lld -C link-arg=-Wl,--as-needed -C link-arg=-L$LIBDIR"
# Qt from the sysroot: qttypes takes these instead of running qmake, which
# it cannot (the target's qmake is an aarch64 binary).
export QT_INCLUDE_PATH="$SYSROOT/usr/include/qt5"
export QT_LIBRARY_PATH="$LIBDIR"
export SAILFISHAPP_INCLUDE_PATH="$SYSROOT/usr/include/sailfishapp"
# C/C++ deps (gstreamer, fontconfig, freetype, harfbuzz, egl, dbus, ...)
# via the target's pkg-config files.
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$LIBDIR/pkgconfig:$SYSROOT/usr/share/pkgconfig"
# bindgen (mozjs, gstreamer-sys) must see the sysroot headers, not the host's.
export BINDGEN_EXTRA_CLANG_ARGS_aarch64_unknown_linux_gnu="--target=$CLANG_TARGET --sysroot=$SYSROOT"
# SpiderMonkey: build from source with clang against the sysroot.
export MOZJS_FROM_SOURCE=1
export MOZJS_CREATE_ARCHIVE=0
export CMAKE_TOOLCHAIN_FILE="$OUT/cmake-toolchain-aarch64-sfos.cmake"
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
log "building tuuli-browser-servo $VERSION (servo $GIT_TAG) for $RUST_TARGET (jobs=$JOBS)"
(cd "$APP" && cargo build --release -j "$JOBS" --target "$RUST_TARGET" --features sailfish)

BIN="$APP/target/$RUST_TARGET/release/tuuli-browser"
[ -f "$BIN" ] || die "build produced no $BIN"

# ---- Checks --------------------------------------------------------------
log "checking the binary"
llvm-readelf -h "$BIN" | grep -q "AArch64" || die "$BIN is not an aarch64 binary"
if llvm-readelf -d "$BIN" | grep -q "RUNPATH\|RPATH"; then
    die "unexpected RUNPATH in tuuli-browser (host paths leaking?)"
fi
for lib in $(llvm-readelf --needed-libs "$BIN" | sed -n 's/^ *\[\(.*\)\]$/\1/p'); do
    [ -e "$LIBDIR/$lib" ] || [ -e "$SYSROOT/lib64/$lib" ] || [ -e "$SYSROOT/lib/$lib" ] \
        || die "needed library $lib is not in the sysroot"
done

# ---- Pack ----------------------------------------------------------------
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/debug"
llvm-objcopy --only-keep-debug "$BIN" "$STAGE/debug/tuuli-browser.debug"
cp "$BIN" "$STAGE/bin/tuuli-browser"
llvm-strip --strip-debug "$STAGE/bin/tuuli-browser"
llvm-objcopy --add-gnu-debuglink="$STAGE/debug/tuuli-browser.debug" "$STAGE/bin/tuuli-browser"
tar -C "$STAGE" -cJf "$OUT/tuuli-browser-servo-$VERSION-aarch64.tar.xz" .
log "wrote $OUT/tuuli-browser-servo-$VERSION-aarch64.tar.xz"

if [ "$VENDOR" = 1 ]; then
    log "vendoring the servo/app dependency tree for the from-source spec (large)"
    VSTAGE="$OUT/vendor-stage"
    rm -rf "$VSTAGE"; mkdir -p "$VSTAGE"
    (cd "$APP" && cargo vendor --locked "$VSTAGE/vendor" > "$VSTAGE/vendor-config.toml")
    tar -C "$VSTAGE" -cJf "$OUT/tuuli-browser-servo-$VERSION-vendor.tar.xz" vendor vendor-config.toml
    rm -rf "$VSTAGE"
    log "wrote $OUT/tuuli-browser-servo-$VERSION-vendor.tar.xz"
fi
log "done"
