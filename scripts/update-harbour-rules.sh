#!/bin/sh
# Refresh ci/harbour/ from sailfishos/sdk-harbour-rpmvalidator.
#
# Those .conf files are the authoritative Harbour rules -- the prose docs
# lag behind them -- so ci/harbour-check.sh reads them rather than a
# transcription.  They are vendored so CI stays deterministic and offline;
# this script is how they move, and the diff is the review.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
dest="$root/ci/harbour"
repo=https://github.com/sailfishos/sdk-harbour-rpmvalidator.git

# rpmvalidation.conf is not copied: it only names the other files and the
# paths, which ci/harbour-check.sh has its own copy of.
files='allowed_libraries.conf allowed_permissions.conf allowed_qmlimports.conf
       allowed_requires.conf allowed_sailjailkeys.conf deprecated_libraries.conf
       deprecated_qmlimports.conf deprecated_requires.conf disallowed_orgnames.conf
       disallowed_qmlimport_patterns.conf dropped_libraries.conf dropped_qmlimports.conf
       dropped_requires.conf'

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

git clone --depth 1 "$repo" "$tmp/validator" >&2
commit=$(cd "$tmp/validator" && git rev-parse HEAD)
date=$(cd "$tmp/validator" && git log -1 --format=%cs)

mkdir -p "$dest"
for f in $files; do
    cp "$tmp/validator/$f" "$dest/$f"
done

cat > "$dest/UPSTREAM" <<EOT
The .conf files in this directory, waivers.conf excepted, are copied
verbatim from

    $repo

at commit $commit ($date), by scripts/update-harbour-rules.sh.
ci/harbour-validate-rpm.sh runs the validator from that same commit.

They carry that project's licence, GPL-2.0-or-later.  They are CI data:
nothing in the packaged application is built from or links to them, and
Tuuli's own code stays MPL-2.0.  rpmvalidation.sh itself is not vendored:
ci/harbour-check.sh reimplements the checks that a source tree can answer,
and .github/workflows/rpm.yml runs the real script against a built RPM.
EOT

echo "ci/harbour: updated to $commit ($date)"
