#!/bin/bash
# ci/harbour-allowlists-drift.sh — compare ci/harbour/*.conf with upstream.
#
# The only script in ci/ that needs the network; it is a CI step, not part of
# `make check`. Prints a GitHub Actions warning per file that lags upstream and always
# exits 0. With --update the upstream copies are written into ci/harbour/.
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
UPSTREAM=$ROOT/ci/harbour/UPSTREAM
UPDATE=0
[[ ${1:-} == --update ]] && UPDATE=1

repo=$(sed -n 's/^repo=//p' "$UPSTREAM")
branch=$(sed -n 's/^branch=//p' "$UPSTREAM")
files=$(sed -n 's/^files=//p' "$UPSTREAM")
raw=${repo/github.com/raw.githubusercontent.com}/$branch

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
drift=0
for file in $files; do
    if ! curl -sSfL -o "$tmp/$file" "$raw/$file"; then
        echo "::warning::could not fetch $raw/$file"
        continue
    fi
    if ! cmp -s "$tmp/$file" "$ROOT/ci/harbour/$file"; then
        drift=1
        if [[ $UPDATE -eq 1 ]]; then
            cp "$tmp/$file" "$ROOT/ci/harbour/$file"
            echo "updated ci/harbour/$file"
        else
            echo "::warning file=ci/harbour/$file::lags upstream $repo ($branch); run ci/harbour-allowlists-drift.sh --update"
        fi
    fi
done
[[ $drift -eq 0 ]] && echo "allow-lists match upstream"
exit 0
