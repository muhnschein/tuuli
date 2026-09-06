#!/bin/bash
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Cross-compiles the Servo-linked harbour-tuuli (servo/app) for aarch64
# Sailfish OS 5.2 against the pinned Servo tag (spec 3.3, 12.1) and packs
# the tarball `rpm/harbour-tuuli.spec --with servo` installs.
#
# Run this on the SDK host (or a CI runner), NOT inside the SDK target: the
# SDK's Rust is 1.75 and cannot build Servo, SpiderMonkey needs a recent
# Clang, and the target's GCC is too old for either (spec 12.1).  The SDK
# target root is used only as the sysroot: Qt, libsailfishapp and the C/C++
# dependencies come from it, and the binary is linked against it.
#
# Prerequisites (host):
#   - the target root: a Sailfish Platform SDK with an aarch64 target
#     installed (sfdk tools target list -> SailfishOS-5.2.0.x-aarch64), or
#     a copy of one passed with --sysroot (what the rpm workflow does,
#     lifted out of the SDK container)
#   - clang >= 17, lld, llvm-ar, llvm-readelf, llvm-strip, llvm-objcopy
#   - rustup (the toolchain is Servo's rust-toolchain.toml at the tag,
#     copied to servo/app/ so cargo uses it for the whole build)
#   - python3, pkg-config, cmake, git, curl, xz
#
# Usage:
#   servo/build.sh [--target SailfishOS-5.2.0.x-aarch64 | --sysroot DIR] [--jobs N] [--media]
#
#   --media (or MEDIA=1) enables Servo's GStreamer media backend, which
#   links the target's GStreamer including libgstwebrtc-1.0 (the sysroot
#   needs gstreamer-webrtc-1.0.pc); off, Servo's dummy media backend is
#   built in and media elements do not play (servo/backend/Cargo.toml).
#
# Output:
#   servo/out/harbour-tuuli-servo-<ver>-aarch64.tar.xz
#       bin/harbour-tuuli (stripped) + debug/harbour-tuuli.debug; copy it to
#       rpm/ and build the spec with --with servo.
#
# Exit criteria this script checks (spec 10, M0.1): the binary builds, is
# aarch64, has no host RUNPATH and needs only libraries the sysroot has.
# Which of those libraries Harbour allows is the validator's call
# (docs/HARBOUR.md).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
APP="$HERE/app"
TAG="$(tr -d '[:space:]' < "$HERE/SERVO_TAG")"
GIT_TAG="v$TAG"
VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' "$ROOT/Cargo.toml" | head -1)"
SFDK_TARGET="${SFDK_TARGET:-}"
SYSROOT="${SYSROOT:-}"
JOBS="${JOBS:-$(nproc)}"
MEDIA="${MEDIA:-0}"
SRC="$HERE/src/servo"
OUT="$HERE/out"
RUST_TARGET="aarch64-unknown-linux-gnu"
CLANG_TARGET="aarch64-linux-gnu"

while [ $# -gt 0 ]; do
    case "$1" in
        --target) SFDK_TARGET="$2"; shift 2 ;;
        --sysroot) SYSROOT="$2"; shift 2 ;;
        --jobs) JOBS="$2"; shift 2 ;;
        --media) MEDIA=1; shift ;;
        --help|-h) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

log() { printf '\033[1;34m==> %s\033[0m\n' "$*"; }
die() { printf '\033[1;31merror: %s\033[0m\n' "$*" >&2; exit 1; }

for tool in clang clang++ ld.lld llvm-ar llvm-readelf llvm-strip llvm-objcopy rustup cargo curl python3 pkg-config cmake git xz; do
    command -v "$tool" >/dev/null || die "$tool not found"
done

# The pins must agree: servo/SERVO_TAG and the git tag in servo/backend/Cargo.toml.
grep -q "tag = \"$GIT_TAG\"" "$HERE/backend/Cargo.toml" \
    || die "servo/backend/Cargo.toml does not pin tag \"$GIT_TAG\" (servo/SERVO_TAG says $TAG)"

