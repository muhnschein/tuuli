# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# tuuli-browser-servo: Tuuli linked with libservo (servo/app).
#
# Servo (with SpiderMonkey) does not build with the SDK target's GCC
# (spec 12.1): the binary is cross-compiled on the SDK host by
# servo/build.sh and packaged from that tarball (Source1).  The
# from-source path is kept for Chum reproducibility once the target has a
# Clang toolchain that can build mozjs (docs/PACKAGING.md).

%bcond_with from_source

%define servo_tag 0.5.0

Name:       tuuli-browser-servo
Version:    0.1.0
Release:    1
Summary:    Servo-based web browser for Sailfish OS
License:    MPL-2.0
URL:        https://github.com/muhnschein/tuuli
Source0:    tuuli-browser-%{version}.tar.bz2
Source1:    tuuli-browser-servo-%{version}-aarch64.tar.xz
Source2:    tuuli-browser-servo-%{version}-vendor.tar.xz

%if %{with from_source}
BuildRequires:  rust >= 1.80
BuildRequires:  cargo
BuildRequires:  clang
BuildRequires:  llvm
BuildRequires:  python3-base
BuildRequires:  pkgconfig(Qt5Core) >= 5.6
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Widgets)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.3
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(harfbuzz)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(openssl)
%endif
BuildRequires:  desktop-file-utils

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   declarative-transferengine-qt5
Requires:   sailfish-share
Requires:   sailjail
Requires:   gstreamer1.0
Requires:   gstreamer1.0-droid
Requires:   gstreamer1.0-plugins-base
Requires:   gstreamer1.0-plugins-good
Requires:   fontconfig
Requires:   ca-certificates
Provides:   tuuli-browser = %{version}-%{release}
Conflicts:  tuuli-browser

%description
Tuuli with the Servo engine (release %{servo_tag}), pinned and rebased
monthly with Servo's release cadence.  Second browser; not a replacement
for Sailfish Browser.  Web content is NOT sandboxed from the application:
see the About page.

%prep
%setup -q -n tuuli-browser-%{version}
%if %{with from_source}
tar xf %{SOURCE2}
# vendor-config.toml is what `cargo vendor` printed in servo/build.sh: it
# redirects crates.io and the Servo git source to the vendored tree.
mkdir -p servo/app/.cargo
sed 's|directory = "vendor"|directory = "../../vendor"|' vendor-config.toml > servo/app/.cargo/config.toml
printf '\n[net]\noffline = true\n' >> servo/app/.cargo/config.toml
%else
tar xf %{SOURCE1}
%endif

%build
%if %{with from_source}
export CARGO_HOME=$PWD/.cargo-home
export QMAKE=%{_libdir}/qt5/bin/qmake
export CC=clang
export CXX=clang++
cargo build --release --offline --frozen --manifest-path servo/app/Cargo.toml --features sailfish
%endif

%install
%if %{with from_source}
install -D -m 0755 servo/app/target/release/tuuli-browser %{buildroot}%{_bindir}/tuuli-browser
%else
install -D -m 0755 bin/tuuli-browser %{buildroot}%{_bindir}/tuuli-browser
%endif
mkdir -p %{buildroot}%{_datadir}/tuuli-browser
cp -r src/qml %{buildroot}%{_datadir}/tuuli-browser/qml
mkdir -p %{buildroot}%{_datadir}/tuuli-browser/filters
install -m 0644 tools/filters/README.md %{buildroot}%{_datadir}/tuuli-browser/filters/README.md
desktop-file-install --dir %{buildroot}%{_datadir}/applications src/app/tuuli-browser.desktop
for size in 86 108 128 172; do
    install -D -m 0644 icons/${size}x${size}/tuuli-browser.png \
        %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/tuuli-browser.png
done

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/tuuli-browser
%{_datadir}/tuuli-browser
%{_datadir}/applications/tuuli-browser.desktop
%{_datadir}/icons/hicolor/*/apps/tuuli-browser.png
