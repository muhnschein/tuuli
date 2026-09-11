#!/bin/bash
# ci/harbour-check.sh — Harbour rules applied to the source tree.
#
# Reimplements the checks of Jolla's sdk-harbour-rpmvalidator (rpmvalidation.sh) over
# the sources, so every pull request sees them without building an RPM. It is not the
# authority: `sfdk check -s harbour` on the built package is (docs/HARBOUR.md). The
# allow-lists in ci/harbour/ are the validator's own files, copied verbatim.
#
# Usage: ci/harbour-check.sh [--root DIR]
# Exit status: 0 when no ERROR remains after ci/harbour/waivers.conf, 1 otherwise.
# shellcheck disable=SC2317  # validators are invoked through run_section
set -uo pipefail
shopt -s extglob

usage() {
    echo "usage: $0 [--root DIR]" >&2
    exit 2
}

ROOT=$(cd "$(dirname "$0")/.." && pwd)
while [[ $# -gt 0 ]]; do
    case "$1" in
        --root)
            [[ $# -ge 2 ]] || usage
            ROOT=$(cd "$2" && pwd) || exit 2
            shift 2
            ;;
        *) usage ;;
    esac
done

CONF_DIR=$ROOT/ci/harbour
SPEC=$(find "$ROOT/rpm" -maxdepth 1 -name '*.spec' | sort | head -1)
SPEC_REL=${SPEC#"$ROOT"/}
ICON_SIZES="86x86 108x108 128x128 172x172"
NAME_REGEX='^harbour-[-a-z0-9_.]+$'

ERRORS=0
WARNINGS=0
WAIVED=0
SECTION_ERRORS=0
NAME=""
USES_SILICA=0
USES_QML_LAUNCHER=0
declare -a WAIVER_IDS=() WAIVER_PATTERNS=()

# ---------------------------------------------------------------- reporting
color() {
    local code=$1
    shift
    if [[ -t 1 && -z ${NO_COLOR:-} ]]; then
        printf '\e[%sm%s\e[0m' "$code" "$*"
    else
        printf '%s' "$*"
    fi
}

load_waivers() {
    local file=$CONF_DIR/waivers.conf line
    [[ -f $file ]] || return 0
    while IFS= read -r line; do
        line=${line%%#*}
        line=${line##+([[:space:]])}
        line=${line%%+([[:space:]])}
        [[ -z $line ]] && continue
        WAIVER_IDS+=("${line%% *}")
        WAIVER_PATTERNS+=("${line#* }")
    done <"$file"
}

waived() { # id message
    local i
    for i in "${!WAIVER_IDS[@]}"; do
        # shellcheck disable=SC2053
        if [[ ${WAIVER_IDS[$i]} == "$1" && $2 == ${WAIVER_PATTERNS[$i]} ]]; then
            return 0
        fi
    done
    return 1
}

error() { # id location message
    if waived "$1" "$3"; then
        printf '%s [%s] [%s] %s\n' "$(color 36 WAIVED)" "$1" "$2" "$3"
        WAIVED=$((WAIVED + 1))
    else
        printf '%s [%s] [%s] %s\n' "$(color 31 ERROR)" "$1" "$2" "$3"
        ERRORS=$((ERRORS + 1))
        SECTION_ERRORS=$((SECTION_ERRORS + 1))
    fi
}

warning() { # id location message
    printf '%s [%s] [%s] %s\n' "$(color 33 WARNING)" "$1" "$2" "$3"
    WARNINGS=$((WARNINGS + 1))
}

info() { # id location message
    printf '%s [%s] [%s] %s\n' "$(color 36 INFO)" "$1" "$2" "$3"
}

run_section() { # title function
    SECTION_ERRORS=0
    echo "$1"
    printf '%s\n' "$1" | sed 's/./=/g'
    "$2"
    if [[ $SECTION_ERRORS -eq 0 ]]; then
        color 32 PASSED
    else
        color 31 FAILED
    fi
    printf '\n\n'
}

# Same semantics as rpmvalidation.sh: extglob patterns, one per line.
check_contained_in() { # query conf-files...
    local query=$1 pat
    shift
    while read -r pat; do
        [[ -z $pat || $pat == \#* ]] && continue
        # shellcheck disable=SC2053
        [[ $query == $pat ]] && return 0
        [[ $query == "$pat()(64bit)" ]] && return 0
    done < <(cat "$@")
    return 1
}

conf() { echo "$CONF_DIR/$1"; }

spec_tag() { grep -E "^$1:" "$SPEC" | head -1 | sed -E "s/^$1:[[:space:]]*//"; }
spec_tags() { grep -E "^$1:" "$SPEC" | sed -E "s/^$1:[[:space:]]*//"; }

# Names from a dependency line: drops comparison operators and the version after them.
dependency_names() {
    local token skip=0
    for token in $(tr ',' ' ' <<<"$1"); do
        if [[ $skip -eq 1 ]]; then
            skip=0
            continue
        fi
        case "$token" in
            '>=' | '<=' | '=' | '>' | '<') skip=1 ;;
            *) echo "$token" ;;
        esac
    done
}

# --------------------------------------------------------------- validators
validate_names() {
    if [[ -z $SPEC ]]; then
        error package-name rpm/ "no spec file found"
        return
    fi
    NAME=$(spec_tag Name)
    if [[ ! $NAME =~ $NAME_REGEX ]]; then
        error package-name "$SPEC_REL" "Name '$NAME' is not valid, must match $NAME_REGEX"
    fi
    [[ -f $ROOT/$NAME.desktop ]] || error package-name "$NAME.desktop" "desktop file must be named after the package"
}

validate_rpm_metadata() {
    local version release tag value provide
    version=$(spec_tag Version)
    release=$(spec_tag Release)
    [[ $version =~ ^[0-9.]+$ ]] || error rpm-version "$SPEC_REL" "Version '$version' must contain only digits and periods"
    [[ $release =~ ^[0-9._]+$ ]] || error rpm-release "$SPEC_REL" "Release '$release' must contain only digits, underscores and periods"
    [[ -z $(spec_tag Vendor) ]] || error vendor "$SPEC_REL" "Vendor must not be set"
    [[ $(spec_tag BuildArch) != noarch ]] || error arch "$SPEC_REL" "an application with a binary cannot be noarch"

    while read -r tag; do
        error scriptlet "$SPEC_REL" "RPM '$tag' script not allowed"
    done < <(grep -oE '^%(pre|post|preun|postun|pretrans|posttrans|verifyscript)\b' "$SPEC")
    while read -r tag; do
        error trigger "$SPEC_REL" "RPM '$tag' trigger not allowed"
    done < <(grep -oE '^%(trigger|filetrigger)[a-z]*' "$SPEC")
    for tag in Obsoletes Conflicts Recommends Suggests Supplements Enhances; do
        while read -r value; do
            [[ -n $value ]] && error dependency-type "$SPEC_REL" "'$tag: $value' not allowed"
        done < <(spec_tags "$tag")
    done
    while read -r value; do
        for provide in $(dependency_names "$value"); do
            case "$provide" in
                "$NAME"* | 'application()' | "application($NAME.desktop)" | mimehandler\(*\)) ;;
                *) error provides "$SPEC_REL" "'Provides: $provide' not allowed" ;;
            esac
        done
    done < <(spec_tags Provides)
}

validate_qml_files() {
    local file line statement import path real share_real
    share_real=$(readlink -f "$ROOT/qml")
    while read -r file; do
        while read -r line; do
            while IFS=';' read -ra statements; do
                for statement in "${statements[@]}"; do
                    import=$(sed -e 's/^\s*import/import/' -e 's/\s\+/ /g' -e 's/ as .*$//' -e 's/;$//' <<<"$statement" | cut -f2-3 -d ' ')
                    [[ -z ${import// /} ]] && continue
                    if [[ $import == "Sailfish.Silica 1.0" ]]; then
                        USES_SILICA=1
                    fi
                    if check_contained_in "$import" "$(conf allowed_qmlimports.conf)"; then
                        continue
                    elif check_contained_in "$import" "$(conf deprecated_qmlimports.conf)"; then
                        warning qml-import "${file#"$ROOT"/}" "import '$import' is deprecated"
                        continue
                    elif [[ $import =~ ^[\"\'](.*)[\"\'] ]]; then
                        path=${BASH_REMATCH[1]}
                        if [[ ${path:0:1} == / ]]; then
                            error qml-import "${file#"$ROOT"/}" "import '$import' is not valid - absolute path imports are forbidden"
                        elif [[ $path == qrc:/* ]]; then
                            continue
                        elif [[ -e $(dirname "$file")/$path ]]; then
                            real=$(readlink -f "$(dirname "$file")/$path")
                            [[ $real == "$share_real"* ]] || error qml-import "${file#"$ROOT"/}" "import '$import' points outside the application directory"
                        else
                            error qml-import "${file#"$ROOT"/}" "import '$import' points to an unsupported external path"
                        fi
                    elif check_contained_in "$import" "$(conf disallowed_qmlimport_patterns.conf)"; then
                        error qml-import "${file#"$ROOT"/}" "import '$import' is not allowed"
                    fi
                done
            done <<<"$line"
        done < <(grep -e '^[[:space:]]*import[[:space:]]' "$file" | sed -e 's/\x0D$//')
    done < <(find "$ROOT/qml" -name '*.qml' | sort)
    [[ $USES_SILICA -eq 1 ]] && info qml-import qml/ "uses Sailfish Silica components"
}

validate_sailjail_key() { # line
    local key=${1%%=*} value=${1#*=} permission
    if ! check_contained_in "$key" "$(conf allowed_sailjailkeys.conf)"; then
        error sailjail-key "$NAME.desktop" "X-Sailjail key is not allowed: $key"
        return
    fi
    case "$key" in
        Permissions)
            IFS=';' read -ra permissions <<<"$value"
            for permission in "${permissions[@]}"; do
                check_contained_in "$permission" "$(conf allowed_permissions.conf)" || error sailjail-permission "$NAME.desktop" "X-Sailjail permission not allowed: $permission"
                [[ $permission == Compatibility ]] && warning sailjail-permission "$NAME.desktop" "Compatibility permission used"
            done
            ;;
        OrganizationName)
            [[ $value =~ ^[0-9a-z._-]+$ ]] || error sailjail-orgname "$NAME.desktop" "OrganizationName contains illegal characters: $value"
            [[ $value =~ (^|[.])[0-9] ]] && error sailjail-orgname "$NAME.desktop" "OrganizationName component may not start with a number: $value"
            check_contained_in "$value" "$(conf disallowed_orgnames.conf)" && error sailjail-orgname "$NAME.desktop" "OrganizationName not allowed: $value"
            ;;
        ApplicationName)
            [[ $value =~ ^[A-Za-z_-][A-Z0-9a-z_-]*$ ]] || error sailjail-appname "$NAME.desktop" "ApplicationName contains illegal characters: $value"
            ;;
        ExecDBus)
            if [[ $USES_QML_LAUNCHER -eq 0 ]]; then
                [[ $value =~ ^${NAME}(|[[:space:]]+[A-Za-z_-][A-Z0-9a-z_-]*)$ ]] || error sailjail-execdbus "$NAME.desktop" "ExecDBus has invalid argument(s): $value"
            else
                [[ $value =~ ^sailfish-qml[[:space:]]+${NAME}(|[[:space:]]+[A-Za-z_-][A-Z0-9a-z_-]*)$ ]] || error sailjail-execdbus "$NAME.desktop" "ExecDBus has invalid argument(s): $value"
            fi
            ;;
    esac
}

validate_desktop_file() {
    local desktop=$ROOT/$NAME.desktop found=0 line
    if [[ ! -f $desktop ]]; then
        error desktop-file "$NAME.desktop" "file is missing"
        return
    fi
    grep -qE '^Name=.+' "$desktop" || error desktop-name "$NAME.desktop" "missing valid Name declaration"
    grep -qE "^Icon=${NAME}[[:space:]]*$" "$desktop" || error desktop-icon "$NAME.desktop" "missing valid Icon declaration, must be Icon=$NAME"
    grep -qE "^Exec(=|=sailfish-qml[[:space:]]+)${NAME}" "$desktop" || error desktop-exec "$NAME.desktop" "missing valid Exec declaration, must be Exec=$NAME"
    grep -qE "^Exec=sailfish-qml[[:space:]]+${NAME}" "$desktop" && USES_QML_LAUNCHER=1
    grep -qE '^Type=Application[[:space:]]*$' "$desktop" || error desktop-type "$NAME.desktop" "missing valid Type declaration"
    grep -qE '^\[Sailjail\]$' "$desktop" && error sailjail-section "$NAME.desktop" "Sailjail section not allowed (use X-Sailjail instead)"

    if grep -qE '^\[X-Sailjail\]$' "$desktop"; then
        while read -r line; do
            [[ $line =~ ^[^#].* ]] || continue
            validate_sailjail_key "$line"
            found=1
        done < <(sed '1,/^\[X-Sailjail\]/d;/\[/,$d' "$desktop")
        [[ $found -eq 1 ]] || error sailjail-section "$NAME.desktop" "empty X-Sailjail section not allowed"
    else
        warning sailjail-section "$NAME.desktop" "X-Sailjail section not found"
    fi

    if ! grep -qE '^X-Nemo-Application-Type=silica-qt5[[:space:]]*$' "$desktop"; then
        if [[ $USES_QML_LAUNCHER -eq 1 ]]; then
            error desktop-app-type "$NAME.desktop" "X-Nemo-Application-Type must be silica-qt5 for sailfish-qml apps"
        elif grep -qE '^X-Nemo-Application-Type=(no-invoker|generic|qtquick2|qt5)[[:space:]]*$' "$desktop"; then
            warning desktop-app-type "$NAME.desktop" "X-Nemo-Application-Type should be silica-qt5 (not a Silica app?)"
        else
            error desktop-app-type "$NAME.desktop" "X-Nemo-Application-Type not declared (use silica-qt5 for QML apps)"
        fi
    fi
}

expand_files_line() { # %files entry -> absolute path
    local path=$1
    path=${path//%\{name\}/$NAME}
    path=${path//%\{_bindir\}//usr/bin}
    path=${path//%\{_datadir\}//usr/share}
    path=${path//%\{_libdir\}//usr/lib64}
    path=${path//%\{_prefix\}//usr}
    path=${path//%\{_sysconfdir\}//etc}
    path=${path//%\{_libexecdir\}//usr/libexec}
    path=${path//%\{_sharedstatedir\}//var/lib}
    echo "$path"
}

install_path_allowed() { # absolute path (may contain * for the icon size)
    case "$1" in
        "/usr/bin/$NAME" | "/usr/share/$NAME" | "/usr/share/$NAME/"* | "/usr/share/applications/$NAME.desktop") return 0 ;;
    esac
    # shellcheck disable=SC2053
    [[ $1 == /usr/share/icons/hicolor/@(\*|+([0-9])x+([0-9]))/apps/$NAME.png ]] && return 0
    return 1
}

validate_paths() {
    local line path dest cmake file size found=0 missing=0
    # %files: every entry must live where Harbour allows.
    while read -r line; do
        line=${line%%#*}
        [[ -z ${line// /} ]] && continue
        case "$line" in
            %defattr*) continue ;;
            %doc* | %license*) error install-path "$SPEC_REL" "'$line' installs outside the application directories" ; continue ;;
            %dir\ *) line=${line#%dir } ;;
            %[a-z]*) error install-path "$SPEC_REL" "unsupported %files directive: $line"; continue ;;
        esac
        path=$(expand_files_line "$line")
        if [[ $path == *%\{* ]]; then
            error install-path "$SPEC_REL" "unexpanded macro in %files entry: $line"
        elif ! install_path_allowed "$path"; then
            error install-path "$SPEC_REL" "installation not allowed in this location: $path"
        fi
    done < <(awk '/^%files/ { active = 1; next }
                  /^%(package|description|prep|build|install|check|clean|changelog|pre|post|preun|postun|pretrans|posttrans|trigger|filetrigger|verifyscript)([[:space:]]|$)/ { active = 0 }
                  active' "$SPEC")

    # CMake install destinations must agree.
    while read -r cmake; do
        while read -r dest; do
            dest=${dest//\$\{PROJECT_NAME\}/$NAME}
            dest=${dest//\$\{size\}/\*}
            [[ $dest == /* ]] || dest=/usr/$dest
            case "$dest" in
                /usr/bin | "/usr/share/$NAME" | "/usr/share/$NAME/"* | /usr/share/applications) ;;
                /usr/share/icons/hicolor/*/apps) ;;
                *) error install-path "${cmake#"$ROOT"/}" "install DESTINATION not allowed: $dest" ;;
            esac
        done < <(grep -oE 'DESTINATION[[:space:]]+[^ )]+' "$cmake" | awk '{print $2}')
    done < <(find "$ROOT" -name CMakeLists.txt -not -path '*/tests/*' -not -path '*/build*/*' | sort)

    # The qml directory is installed as-is: nothing but QML may be in it.
    while read -r file; do
        error stray-file "${file#"$ROOT"/}" "this kind of file must not be included"
    done < <(find "$ROOT/qml" \( -type f ! -name '*.qml' \) -o -name '.git' -o -name '.svn' -o -name '.DS_Store' -o -name '*~' -o -name '.*.swp')
    while read -r file; do
        error file-mode "${file#"$ROOT"/}" "installed data file must not be executable"
    done < <(find "$ROOT/qml" "$ROOT/icons" -type f \( -name '*.qml' -o -name '*.png' \) -perm /111)

    # Mandatory files.
    [[ -f $ROOT/src/main.cpp ]] || error paths src/main.cpp "application entry point missing"
    for size in $ICON_SIZES; do
        if [[ -f $ROOT/icons/$size/$NAME.png ]]; then
            found=1
        else
            missing=1
            warning icon-missing "icons/$size/$NAME.png" "icon not found"
        fi
    done
    if [[ $found -eq 0 ]]; then
        error icon icons/ "no icons found, at least one of $ICON_SIZES is required"
    elif [[ $missing -eq 1 ]]; then
        warning icon icons/ "not all icons found; recommended sizes: $ICON_SIZES"
    fi
}

validate_icons() {
    local size icon type expected
    if ! command -v file >/dev/null 2>&1; then
        warning icon-size icons/ "'file' not available, icon sizes not verified"
        return
    fi
    for size in $ICON_SIZES; do
        icon=$ROOT/icons/$size/$NAME.png
        [[ -s $icon ]] || continue
        type=$(file -b "$icon")
        expected="PNG image data, ${size/x/ x },"
        case "$type" in
            "$expected"*) ;;
            PNG*) error icon-size "icons/$size/$NAME.png" "wrong size, must be $size (detected '$type')" ;;
            *) error icon-size "icons/$size/$NAME.png" "must be a PNG image (detected '$type')" ;;
        esac
    done
}

validate_libraries() {
    local cmake=$ROOT/src/CMakeLists.txt module lib
    while read -r module; do
        lib="libQt5$module.so.5"
        check_contained_in "$lib" "$(conf allowed_libraries.conf)" || error library src/CMakeLists.txt "cannot link to shared library: $lib"
    done < <(grep -ohE 'Qt5::[A-Za-z]+' "$cmake" "$ROOT/CMakeLists.txt" | sed 's/Qt5:://' | sort -u)
    while read -r module; do
        case "$module" in
            sailfishapp) lib=libsailfishapp.so.1 ;;
            sailfishwebengine) lib=libsailfishwebengine.so.1 ;;
            qt5embedwidget) lib=libqt5embedwidget.so.1 ;;
            mlite5) lib=libmlite5.so.0 ;;
            nemonotifications-qt5) lib=libnemonotifications-qt5.so.1 ;;
            keepalive) lib=libkeepalive.so.1 ;;
            *) error library src/CMakeLists.txt "pkg-config module '$module' has no library mapping in ci/harbour-check.sh"; continue ;;
        esac
        check_contained_in "$lib" "$(conf allowed_libraries.conf)" || error library src/CMakeLists.txt "cannot link to shared library: $lib"
    done < <(grep -oE 'pkg_check_modules\([^)]*\)' "$cmake" | sed -E 's/.*[[:space:]]([A-Za-z0-9_-]+)\)$/\1/' | sort -u)
}