# ---- SDK target ----------------------------------------------------------
if [ -z "$SYSROOT" ]; then
    command -v sfdk >/dev/null || die "sfdk not found; run inside the Sailfish SDK host shell, or pass --sysroot"
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
fi
[ -d "$SYSROOT/usr/include/qt5/QtCore" ] || die "target sysroot with Qt headers not found at $SYSROOT"
[ -d "$SYSROOT/usr/include/sailfishapp" ] || die "libsailfishapp-devel is not installed in the target"
LIBDIR="$SYSROOT/usr/lib64"; [ -d "$LIBDIR" ] || LIBDIR="$SYSROOT/usr/lib"
log "sysroot: $SYSROOT (libdir $LIBDIR)"
# crtbegin.o, crtend.o and libgcc come from the target's GCC, installed
# under a triple (aarch64-meego-linux-gnu) clang does not scan for; it is
# pointed there explicitly.
GCC_INSTALL=""
if [ -d "$SYSROOT/usr/lib/gcc" ]; then
    GCC_INSTALL="$(find "$SYSROOT/usr/lib/gcc" -mindepth 2 -maxdepth 2 -type d -path '*/aarch64-*-linux-gnu/*' | sort -V | tail -1 || true)"
fi
[ -n "$GCC_INSTALL" ] || die "no GCC installation under $SYSROOT/usr/lib/gcc: the target has none of its own, lift the tooling's cross gcc lib dir there (see the rpm workflow's sysroot step)"
[ -f "$GCC_INSTALL/crtbegin.o" ] || die "$GCC_INSTALL has no crtbegin.o"
log "target gcc: $GCC_INSTALL"
# libstdc++ headers (libstdc++-devel in the target), named explicitly:
# clang derives the directory from the GCC version and the target's
# layout need not match.
CXX_INCLUDE=""
if [ -d "$SYSROOT/usr/include/c++" ]; then
    CXX_INCLUDE="$(find "$SYSROOT/usr/include/c++" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -1 || true)"
fi
[ -n "$CXX_INCLUDE" ] || die "no libstdc++ headers under $SYSROOT/usr/include/c++ (install libstdc++-devel in the target)"
CXX_TARGET_INCLUDE="$(find "$CXX_INCLUDE" -mindepth 1 -maxdepth 1 -type d -name 'aarch64-*' | head -1 || true)"
log "libstdc++ headers: $CXX_INCLUDE${CXX_TARGET_INCLUDE:+ + $CXX_TARGET_INCLUDE}"

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
servo = { path = "$SRC/components/servo" }
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

