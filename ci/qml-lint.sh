#!/bin/sh
# Parse every .qml file.
#
# Syntax only: qmllint cannot resolve `Sailfish.Silica`, and the Qt 5.6
# constraint is enforced by the SDK build in the rpm workflow.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)

if ! command -v qmllint >/dev/null 2>&1; then
    echo "qml-lint: FAIL (qmllint not found; install qtdeclarative5-dev-tools)" >&2
    exit 1
fi

count=0
status=0
# Read rather than word-split: a path with a space in it would otherwise
# arrive as two arguments and neither would exist.
while IFS= read -r file; do
    [ -n "$file" ] || continue
    count=$((count + 1))
    if ! qmllint "$file"; then
        status=1
    fi
done <<EOF
$(find "$root/src/qml" -name '*.qml' | sort)
EOF

if [ "$count" -eq 0 ]; then
    echo "qml-lint: FAIL (no .qml files found -- did the tree move?)" >&2
    exit 1
fi

[ "$status" -eq 0 ] && echo "qml-lint: ok ($count files)"
exit "$status"
