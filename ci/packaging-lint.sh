#!/bin/bash
# ci/packaging-lint.sh — packaging checks (SCOPE.md §7, test tier 4).
#
#  * the spec parses            (rpmspec)
#  * the desktop entry validates (desktop-file-validate)
#  * shell scripts are clean     (shellcheck)
#  * translations compile and are current (lrelease, lupdate)
#  * every docs/*.md a comment points at exists
#  * the changelog has an Unreleased section; Sailjail permissions are documented
#
# A missing tool is SKIP locally and a failure with PACKAGING_LINT_STRICT=1 (CI).
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
STRICT=${PACKAGING_LINT_STRICT:-0}
FAILED=0
SPEC=$ROOT/rpm/harbour-tuuli.spec
DESKTOP=$ROOT/harbour-tuuli.desktop

fail() { # check-id location message
    printf 'ERROR [%s] [%s] %s\n' "$1" "$2" "$3"
    FAILED=1
}

have() { # tool
    if command -v "$1" >/dev/null 2>&1; then
        return 0
    fi
    if [[ $STRICT == 1 ]]; then
        fail tool-missing "$1" "required in CI (PACKAGING_LINT_STRICT=1)"
    else
        echo "SKIP  [$1] not installed"
    fi
    return 1
}

# 1. Spec parses
if have rpmspec; then
    rpmspec -P "$SPEC" >/dev/null 2>&1 || fail spec rpm/harbour-tuuli.spec "$(rpmspec -P "$SPEC" 2>&1 | head -1)"
fi

# 2. Desktop entry validates
if have desktop-file-validate; then
    desktop-file-validate "$DESKTOP" || fail desktop harbour-tuuli.desktop "desktop-file-validate failed"
fi

# 3. Shell scripts clean
if have shellcheck; then
    mapfile -t scripts < <(find "$ROOT/ci" "$ROOT/icons" -name '*.sh' | sort)
    shellcheck --severity=style "${scripts[@]}" || fail shellcheck ci/ "shellcheck reported findings"
fi

# 4. Translations compile and are current
TS_SOURCE=$ROOT/translations/harbour-tuuli.ts
sources_of() { grep -o '<source>[^<]*</source>' "$1" | sort -u; }
if have lrelease; then
    tmp=$(mktemp -d)
    for ts in "$ROOT"/translations/*.ts; do
        lrelease -silent "$ts" -qm "$tmp/$(basename "${ts%.ts}").qm" || fail translations "translations/$(basename "$ts")" "lrelease failed"
    done
    rm -rf "$tmp"
fi
for ts in "$ROOT"/translations/*.ts; do
    [[ $ts == "$TS_SOURCE" ]] && continue
    diff -q <(sources_of "$TS_SOURCE") <(sources_of "$ts") >/dev/null || fail translations "translations/$(basename "$ts")" "source strings differ from harbour-tuuli.ts; run 'make translations'"
done
if have lupdate; then
    tmp=$(mktemp -d)
    cp "$TS_SOURCE" "$tmp/current.ts"
    (cd "$ROOT" && lupdate -silent -no-obsolete -locations none qml src -ts "$tmp/current.ts")
    diff -q <(sources_of "$TS_SOURCE") <(sources_of "$tmp/current.ts") >/dev/null || fail translations translations/harbour-tuuli.ts "catalog is stale; run 'make translations' and commit"
    rm -rf "$tmp"
fi

# 5. Every docs/*.md a comment points at exists
while read -r ref; do
    [[ -f $ROOT/$ref ]] || fail docs-reference "$ref" "referenced but missing"
done < <(grep -rhoE 'docs/[A-Za-z0-9_./-]+\.md' "$ROOT/src" "$ROOT/qml" "$ROOT/tests" "$ROOT/ci" "$ROOT/rpm" "$ROOT/docs" "$ROOT/README.md" "$ROOT/Makefile" "$ROOT/CMakeLists.txt" 2>/dev/null | sort -u)

# 6. Changelog and permission documentation
grep -q '^## \[Unreleased\]' "$ROOT/docs/CHANGELOG.md" 2>/dev/null || fail changelog docs/CHANGELOG.md "missing '## [Unreleased]' section"
permissions=$(sed -n 's/^Permissions=//p' "$DESKTOP" | tr ';' ' ')
for permission in $permissions; do
    grep -q "\b$permission\b" "$ROOT/docs/HARBOUR.md" 2>/dev/null || fail permissions docs/HARBOUR.md "Sailjail permission '$permission' is not documented"
done

if [[ $FAILED -eq 0 ]]; then
    echo "packaging-lint: clean"
    exit 0
fi
exit 1
