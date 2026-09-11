#!/bin/bash
# Run clang-tidy over every C++ source we own, using the compile database of the
# host build. Findings are errors (.clang-tidy: WarningsAsErrors '*').
set -euo pipefail

BUILD_DIR=${1:-build}
cd "$(dirname "$0")/.."

if [[ ! -f "$BUILD_DIR/compile_commands.json" ]]; then
    echo "clang-tidy: $BUILD_DIR/compile_commands.json missing; run 'make configure' first" >&2
    exit 1
fi

if ! command -v clang-tidy >/dev/null 2>&1; then
    echo "clang-tidy: not installed" >&2
    exit 1
fi

mapfile -t SOURCES < <(find src tests -name '*.cpp' | sort)
clang-tidy --quiet -p "$BUILD_DIR" "${SOURCES[@]}"
echo "clang-tidy: ${#SOURCES[@]} files clean"