validate_symbols() {
    grep -q 'Q_DECL_EXPORT int main' "$ROOT/src/main.cpp" || error main-export src/main.cpp "main() must be exported (Q_DECL_EXPORT) for the booster"
    grep -qE 'target_link_options\(.*-rdynamic' "$ROOT/src/CMakeLists.txt" || error link-flags src/CMakeLists.txt "binary must be linked with -rdynamic"
    grep -qE 'target_link_options\(.*-pie' "$ROOT/src/CMakeLists.txt" || error link-flags src/CMakeLists.txt "binary must be linked with -pie"
}

validate_requires() {
    local value require launcher=0
    while read -r value; do
        for require in $(dependency_names "$value"); do
            if check_contained_in "$require" "$(conf allowed_libraries.conf)" "$(conf allowed_requires.conf)"; then
                continue
            elif check_contained_in "$require" "$(conf deprecated_libraries.conf)" "$(conf deprecated_requires.conf)"; then
                warning requires "$SPEC_REL" "dependency is deprecated: $require"
                continue
            fi
            case "$require" in
                rpmlib\(*\) | 'rtld(GNU_HASH)') ;;
                libsailfishapp-launcher)
                    launcher=1
                    [[ $USES_QML_LAUNCHER -eq 1 ]] || error requires "$SPEC_REL" "invalid 'Requires: $require' (sailfish-qml launcher not used)"
                    ;;
                lib*.so.* | lib*.so) error requires "$SPEC_REL" "cannot require shared library: $require" ;;
                *) error requires "$SPEC_REL" "dependency not allowed: $require" ;;
            esac
        done
    done < <(spec_tags Requires)
    [[ $USES_QML_LAUNCHER -eq 1 && $launcher -eq 0 ]] && error requires "$SPEC_REL" "add 'Requires: libsailfishapp-launcher' for sailfish-qml apps"
}

