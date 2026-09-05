#!/bin/bash
# Prove that ci/harbour-check.sh still fails what it claims to fail.
#
# A gate that only ever prints "ok" is indistinguishable from a gate that
# has stopped looking, and every check in there is a regex over a file
# format someone will reformat one day.  So each case below breaks one
# Harbour rule in a throwaway copy of the tree and asserts the check names
# it.
#
# The binary is not copied: the checks that need it report SKIP, which is
# not what any case asserts.
set -u

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Straight into place, dotfiles and all: .github/workflows/rpm.yml is one
# of the things a case breaks.
pristine="$work/pristine"
mkdir -p "$pristine"
tar -C "$root" -cf - \
    --exclude=./target --exclude=./.git --exclude=./third_party \
    --exclude='./servo/app/target' --exclude='./servo/backend/target' \
    --exclude=./servo/src --exclude=./servo/out \
    . 2>/dev/null | tar -C "$pristine" -xf -
if [ ! -d "$pristine/ci" ] || [ ! -f "$pristine/.github/workflows/rpm.yml" ]; then
    echo "selftest: FAIL could not stage a copy of the tree" >&2
    exit 1
fi

S='rpm/harbour-tuuli.spec'
D='src/app/harbour-tuuli.desktop'
Q='src/qml/cover/CoverPage.qml'
P='crates/tuuli-core/src/paths.rs'

if [ ! -f "$pristine/$S" ] || [ ! -f "$pristine/$D" ] || [ ! -f "$pristine/$Q" ]; then
    echo "selftest: FAIL the spec, the .desktop file or the QML is not where this test expects it" >&2
    exit 1
fi

status=0
cases=0

# break <expected id> <description> <shell command run inside the copy>
break_and_expect() {
    local expect=$1 what=$2 script=$3
    local dir="$work/case"
    cases=$((cases + 1))

    rm -rf "$dir"
    cp -a "$pristine" "$dir"
    ( cd "$dir" && eval "$script" ) || {
        echo "selftest: FAIL could not apply '$what'" >&2
        status=1
        return
    }

    local out
    out=$("$dir/ci/harbour-check.sh" 2>&1)
    if grep -qF "FAIL [$expect]" <<< "$out"; then
        echo "selftest: ok   $what -> $expect"
    else
        echo "selftest: FAIL $what should have been reported as $expect" >&2
        grep -E '^harbour-check: (FAIL|WAIVED)' <<< "$out" >&2 || echo "  (nothing failed)" >&2
        status=1
    fi
}

break_and_expect 1.1.1 "package name without the harbour- prefix" \
    "sed -i 's/^Name:.*/Name:       tuuli/' $S"
break_and_expect 1.1.3 "Version with a letter in it" \
    "sed -i 's/^Version:.*/Version:    0.1.0rc1/' $S"
break_and_expect 1.1.4 "Release with a letter in it" \
    "sed -i 's/^Release:.*/Release:    1.gabc123/' $S"
# No unescaped $ in a case script: it is run through eval, which would
# expand a shell variable in the pattern before sed ever saw it.
break_and_expect 1.1.4 "an rpm workflow that stamps a git hash into Release" \
    "sed -i 's|^ *release=.*|          release=\"1.17.gabc1234\"|' .github/workflows/rpm.yml"
break_and_expect 1.1.5 "an rpm workflow offering an architecture Harbour lacks" \
    "sed -i 's|options: \\[aarch64\\]|options: [aarch64, x86_64]|' .github/workflows/rpm.yml"
break_and_expect 1.2.1 "a file installed outside the allowed paths" \
    "sed -i 's|^%{_bindir}/%{name}\$|%{_bindir}/%{name}\\n/etc/harbour-tuuli.conf|' $S"
break_and_expect 1.8.1 "a Vendor: tag" \
    "sed -i 's|^Group:.*|Vendor:     acme\\nGroup:      Qt/Qt|' $S"
break_and_expect 1.8.2 "an explicit Provides:" \
    "sed -i 's|^Group:.*|Provides:   tuuli\\nGroup:      Qt/Qt|' $S"
break_and_expect 1.8.2 "an explicit Conflicts:" \
    "sed -i 's|^Group:.*|Conflicts:  tuuli-browser\\nGroup:      Qt/Qt|' $S"
break_and_expect 1.8.3 "a dependency that is not on the allowed list" \
    "sed -i 's|^Requires:   sailfishsilica-qt5\$|Requires:   sailfishsilica-qt5\\nRequires:   sailfish-share|' $S"
break_and_expect 1.8.4 "a versioned Requires" \
    "sed -i 's|^Requires:   sailfishsilica-qt5\$|Requires:   sailfishsilica-qt5 >= 0.10.9|' $S"
