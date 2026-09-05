#!/bin/sh
# Compile translations/*.ts into the .qm files the app loads.
#
#     scripts/release-translations.sh [<output dir>]
#
# -idbased: the strings are qsTrId() ids, and an id with no translation
# gets its engineering English (the //% text) instead of nothing.  The RPM
# runs this in %build; `make translations` runs it for a source-tree run.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-$root/translations}
mkdir -p "$out"

lrelease=$(command -v lrelease || command -v lrelease-qt5) || {
    echo "release-translations: lrelease not found (install qttools5-dev-tools)" >&2
    exit 1
}

for ts in "$root"/translations/*.ts; do
    name=$(basename "$ts" .ts)
    "$lrelease" -idbased "$ts" -qm "$out/$name.qm"
done
