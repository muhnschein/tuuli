#!/bin/sh
# Produces the vendored-crates tarball rpm/tuuli-browser.spec needs
# (Source1) so the SDK target builds offline.
#
#   tools/vendor.sh [out-dir]
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-$HERE/out}"
VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' "$HERE/Cargo.toml" | head -1)"
mkdir -p "$OUT"
cd "$HERE"
rm -rf vendor
cargo vendor --locked vendor >/dev/null
tar -cJf "$OUT/tuuli-browser-$VERSION-vendor.tar.xz" vendor
rm -rf vendor
git archive --format=tar --prefix="tuuli-browser-$VERSION/" HEAD | bzip2 > "$OUT/tuuli-browser-$VERSION.tar.bz2"
echo "wrote $OUT/tuuli-browser-$VERSION.tar.bz2 and $OUT/tuuli-browser-$VERSION-vendor.tar.xz"
