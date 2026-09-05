#!/bin/sh
# Regenerate translations/harbour-tuuli.ts from the qsTrId() calls in
# src/qml.  The //% comments beside them are the engineering English, which
# lupdate records as each id's source text; lrelease -idbased then uses it
# wherever a translation is missing, so the app never shows a bare id.
#
#     scripts/update-translations.sh [<output dir>]
#
# Without locations: line numbers would churn the catalog on every QML
# edit, and ci/packaging-lint.sh compares the committed file with a fresh
# run.  Per-language catalogs (harbour-tuuli-<lang>.ts) are updated from
# the same sources when they exist.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
out=${1:-$root/translations}
mkdir -p "$out"

lupdate=$(command -v lupdate || command -v lupdate-qt5) || {
    echo "update-translations: lupdate not found (install qttools5-dev-tools)" >&2
    exit 1
}

set -- "$out/harbour-tuuli.ts"
for ts in "$out"/harbour-tuuli-*.ts; do
    [ -f "$ts" ] && set -- "$@" "$ts"
done
"$lupdate" -locations none -no-ui-lines -recursive "$root/src/qml" -ts "$@"
