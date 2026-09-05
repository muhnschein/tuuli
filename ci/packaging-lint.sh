#!/bin/sh
# Static checks on what decides whether the RPM installs and the launcher
# works.  Resolving BuildRequires or running mb2 needs the SDK
# (docs/BUILDING.md); this checks what is checkable anywhere.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
spec="$root/rpm/harbour-tuuli.spec"
desktop="$root/src/app/harbour-tuuli.desktop"
status=0
ran=0

# PACKAGING_LINT_STRICT=1 turns "the tool for this check is missing" into
# a failure.  CI sets it, so the job cannot quietly stop checking when the
# apt line that installs the tools changes.
strict=${PACKAGING_LINT_STRICT:-0}
skip() {
    if [ "$strict" = 1 ]; then
        echo "packaging-lint: FAIL $1 is not installed ($2) (strict mode)" >&2
        status=1
    else
        echo "packaging-lint: SKIP $1 ($2)"
    fi
}

if command -v rpmspec >/dev/null 2>&1; then
    ran=$((ran + 1))
    # -P expands and parses only; BuildRequires are the SDK's job.  Both
    # engines, since --with servo changes the Sources and %install.
    for mode in "" "--with servo"; do
        # shellcheck disable=SC2086 # $mode is zero or two words.
        if rpmspec -P --target aarch64 $mode "$spec" >/dev/null; then
            echo "packaging-lint: rpm/harbour-tuuli.spec parses ${mode:-(mock engine)}"
        else
            echo "packaging-lint: FAIL rpm/harbour-tuuli.spec does not parse ${mode:-(mock engine)}" >&2
            status=1
        fi
    done
else
    skip rpmspec "install rpm"
fi

# rpm expands macros inside comments, and the SDK's rpm still does even
# where a newer host rpm has stopped: a comment mentioning %build expands
# to the whole build preamble, whose first line rpm then reads as a tag.
ran=$((ran + 1))
bare=$(awk '/^[[:space:]]*#/ {
        stripped = $0
        gsub(/%%/, "", stripped)
        if (stripped ~ /%/) printf "%s:%d: %s\n", FILENAME, FNR, $0
    }' "$spec")
if [ -z "$bare" ]; then
    echo "packaging-lint: spec comments escape their macros"
else
    echo "$bare" >&2
    echo "packaging-lint: FAIL a spec comment has an unescaped % (write %%)" >&2
    status=1
fi

if command -v desktop-file-validate >/dev/null 2>&1; then
    ran=$((ran + 1))
    # Sailfish's own keys are not in the freedesktop spec; each expected
    # warning is named, and anything else still fails.
    out=$(desktop-file-validate "$desktop" 2>&1 |
        grep -v 'value "silica-qt5" for key "X-Nemo-Application-Type"' |
        grep -v 'key "X-Nemo-Application-Type" .* is not known' || true)
    if [ -z "$out" ]; then
        echo "packaging-lint: harbour-tuuli.desktop valid"
    else
        echo "$out" >&2
        echo "packaging-lint: FAIL harbour-tuuli.desktop" >&2
        status=1
    fi
else
    skip desktop-file-validate "install desktop-file-utils"
fi

# Every build has to be a distinguishable package.  The spec pins Version
# and Release and mb2 runs with -X, so without a stamp in the workflow
# every build is harbour-tuuli-0.1.0-1 and `rpm -U` refuses it as already
# installed.
ran=$((ran + 1))
if grep -q '^Release:' "$spec" &&
    ! grep -q 'sed -i "s/\^Release:' "$root/.github/workflows/rpm.yml"; then
    echo "packaging-lint: FAIL the rpm workflow no longer stamps Release, so" \
         "every build would be the same NEVRA and refuse to install over" \
         "the last" >&2
    status=1
else
    echo "packaging-lint: the rpm workflow stamps a unique Release"
fi

# mb2 derives the package it is building from the directory it is run in,
# and then looks for rpm/<that>.spec.  The workflow mounts the checkout at
# a path it chooses, so that name and the spec's have to agree.
ran=$((ran + 1))
spec_base=$(basename "$spec" .spec)
# shellcheck disable=SC2016 # $home is the workflow's text, not ours.
builddir=$(sed -n 's|.*BUILDDIR=\$home/\([^"]*\)".*|\1|p' \
    "$root/.github/workflows/rpm.yml" | head -1)
if [ "$spec_base" = "$builddir" ]; then
    echo "packaging-lint: the rpm workflow builds in a directory named for the spec"
else
    echo "packaging-lint: FAIL rpm.yml mounts the checkout as '$builddir' but the" \
         "spec is rpm/$spec_base.spec; mb2 would not find it" >&2
    status=1
fi

# The icons are generated; a stale set is invisible until the launcher
# shows it.
if command -v python3 >/dev/null 2>&1; then
    ran=$((ran + 1))
    tmp=$(mktemp -d)
    cp -r "$root/icons" "$tmp/icons"
    if python3 "$root/tools/make-icons.py" "$tmp/icons" >/dev/null 2>&1 &&
        diff -rq "$root/icons" "$tmp/icons" >/dev/null; then
        echo "packaging-lint: icons match tools/make-icons.py"
    else
        echo "packaging-lint: FAIL icons/ differ from what tools/make-icons.py produces; regenerate" >&2
        status=1
    fi
    rm -rf "$tmp"
else
    skip python3 "install python3"
fi

# Every docs/<name>.md that a comment, a script or a document points at
# has to exist.
ran=$((ran + 1))
missing=$(grep -rhoE 'docs/[A-Za-z0-9_-]+\.md' "$root" \
        --include='*.rs' --include='*.qml' --include='*.js' --include='*.sh' \
        --include='*.yml' --include='*.toml' --include='*.md' --include='*.spec' \
        --include='.gitignore' --include='Makefile' --include='*.conf' \
        --exclude-dir=.git --exclude-dir=target --exclude-dir=vendor \
        --exclude-dir=third_party |
    sort -u | while read -r ref; do
        [ -f "$root/$ref" ] || echo "  $ref"
    done)
if [ -z "$missing" ]; then
    echo "packaging-lint: every docs/*.md referenced exists"
else
    echo "$missing" >&2
    echo "packaging-lint: FAIL these documents are referenced but do not exist; repoint or restore them" >&2
    status=1
fi

if command -v shellcheck >/dev/null 2>&1; then
    ran=$((ran + 1))
    if shellcheck "$root"/ci/*.sh "$root"/scripts/*.sh "$root"/servo/build.sh; then
        echo "packaging-lint: shell scripts clean"
    else
        echo "packaging-lint: FAIL shellcheck" >&2
        status=1
    fi
else
    skip shellcheck "install shellcheck"
fi

if [ "$ran" -eq 0 ]; then
    echo "packaging-lint: FAIL (no checker was available; this job proves nothing)" >&2
    exit 1
fi

exit "$status"
