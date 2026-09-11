#!/bin/bash
# ci/qml-lint.sh — QML rules from SCOPE.md §7 that host Qt would accept silently.
#
#  * qmllint clean (SKIP when the tool is missing, unless PACKAGING_LINT_STRICT=1)
#  * no console.* calls in shipped QML
#  * Theme values, never pixel counts
#  * Qt 5.6 only: import versions, no ES6/ES2015+, no Qt 5.7+ QML syntax
#  * every user-visible string translatable
#  * no file over 400 lines; no TODO/FIXME without an issue number
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
STRICT=${PACKAGING_LINT_STRICT:-0}
FAILED=0

fail() { # check-id location message
    printf 'ERROR [%s] [%s] %s\n' "$1" "$2" "$3"
    FAILED=1
}

skip() { # tool
    if [[ $STRICT == 1 ]]; then
        fail tool-missing "$1" "required in CI (PACKAGING_LINT_STRICT=1)"
    else
        echo "SKIP  [$1] not installed"
    fi
}

mapfile -t FILES < <(find "$ROOT/qml" -name '*.qml' | sort)
rel() { echo "${1#"$ROOT"/}"; }

# 1. qmllint
if command -v qmllint >/dev/null 2>&1; then
    for file in "${FILES[@]}"; do
        qmllint "$file" >/dev/null 2>&1 || fail qmllint "$(rel "$file")" "$(qmllint "$file" 2>&1 | head -1)"
    done
else
    skip qmllint
fi

# 2. console
while read -r hit; do
    fail console "${hit%%:*}" "console call in shipped QML: ${hit#*:}"
done < <(grep -nE '\bconsole\.' "${FILES[@]}" | sed "s|^$ROOT/||")

# 3. Qt 5.6 import versions (non-Qt modules are checked by ci/harbour-check.sh)
while read -r hit; do
    file=${hit%%:*}
    line=${hit#*:}
    module=$(awk '{print $2}' <<<"$line")
    version=$(awk '{print $3}' <<<"$line")
    major=${version%%.*}
    minor=${version#*.}
    case "$module" in
        QtQuick)
            [[ $major -eq 2 && $minor -le 6 ]] || fail qt56-import "$file" "QtQuick $version is newer than Qt 5.6 (max 2.6)"
            ;;
        QtQml)
            [[ $major -eq 2 && $minor -le 2 ]] || fail qt56-import "$file" "QtQml $version is newer than Qt 5.6 (max 2.2)"
            ;;
        QtQuick.Window)
            [[ $major -eq 2 && $minor -le 2 ]] || fail qt56-import "$file" "QtQuick.Window $version is newer than Qt 5.6 (max 2.2)"
            ;;
        QtQuick.Layouts)
            [[ $major -eq 1 && $minor -le 1 ]] || fail qt56-import "$file" "QtQuick.Layouts $version is newer than Qt 5.6 (max 1.1)"
            ;;
        QtQuick.Controls* | QtQuick.Templates* | Qt.labs.*)
            fail qt56-import "$file" "$module is not available on the device"
            ;;
    esac
done < <(grep -nE '^\s*import\s+Qt' "${FILES[@]}" | sed "s|^$ROOT/||")

# 4. Syntax the device engine rejects (ES2015+ and QML from Qt 5.7 onwards)
declare -A NEWER=(
    ['\blet\b']='ES2015 let'
    ['\bconst\b']='ES2015 const'
    ['=>']='ES2015 arrow function'
    ['`']='ES2015 template literal'
    ['\bclass\s+[A-Z]']='ES2015 class'
    ['\bfor\s*\(\s*(var\s+)?[A-Za-z_]\w*\s+of\b']='ES2015 for..of'
    ['\.(includes|padStart|padEnd|startsWith|endsWith|repeat)\(']='ES2015 string method'
    ['\bObject\.(assign|entries|values)\b']='ES2015 Object method'
    ['\bArray\.from\b']='ES2015 Array.from'
    ['\?\?']='ES2020 nullish coalescing'
    ['\?\.']='ES2020 optional chaining'
    ['\bQt\.callLater\b']='Qt.callLater needs Qt 5.8'
    ['^\s*required\s+property\b']='required property needs Qt 5.15'
    ['^\s*component\s+[A-Z]\w*\s*:']='inline component needs Qt 5.15'
    ['^\s*function\s+on[A-Z]\w*\s*\(']='function onFoo() in Connections needs Qt 5.15'
)
for pattern in "${!NEWER[@]}"; do
    while read -r hit; do
        fail qt56-syntax "${hit%%:*}" "${NEWER[$pattern]}: ${hit#*:}"
    done < <(grep -nE "$pattern" "${FILES[@]}" | sed "s|^$ROOT/||")
done

# 5. Theme values, never pixel counts (0 and 1 are not sizes)
SIZE_PROPS='(width|height|x|y|spacing|radius|padding|leftPadding|rightPadding|topPadding|bottomPadding|contentWidth|contentHeight|implicitWidth|implicitHeight|font\.pixelSize|(anchors\.)?(margins|leftMargin|rightMargin|topMargin|bottomMargin))'
while read -r hit; do
    value=$(sed -E 's/.*:\s*//' <<<"${hit#*:*:}")
    [[ $value =~ ^-?[01](\.0+)?$ ]] && continue
    fail pixel-count "${hit%%:*}" "size bound to a pixel count, use Theme values: ${hit#*:}"
done < <(grep -nE "^\s*$SIZE_PROPS\s*:\s*-?[0-9]+(\.[0-9]+)?\s*$" "${FILES[@]}" | sed "s|^$ROOT/||")

# 6. Translatable strings: user-visible text properties must go through qsTr
TEXT_PROPS='(text|title|label|placeholderText|description|hintText|acceptText|cancelText)'
while read -r hit; do
    fail untranslated "${hit%%:*}" "user-visible string is not translatable: ${hit#*:}"
done < <(grep -nE "^\s*$TEXT_PROPS\s*:\s*\"[^\"]+\"\s*$" "${FILES[@]}" | sed "s|^$ROOT/||")

# 7. File size
for file in "${FILES[@]}"; do
    lines=$(wc -l <"$file")
    [[ $lines -le 400 ]] || fail file-size "$(rel "$file")" "$lines lines; files over 400 lines need an ADR (docs/DECISIONS)"
done

# 8. TODO without an issue number, across shipped sources and tests
while read -r hit; do
    fail todo "${hit%%:*}" "TODO/FIXME must reference an issue as TODO(#123): ${hit#*:}"
done < <(grep -rnE '\b(TODO|FIXME|XXX)\b' "$ROOT/src" "$ROOT/qml" "$ROOT/tests" "$ROOT/docs" 2>/dev/null | grep -vE '(TODO|FIXME|XXX)\(#[0-9]+\)' | sed "s|^$ROOT/||")

if [[ $FAILED -eq 0 ]]; then
    echo "qml-lint: ${#FILES[@]} files clean"
    exit 0
fi
exit 1