# The same flags reach every C/C++ build script (cc, cmake, autoconf) and
# the final link.  -fuse-ld=lld is in the compile flags too: autoconf's
# "C compiler works" test links, and the host's GNU ld has no aarch64
# emulation.
CROSS_TARGET_FLAGS="--target=$CLANG_TARGET --sysroot=$SYSROOT --gcc-install-dir=$GCC_INSTALL"
CROSS_FLAGS="$CROSS_TARGET_FLAGS -fuse-ld=lld"
CROSS_CXX_INCLUDES="-isystem $CXX_INCLUDE${CXX_TARGET_INCLUDE:+ -isystem $CXX_TARGET_INCLUDE}"
# Qt 5.6's qtypetraits.h implements is_unsigned as (T(0) < T(-1)), which
# casts -1 to every enum it is instantiated with.  Since clang 16 that is
# an error by default, so every translation unit including a Qt header --
# qttypes, the cpp crate's closures, our own -- fails on the target's Qt.
# The cast is well defined for the flag enums Qt uses it on, and the
# target's own gcc compiles it; nothing here can change Qt 5.6.
CROSS_CXX_FLAGS="$CROSS_FLAGS $CROSS_CXX_INCLUDES -Wno-enum-constexpr-conversion"
# The flags travel in the compiler commands, not only in CFLAGS.
# SpiderMonkey's configure (mozjs_sys reads CC/CXX/CFLAGS/CXXFLAGS by the
# cc-rs rules, then makefile.cargo hands them to js/src/configure) runs its
# early probes as the bare compiler plus mozilla's own --target, without
# the CFLAGS it was given -- which is why the libstdc++ probe looked for
# <cstddef> outside the sysroot.  Anything that runs the compiler at all
# gets the sysroot this way.  -Qunused-arguments keeps the linker choice
# from warning on compile-only probes.
CROSS_CC="clang $CROSS_FLAGS -Qunused-arguments"
CROSS_CXX="clang++ $CROSS_CXX_FLAGS -Qunused-arguments"
export CC_aarch64_unknown_linux_gnu="$CROSS_CC"
export CXX_aarch64_unknown_linux_gnu="$CROSS_CXX"
export AR_aarch64_unknown_linux_gnu=llvm-ar
export CFLAGS_aarch64_unknown_linux_gnu="$CROSS_FLAGS"
export CXXFLAGS_aarch64_unknown_linux_gnu="$CROSS_CXX_FLAGS"
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=clang
export CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_RUSTFLAGS="-C link-arg=--target=$CLANG_TARGET -C link-arg=--sysroot=$SYSROOT -C link-arg=--gcc-install-dir=$GCC_INSTALL -C link-arg=-fuse-ld=lld -C link-arg=-Wl,--as-needed -C link-arg=-L$LIBDIR"
# Qt from the sysroot: qttypes takes these instead of running qmake, which
# it cannot (the target's qmake is an aarch64 binary).
export QT_INCLUDE_PATH="$SYSROOT/usr/include/qt5"
export QT_LIBRARY_PATH="$LIBDIR"
export SAILFISHAPP_INCLUDE_PATH="$SYSROOT/usr/include/sailfishapp"
# The target's Qt is a GLES build: the FBO renderer links libGLESv2.
export TUULI_LINK_GLESV2=1
# C/C++ deps (gstreamer, fontconfig, freetype, harfbuzz, egl, dbus, ...)
# via the target's pkg-config files.
export PKG_CONFIG_ALLOW_CROSS=1
export PKG_CONFIG_SYSROOT_DIR="$SYSROOT"
export PKG_CONFIG_LIBDIR="$LIBDIR/pkgconfig:$SYSROOT/usr/share/pkgconfig"
# bindgen (mozjs, gstreamer-sys) must see the sysroot headers, not the host's.
export BINDGEN_EXTRA_CLANG_ARGS_aarch64_unknown_linux_gnu="$CROSS_TARGET_FLAGS $CROSS_CXX_INCLUDES -Wno-enum-constexpr-conversion"
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
set(CMAKE_C_FLAGS_INIT "$CROSS_FLAGS")
set(CMAKE_CXX_FLAGS_INIT "$CROSS_CXX_FLAGS")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=lld")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
CM

# ---- Toolchain preflight -------------------------------------------------
# Every C and C++ dependency (jemalloc, SpiderMonkey, the -sys crates)
# configures itself by linking a test program, and a sysroot missing one
# startup object or link name fails all of them the same way, an hour into
# the build.  One link here says so in a second, with the linker's own
# message.
# The checks run $CROSS_CC and $CROSS_CXX, the compiler commands the
# sub-builds are handed, rather than flags only this script assembles.
log "checking that the cross toolchain links"
mkdir -p "$OUT"
printf 'int main(void){return 0;}\n' > "$OUT/conftest.c"
# shellcheck disable=SC2086 # the compiler command is several words.
if $CROSS_CC -o "$OUT/conftest" "$OUT/conftest.c" 2> "$OUT/conftest.log"; then
    llvm-readelf -h "$OUT/conftest" | grep -q AArch64 || die "the preflight binary is not aarch64"
    rm -f "$OUT/conftest" "$OUT/conftest.c" "$OUT/conftest.log"
else
    cat "$OUT/conftest.log" >&2
    die "the cross toolchain cannot link against $SYSROOT (message above)"
fi
# C++ too: mozjs and the C++ -sys crates need the libstdc++ headers and
# the link name, which come from different packages than the C ones.
# <cstddef> is the header SpiderMonkey's configure probes for, and the
# one a sysroot-less C++ command line fails to find first.
printf '#include <cstddef>\n#include <string>\nint main(void){ return std::string("x").size() == 1 ? 0 : 1; }\n' > "$OUT/conftest.cc"
# shellcheck disable=SC2086 # the compiler command is several words.
if $CROSS_CXX -o "$OUT/conftest" "$OUT/conftest.cc" 2> "$OUT/conftest.log"; then
    rm -f "$OUT/conftest" "$OUT/conftest.cc" "$OUT/conftest.log"