break_and_expect 1.8.5 "an RPM scriptlet" \
    "printf '\\n%%post\\n/bin/true\\n' >> $S"
break_and_expect 1.8.7 "requiring the sailfish-qml launcher without using it" \
    "sed -i 's|^Requires:   sailfishsilica-qt5\$|Requires:   sailfishsilica-qt5\\nRequires:   libsailfishapp-launcher|' $S"

break_and_expect 1.3.2 "an Exec= that is not the package name" \
    "sed -i 's|^Exec=.*|Exec=/usr/bin/harbour-tuuli|' $D"
break_and_expect 1.3.3 "an Icon= with a path in it" \
    "sed -i 's|^Icon=.*|Icon=/usr/share/icons/x.png|' $D"
break_and_expect 1.3.4 "a missing Type=Application" \
    "sed -i 's|^Type=Application\$|Type=Service|' $D"
break_and_expect 1.3.5 "an X-Nemo-Application-Type that is not silica-qt5" \
    "sed -i 's|^X-Nemo-Application-Type=.*|X-Nemo-Application-Type=generic|' $D"
break_and_expect 1.3.6 "a [Sailjail] section header" \
    "sed -i 's|^\\[X-Sailjail\\]\$|[Sailjail]|' $D"
break_and_expect 1.4.1 "an OrganizationName with illegal characters" \
    "sed -i 's|^OrganizationName=.*|OrganizationName=Tuuli!|' $D"
break_and_expect 1.4.2 "an OrganizationName component starting with a digit" \
    "sed -i 's|^OrganizationName=.*|OrganizationName=9tuuli|' $D"
break_and_expect 1.4.3 "a reserved OrganizationName" \
    "sed -i 's|^OrganizationName=.*|OrganizationName=com.jolla|' $D"
break_and_expect 1.4.4 "an ApplicationName with illegal characters" \
    "sed -i 's|^ApplicationName=.*|ApplicationName=.tuuli|' $D"
break_and_expect 1.4.5 "a permission that is not on the whitelist" \
    "sed -i 's|^Permissions=.*|Permissions=Internet;Telepathy|' $D"
break_and_expect 1.4.5 "the Compatibility permission" \
    "sed -i 's|^Permissions=.*|Permissions=Internet;Compatibility|' $D"
break_and_expect 1.4.7 "a key that is not allowed in [X-Sailjail]" \
    "printf 'DBusName=org.example\\n' >> $D"
break_and_expect 2.5 "a sandbox grant the app's data path does not match" \
    "sed -i 's|^ApplicationName=.*|ApplicationName=browser|' $D"

break_and_expect 1.6.4 "a QML import at a version Harbour does not allow" \
    "sed -i 's|^import QtQuick 2.2\$|import QtQuick 2.7|' $Q"
break_and_expect 1.6.4 "an import that was dropped from the platform" \
    "sed -i '1i import QtWebKit 3.0' $Q"
break_and_expect 1.6.4 "a private QML module under a blocked prefix" \
    "sed -i 's|^import Tuuli 1.0\$|import Nemo.Tuuli 1.0|' $Q"
break_and_expect 1.6.5 "an absolute-path QML import" \
    "sed -i '1i import \"/usr/share/harbour-tuuli/qml/pages\"' $Q"
break_and_expect 1.6.6 "a relative import pointing outside the installed tree" \
    "sed -i '1i import \"../../icons\"' $Q"
break_and_expect 1.8.6 "XmlListModel used without requiring its package" \
    "sed -i '1i import QtQuick.XmlListModel 2.0' $Q"

break_and_expect 1.5.1 "a missing icon size" \
    "rm icons/128x128/harbour-tuuli.png"
break_and_expect 1.5.4 "an icon whose pixels do not match its directory" \
    "cp icons/86x86/harbour-tuuli.png icons/172x172/harbour-tuuli.png"
break_and_expect 1.5.3 "an icon that is not a PNG" \
    "printf 'not a png' > icons/86x86/harbour-tuuli.png"

break_and_expect 2.1 "a hardcoded /home/nemo path" \
    "printf 'pub const LEGACY: &str = \"/home/nemo/.tuuli\";\\n' >> $P"
break_and_expect 2.6 "a write into the installed data directory" \
    "printf 'pub fn scribble() { let _ = std::fs::create_dir_all(\"/usr/share/harbour-tuuli/x\"); }\\n' >> $P"
break_and_expect 1.2.3 "a cargo binary named something other than the package" \
    "sed -i 's|^name = \"harbour-tuuli\"\$|name = \"tuuli-browser\"|' crates/tuuli-browser/Cargo.toml"
break_and_expect 1.2.3 "a Servo-linked binary named something other than the package" \
    "sed -i 's|^name = \"harbour-tuuli\"\$|name = \"tuuli-browser\"|' servo/app/Cargo.toml"

