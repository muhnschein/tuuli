#!/bin/bash
# ci/harbour-check-selftest.sh — breaks each Harbour rule in a throwaway copy of the
# tree and asserts that ci/harbour-check.sh names it. Keeps the checker honest.
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CHECK=$ROOT/ci/harbour-check.sh
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
TREE=$WORK/tree
SPEC=$TREE/rpm/harbour-tuuli.spec
DESKTOP=$TREE/harbour-tuuli.desktop
FAILED=0
CASES=0

fresh() {
    rm -rf "$TREE"
    mkdir -p "$TREE"
    cp -r "$ROOT/ci" "$ROOT/qml" "$ROOT/rpm" "$ROOT/src" "$ROOT/icons" \
        "$ROOT/CMakeLists.txt" "$ROOT/harbour-tuuli.desktop" "$TREE/"
}

run_check() {
    NO_COLOR=1 "$CHECK" --root "$TREE" 2>&1
}

# expect <error|warning|waived|clean> <check-id> <description>
expect() {
    local kind=$1 id=$2 desc=$3 output status ok=0
    CASES=$((CASES + 1))
    output=$(run_check)
    status=$?
    case "$kind" in
        error) [[ $status -eq 1 ]] && grep -q "^ERROR \[$id\]" <<<"$output" && ok=1 ;;
        warning) [[ $status -eq 0 ]] && grep -q "^WARNING \[$id\]" <<<"$output" && ok=1 ;;
        waived) [[ $status -eq 0 ]] && grep -q "^WAIVED \[$id\]" <<<"$output" && ok=1 ;;
        clean) [[ $status -eq 0 ]] && ! grep -q '^ERROR' <<<"$output" && ok=1 ;;
    esac
    if [[ $ok -eq 1 ]]; then
        echo "ok   $desc"
    else
        echo "FAIL $desc (expected $kind [$id], exit $status)"
        grep -E '^(ERROR|WARNING|WAIVED)' <<<"$output" | sed 's/^/     /'
        FAILED=1
    fi
}

add_import() { # file import-line
    sed -i "0,/^import /s//$2\nimport /" "$1"
}

fresh
expect clean - "pristine tree passes"

fresh; sed -i 's/^Name:.*/Name:       tuuli/' "$SPEC"
expect error package-name "package name without harbour- prefix"

fresh; sed -i 's/^Version:.*/Version:    1.0-beta/' "$SPEC"
expect error rpm-version "version with a dash"

fresh; sed -i 's/^Release:.*/Release:    1a/' "$SPEC"
expect error rpm-release "release with a letter"

fresh; sed -i 's/^Summary:.*/&\nVendor:     someone/' "$SPEC"
expect error vendor "vendor set"

fresh; printf '\n%%post\nls\n' >>"$SPEC"
expect error scriptlet "post scriptlet"

fresh; printf '\n%%triggerin -- foo\nls\n' >>"$SPEC"
expect error trigger "trigger"

fresh; sed -i 's/^Summary:.*/&\nObsoletes:  oldbrowser/' "$SPEC"
expect error dependency-type "Obsoletes"

fresh; sed -i 's/^Summary:.*/&\nProvides:   libfoo.so.1/' "$SPEC"
expect error provides "Provides of a library"

fresh; sed -i 's/^Summary:.*/&\nRequires:   python3-foo/' "$SPEC"
expect error requires "Requires outside the allow-list"

fresh; sed -i 's/^Summary:.*/&\nRequires:   libfoo.so.1/' "$SPEC"
expect error requires "Requires of a bare shared library"

fresh; sed -i 's/^Summary:.*/&\nBuildArch:  noarch/' "$SPEC"
expect error arch "noarch with a binary"

fresh; add_import "$TREE/qml/pages/TabsPage.qml" "import QtQuick.Controls 1.0"
expect error qml-import "disallowed QML import"

fresh; add_import "$TREE/qml/pages/TabsPage.qml" 'import "\/usr\/share\/other"'
expect error qml-import "absolute path QML import"

fresh; add_import "$TREE/qml/pages/TabsPage.qml" 'import "..\/..\/src"'
expect error qml-import "QML import outside the application directory"

fresh; add_import "$TREE/qml/pages/TabsPage.qml" "import org.nemomobile.notifications 1.0"
expect warning qml-import "deprecated QML import"