else
    cat "$OUT/conftest.log" >&2
    die "the cross toolchain cannot link C++ against $SYSROOT (message above)"
fi
# And the target's Qt headers, which qttypes, the cpp crate and the two
# Qt crates all compile against: Qt 5.6 against a current clang is its own
# question, separate from whether the sysroot is sound.
printf '#include <QtCore/QByteArray>\n#include <QtGui/QGuiApplication>\nQByteArray tuuli_preflight(void){ return QByteArray("x"); }\n' > "$OUT/conftest.cc"
# shellcheck disable=SC2086 # the compiler command is several words.
if $CROSS_CXX -I"$QT_INCLUDE_PATH" -std=c++11 -c -o "$OUT/conftest.o" "$OUT/conftest.cc" 2> "$OUT/conftest.log"; then
    rm -f "$OUT/conftest.o" "$OUT/conftest.cc" "$OUT/conftest.log"
else
    cat "$OUT/conftest.log" >&2
    die "the cross toolchain cannot compile against the target's Qt headers (message above)"
fi

# The shape SpiderMonkey's configure uses: the compiler command alone,
# with no CFLAGS behind it.  This is the check run 12 needed and did not
# have; if it passes, mozjs's own probes see the sysroot.
printf '#include <cstddef>\nint main(void){ return 0; }\n' > "$OUT/conftest.cc"
# shellcheck disable=SC2086 # the compiler command is several words.
if $CROSS_CXX "$OUT/conftest.cc" -c -o "$OUT/conftest.o" 2> "$OUT/conftest.log"; then
    rm -f "$OUT/conftest.o" "$OUT/conftest.cc" "$OUT/conftest.log"
else
    cat "$OUT/conftest.log" >&2
    die "the compiler command alone cannot find the sysroot's C++ headers, which is what SpiderMonkey's configure probes with (message above)"
fi

# ---- Build ---------------------------------------------------------------
FEATURES="sailfish"
if [ "$MEDIA" = 1 ]; then
    FEATURES="$FEATURES,media"
    PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR" PKG_CONFIG_SYSROOT_DIR="$SYSROOT" pkg-config --exists gstreamer-webrtc-1.0 \
        || die "--media needs gstreamer-webrtc-1.0.pc in the sysroot (gstreamer1.0-plugins-bad-devel)"
fi
log "building harbour-tuuli (servo $GIT_TAG) $VERSION for $RUST_TARGET (jobs=$JOBS, features=$FEATURES)"
(cd "$APP" && cargo build --release -j "$JOBS" --target "$RUST_TARGET" --features "$FEATURES")

BIN="$APP/target/$RUST_TARGET/release/harbour-tuuli"
[ -f "$BIN" ] || die "build produced no $BIN"

# ---- Checks --------------------------------------------------------------
log "checking the binary"
llvm-readelf -h "$BIN" | grep -q "AArch64" || die "$BIN is not an aarch64 binary"
if llvm-readelf -d "$BIN" | grep -q "RUNPATH\|RPATH"; then
    die "unexpected RUNPATH in harbour-tuuli (host paths leaking?)"
fi
for lib in $(llvm-readelf --needed-libs "$BIN" | sed -n 's/^ *\[\(.*\)\]$/\1/p'); do
    [ -e "$LIBDIR/$lib" ] || [ -e "$SYSROOT/lib64/$lib" ] || [ -e "$SYSROOT/lib/$lib" ] \
        || die "needed library $lib is not in the sysroot"
done

# ---- Pack ----------------------------------------------------------------
STAGE="$OUT/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/debug"
llvm-objcopy --only-keep-debug "$BIN" "$STAGE/debug/harbour-tuuli.debug"
cp "$BIN" "$STAGE/bin/harbour-tuuli"
llvm-strip --strip-debug "$STAGE/bin/harbour-tuuli"
llvm-objcopy --add-gnu-debuglink="$STAGE/debug/harbour-tuuli.debug" "$STAGE/bin/harbour-tuuli"
TARBALL="$OUT/harbour-tuuli-servo-$VERSION-aarch64.tar.xz"
tar -C "$STAGE" -cJf "$TARBALL" .
log "wrote $TARBALL"
log "next: cp $TARBALL rpm/ && build rpm/harbour-tuuli.spec --with servo"