# ci/harbour-validate-rpm.sh judges the real validator's output against the
# same waiver file, and its matching is the subtle part: a layout error
# names the offending path as its subject, while a linking error names the
# *binary* and puts the library in the message.  Fed saved logs rather than
# a built RPM, which needs the SDK.
validate_rpm() {
    local expect=$1 what=$2 body=$3
    local log="$work/validation.log"
    cases=$((cases + 1))
    printf '%s' "$body" > "$log"

    if "$pristine/ci/harbour-validate-rpm.sh" --log "$log" >/dev/null 2>&1; then
        local got=pass
    else
        local got=fail
    fi
    if [ "$got" = "$expect" ]; then
        echo "selftest: ok   $what -> $expect"
    else
        echo "selftest: FAIL $what should $expect, got $got" >&2
        "$pristine/ci/harbour-validate-rpm.sh" --log "$log" >&2
        status=1
    fi
}

validate_rpm pass "an RPM the validator accepts outright" \
'!BEGIN!x
!END!PASS!x
'
validate_rpm pass "an RPM with warnings only" \
'!BEGIN!x
WARNING|/usr/bin/harbour-tuuli|file is not stripped!
!END!FAIL!x
'
# The vendored qmetaobject patch is what keeps this out of the binary.  If
# it is ever lost, the RPM check has to say so rather than wave it through.
validate_rpm fail "a binary that links QtWidgets" \
'!BEGIN!x
ERROR|/usr/bin/harbour-tuuli|Cannot link to shared library: libQt5Widgets.so.5
!END!FAIL!x
'
validate_rpm fail "a binary linking a library Harbour does not allow" \
'!BEGIN!x
ERROR|/usr/bin/harbour-tuuli|Cannot link to shared library: libgstreamer-1.0.so.0
!END!FAIL!x
'
validate_rpm fail "an RPM installing somewhere new" \
'!BEGIN!x
ERROR|/etc/harbour-tuuli.conf|Installation not allowed in this location
!END!FAIL!x
'
validate_rpm fail "an RPM whose binary stopped exporting main()" \
'!BEGIN!x
ERROR|/usr/bin/harbour-tuuli|Binary must export main() symbol for booster to work (Q_DECL_EXPORT)
!END!FAIL!x
'
validate_rpm fail "a dependency the allow-list does not carry" \
'!BEGIN!x
ERROR|libcurl.so.4|Dependency not allowed
!END!FAIL!x
'
validate_rpm fail "a validation log that was cut off before the verdict" \
'ERROR|/usr/bin/harbour-tuuli|something
'

# A waiver excuses exactly the finding it names, on both checks.
cases=$((cases + 1))
dir="$work/case"
rm -rf "$dir"
cp -a "$pristine" "$dir"
printf '1.2.1  /etc/harbour-tuuli.conf  Installation*  # selftest\n' >> "$dir/ci/harbour/waivers.conf"
sed -i 's|^%{_bindir}/%{name}$|%{_bindir}/%{name}\n/etc/harbour-tuuli.conf|' "$dir/$S"
if "$dir/ci/harbour-check.sh" 2>&1 | grep -q 'WAIVED \[1.2.1\] /etc/harbour-tuuli.conf'; then
    echo "selftest: ok   a waived finding -> WAIVED"
else
    echo "selftest: FAIL a finding named in waivers.conf should be reported as WAIVED" >&2
    status=1
fi
printf '!BEGIN!x\nERROR|/etc/harbour-tuuli.conf|Installation not allowed in this location\n!END!FAIL!x\n' > "$work/validation.log"
if "$dir/ci/harbour-validate-rpm.sh" --log "$work/validation.log" >/dev/null 2>&1; then
    echo "selftest: ok   a waived validator error -> pass"
else
    echo "selftest: FAIL a validator error named in waivers.conf should pass" >&2
    status=1
fi

# The waiver file has to stay honest in both directions: an entry that
# stops matching is as much a defect as a missing check.
cases=$((cases + 1))
rm -rf "$dir"
cp -a "$pristine" "$dir"
printf '9.9.9  /nowhere  # excuses nothing\n' >> "$dir/ci/harbour/waivers.conf"
if "$dir/ci/harbour-check.sh" 2>&1 | grep -q 'stale waiver'; then
    echo "selftest: ok   a waiver that matches nothing -> stale waiver"
else
    echo "selftest: FAIL a waiver that matches nothing should be reported as stale" >&2
    status=1
fi

echo
if [ "$status" -eq 0 ]; then
    echo "selftest: ok ($cases cases)"
else
    echo "selftest: FAILED" >&2
fi
exit "$status"