fresh; sed -i 's/^Icon=.*/Icon=other/' "$DESKTOP"
expect error desktop-icon "desktop Icon not the package name"

fresh; sed -i 's/^Exec=.*/Exec=other/' "$DESKTOP"
expect error desktop-exec "desktop Exec not the package name"

fresh; sed -i '/^Type=/d' "$DESKTOP"
expect error desktop-type "desktop Type missing"

fresh; sed -i 's/^\[X-Sailjail\]/[Sailjail]/' "$DESKTOP"
expect error sailjail-section "legacy Sailjail section"

fresh; sed -i 's/^Permissions=.*/Permissions=Internet;Root/' "$DESKTOP"
expect error sailjail-permission "unknown Sailjail permission"

fresh; printf 'Foo=bar\n' >>"$DESKTOP"
expect error sailjail-key "unknown X-Sailjail key"

fresh; sed -i 's/^OrganizationName=.*/OrganizationName=com.jolla/' "$DESKTOP"
expect error sailjail-orgname "reserved OrganizationName"

fresh; sed -i 's/^OrganizationName=.*/OrganizationName=1abc.def/' "$DESKTOP"
expect error sailjail-orgname "OrganizationName component starting with a digit"

fresh; sed -i 's/^ApplicationName=.*/ApplicationName=my app/' "$DESKTOP"
expect error sailjail-appname "ApplicationName with a space"

fresh; sed -i '/^X-Nemo-Application-Type=/d' "$DESKTOP"
expect error desktop-app-type "application type missing"

fresh; sed -i '/^\[X-Sailjail\]/,$d' "$DESKTOP"
expect warning sailjail-section "X-Sailjail section missing"

fresh; rm "$TREE/icons/86x86/harbour-tuuli.png"
expect warning icon-missing "one icon missing"

fresh; rm -r "$TREE/icons"
expect error icon "all icons missing"

fresh; cp "$TREE/icons/108x108/harbour-tuuli.png" "$TREE/icons/86x86/harbour-tuuli.png"
expect error icon-size "icon of the wrong size"

fresh; printf '%%{_libdir}/libfoo.so\n' >>"$SPEC"
expect error install-path "file installed under libdir"

fresh; printf '%%doc README.md\n' >>"$SPEC"
expect error install-path "%doc entry"

fresh; printf 'install(FILES x DESTINATION lib)\n' >>"$TREE/CMakeLists.txt"
expect error install-path "CMake install outside the application directories"

fresh; printf 'notes\n' >"$TREE/qml/notes.txt"
expect error stray-file "non-QML file in qml/"

fresh; chmod +x "$TREE/qml/harbour-tuuli.qml"
expect error file-mode "executable QML file"

fresh; printf 'target_link_libraries(harbour-tuuli PRIVATE Qt5::WebEngine)\n' >>"$TREE/src/CMakeLists.txt"
expect error library "link against a library outside the allow-list"

fresh; printf 'pkg_check_modules(FOO IMPORTED_TARGET foo)\n' >>"$TREE/src/CMakeLists.txt"
expect error library "unknown pkg-config module"

fresh; sed -i 's/Q_DECL_EXPORT int main/int main/' "$TREE/src/main.cpp"
expect error main-export "main() not exported"

fresh; sed -i 's/-rdynamic//' "$TREE/src/CMakeLists.txt"
expect error link-flags "binary without -rdynamic"

fresh; printf 'const char *const Home = "/home/nemo/Documents";\n' >>"$TREE/src/Core.cpp"
expect error hardcoded-path "hardcoded home path"

fresh; printf 'static const int Loc = QStandardPaths::HomeLocation;\n' >>"$TREE/src/Core.cpp"
expect error path-policy "standard location outside the sandbox"

fresh; printf 'void probe() { QSettings settings; }\n' >>"$TREE/src/Core.cpp"
expect error path-policy "QSettings without a file path"

fresh; sed -i 's/^Summary:.*/&\nVendor:     someone/' "$SPEC"
printf 'vendor Vendor must not be set # selftest waiver\n' >>"$TREE/ci/harbour/waivers.conf"
expect waived vendor "waived finding reported as WAIVED"

echo "harbour-check-selftest: $CASES cases, $([[ $FAILED -eq 0 ]] && echo all passed || echo FAILED)"
exit $FAILED
