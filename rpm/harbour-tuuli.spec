# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# harbour-tuuli: Tuuli, packaged for Jolla's Harbour store (docs/HARBOUR.md).
#
# Two build modes, one package:
#
#   default       the mock engine, built by cargo inside the SDK target with
#                 the Rust the SDK ships.  The whole chrome with placeholder
#                 content: what the rpm workflow builds by default, and what
#                 M0's chrome, sailjail and paths items are answered with.
#   --with servo  the Servo-linked binary.  The SDK's Rust cannot build Servo
#                 (docs/BUILDING.md), so servo/build.sh cross-compiles it on
#                 the SDK host against the target root and packs it as
#                 rpm/harbour-tuuli-servo-<version>-<arch>.tar.xz, which
#                 %%install unpacks.
#
# Harbour constrains the name, the install paths and every Requires:.
# ci/harbour-check.sh fails a change that breaks one, and the rpm workflow
# runs Jolla's own validator on what this produces.  No bare %% in a
# comment: the SDK's rpm expands macros inside comments.

%bcond_with servo

# Harbour requires the harbour- prefix and lowercase throughout; the name
# users see is the .desktop file's Name= and the Store's Title field.
Name:       harbour-tuuli
Summary:    Servo-based web browser
Version:    0.1.0
Release:    1
License:    MPL-2.0
Group:      Qt/Qt
URL:        https://github.com/muhnschein/tuuli
Source0:    %{name}-%{version}.tar.gz
%if %{with servo}
Source1:    %{name}-servo-%{version}-%{_target_cpu}.tar.xz
%endif

# Unversioned, and only what Harbour's allowed_requires.conf lists: the
# validator hands each whitespace-separated token to its allow-list, so a
# versioned dependency arrives as three of them and the operator and the
# version are both rejected.  Harbour derives its own compatibility range
# from these; the OS floor (5.2, the Jolla Phone) goes in the submission
# form's "From OS version" field.
#
# Sailfish.Share and Transfer Engine are reached through a QML import and
# D-Bus that every device image carries; neither has an allowed package to
# require.  No libsailfishapp-launcher: that is for sailfish-qml apps.
Requires:   sailfishsilica-qt5

%if %{without servo}
# Sailfish ships Rust 1.75.0 (sailfishos/rust); Cargo.lock is kept in the
# v3 format because cargo learned v4 in 1.78 (ci/check-lockfile.sh).
BuildRequires:  rust >= 1.75
BuildRequires:  rust-std-static >= 1.75
BuildRequires:  cargo >= 1.75
# qmetaobject-rs and the WebView item compile C++ glue against Qt.
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(sailfishapp)
%endif
# lrelease, which compiles the translation catalogs in %%build.
BuildRequires:  qt5-qttools-linguist
BuildRequires:  desktop-file-utils

%ifarch %arm
%define rusttarget armv7-unknown-linux-gnueabihf
%endif
%ifarch aarch64
%define rusttarget aarch64-unknown-linux-gnu
%endif
%ifarch %ix86
%define rusttarget i686-unknown-linux-gnu
%endif
%ifarch x86_64
%define rusttarget x86_64-unknown-linux-gnu
%endif

# Where cargo leaves the binary.  Under sb2, SB2_RUST_TARGET_TRIPLE makes it
# write to target/<triple>/release; a native build gets plain
# target/release.  Both are checked at install time.
%define builddir target/%{rusttarget}/release
%define nativedir target/release
%define appdatadir %{_datadir}/%{name}

%description
Tuuli is a native Sailfish OS browser: Silica QML chrome over a Rust core
that drives the Servo engine.  Second browser; not a replacement for
Sailfish Browser.  Web content is NOT sandboxed from the application
(single-process engine; see the About page).

%prep
%setup -q -n %{name}-%{version}

%build
%if %{without servo}
rustc --version
cargo --version

