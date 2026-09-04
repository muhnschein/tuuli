#!/bin/sh
# Compares the cbindgen-generated servo_capi.h from an engine build with the
# copy the shim was compiled against (servo/capi/servo_capi.h), ignoring
# comments and whitespace.  M0 exit criterion: they must match.
#
#   tools/check-capi-header.sh <generated.h> <servo/capi/servo_capi.h>

set -eu
[ $# -eq 2 ] || { echo "usage: $0 GENERATED_H REFERENCE_H" >&2; exit 2; }

normalize() {
    # strip block and line comments, collapse whitespace, drop blank lines
    sed -e 's|//.*$||' "$1" \
        | tr '\n' ' ' \
        | sed -e 's|/\*[^*]*\*\+\([^/*][^*]*\*\+\)*/||g' \
        | tr -s ' \t' ' ' \
        | sed -e 's/; */;\n/g' -e 's/{ */{\n/g' -e 's/} */}\n/g' \
        | sed -e 's/^ *//' -e 's/ *$//' \
        | grep -v '^$' \
        | grep -v '^#include' \
        | sort
}

a="$(mktemp)"; b="$(mktemp)"
trap 'rm -f "$a" "$b"' EXIT
normalize "$1" > "$a"
normalize "$2" > "$b"
if diff -u "$b" "$a"; then
    echo "servo_capi.h: match"
else
    echo "servo_capi.h: MISMATCH between generated ($1) and reference ($2)" >&2
    exit 1
fi