validate_sandboxing() {
    local hit
    while read -r hit; do
        error hardcoded-path "${hit%%:*}" "hardcoded home path: ${hit#*:}"
    done < <(grep -rnE '/home/(nemo|defaultuser)/' "$ROOT/src" "$ROOT/qml" | sed "s|^$ROOT/||")
    # Sailjail only grants writes below the app-specific standard locations.
    while read -r hit; do
        error path-policy "${hit%%:*}" "location not writable under Sailjail: ${hit#*:}"
    done < <(grep -rnoE 'QStandardPaths::(GenericDataLocation|GenericConfigLocation|GenericCacheLocation|HomeLocation|ConfigLocation|DataLocation)\b' "$ROOT/src" | sed "s|^$ROOT/||")
    # Default-constructed or organisation-keyed QSettings land outside the sandbox;
    # members (m_*) are constructed in initialiser lists and are not decidable here.
    while read -r hit; do
        error path-policy "${hit%%:*}" "QSettings must be given an explicit file path: ${hit#*:}"
    done < <(grep -rnE 'QSettings[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*;|QSettings[[:space:]]*\([[:space:]]*\)|QSettings[[:space:]]*\([[:space:]]*QSettings::' "$ROOT/src" | grep -vE ':[[:space:]]*(//|\*)|QSettings[[:space:]]+m_' | sed "s|^$ROOT/||")
}

# ---------------------------------------------------------------------- main
load_waivers
run_section "Package name" validate_names
run_section "RPM metadata" validate_rpm_metadata
run_section "QML files" validate_qml_files
run_section "Desktop file" validate_desktop_file
run_section "Paths" validate_paths
run_section "Icons" validate_icons
run_section "Libraries" validate_libraries
run_section "Symbols" validate_symbols
run_section "Requires" validate_requires
run_section "Sandboxing" validate_sandboxing

echo "harbour-check: $ERRORS error(s), $WARNINGS warning(s), $WAIVED waived"
[[ $ERRORS -eq 0 ]] || exit 1
exit 0