# Cross-compiling Rust under scratchbox2: the accelerated rustc would emit
# host code unless told the real target (the mechanism Whisperfish's spec
# uses).  No --target for cargo, which would make it look for a target std
# it cannot find.
export SB2_RUST_TARGET_TRIPLE=%{rusttarget}

# qttypes' build script runs `qmake -query` by default, which a build
# script under sb2 cannot exec.  Both variables together make it skip qmake
# and read the Qt version from the headers; these are the target-rootfs
# paths as seen from inside sb2.
export QT_INCLUDE_PATH=%{_includedir}/qt5
export QT_LIBRARY_PATH=%{_libdir}
# The target's Qt is a GLES build: the FBO renderer links libGLESv2.
export TUULI_LINK_GLESV2=1

# Build scripts and proc-macros are compiled for the tooling's own
# architecture, and rustc links them by calling plain `cc`, which sb2
# rewrites to the cross compiler.  scratchbox2 exposes the native compiler
# as host-gcc for exactly this.  Outside sb2 nothing is overridden.
if [ -n "${SBOX_SESSION_DIR:-}" ]; then
    host_triple=$(rustc -vV | sed -n 's/^host: //p')
    export "CARGO_TARGET_$(echo "$host_triple" | tr 'a-z-' 'A-Z_')_LINKER"=host-gcc
fi

# -j1 under sb2: parallel cargo deadlocks there while C++ glue compiles
# (observed in Postivene, whose spec this follows).
cargo build \
    ${SBOX_SESSION_DIR:+-j1} \
    --release \
    --locked \
    -p tuuli-browser \
    --features sailfish
%endif

# The catalogs: translations/harbour-tuuli.ts (engineering English from the
# //%% comments) and any harbour-tuuli-<lang>.ts, compiled here.
./scripts/release-translations.sh

%install
rm -rf %{buildroot}

%if %{with servo}
# The cross-compiled binary; servo/build.sh strips it and keeps the debug
# info beside it in the tarball.
mkdir -p prebuilt
tar -C prebuilt -xf %{SOURCE1}
install -Dm 755 prebuilt/bin/%{name} %{buildroot}%{_bindir}/%{name}
%else
builddir=%{builddir}
[ -x "$builddir/%{name}" ] || builddir=%{nativedir}
install -Dm 755 "$builddir/%{name}" %{buildroot}%{_bindir}/%{name}
%endif

# The Silica chrome, under the package's own data directory, where
# SailfishApp::pathTo() looks.  Only what the engine reads: a stray editor
# backup under src/qml would otherwise ship, and Harbour would have an
# opinion about it.
(cd src/qml && find . -type f \( -name '*.qml' -o -name '*.js' -o -name qmldir \) -exec \
    install -Dm 644 "{}" "%{buildroot}%{appdatadir}/qml/{}" \; )

# The compiled catalogs, where the application object loads them from.
(cd translations && find . -type f -name 'harbour-tuuli*.qm' -exec \
    install -Dm 644 "{}" "%{buildroot}%{appdatadir}/translations/{}" \; )

# Where the user drops cosmetic filter lists (spec 9.3), documented in place.
install -Dm 644 tools/filters/README.md %{buildroot}%{appdatadir}/filters/README.md
install -Dm 644 LICENSE %{buildroot}%{appdatadir}/LICENSE

desktop-file-install \
    --dir %{buildroot}%{_datadir}/applications \
    src/app/%{name}.desktop

install -Dm 644 icons/86x86/%{name}.png \
    %{buildroot}%{_datadir}/icons/hicolor/86x86/apps/%{name}.png
install -Dm 644 icons/108x108/%{name}.png \
    %{buildroot}%{_datadir}/icons/hicolor/108x108/apps/%{name}.png
install -Dm 644 icons/128x128/%{name}.png \
    %{buildroot}%{_datadir}/icons/hicolor/128x128/apps/%{name}.png
install -Dm 644 icons/172x172/%{name}.png \
    %{buildroot}%{_datadir}/icons/hicolor/172x172/apps/%{name}.png

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{appdatadir}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
