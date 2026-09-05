# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# tuuli-browser: the Rust application (mock engine) with the Silica QML,
#                the .desktop file and the sailjail profile.
#
# The Servo-linked binary of the same name is built by rpm/tuuli-browser-servo.spec
# from servo/app and conflicts with this package: install one or the other.
# Engine rebases and UI iteration therefore ship independently (spec 12.2).
#
# Rust on the SDK target: the crates are vendored (Source1, produced by
# tools/vendor.sh) so the build is offline, as Chum/OBS require.

Name:       tuuli-browser
Version:    0.1.0
Release:    1
Summary:    Servo-based web browser for Sailfish OS (mock engine build)
License:    MPL-2.0
URL:        https://github.com/muhnschein/tuuli
Source0:    %{name}-%{version}.tar.bz2
Source1:    %{name}-%{version}-vendor.tar.xz

BuildRequires:  rust >= 1.80
BuildRequires:  cargo
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(Qt5Core) >= 5.6
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(Qt5Widgets)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.3
BuildRequires:  qt5-qmake
BuildRequires:  desktop-file-utils

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   declarative-transferengine-qt5
Requires:   sailfish-share
Requires:   sailjail
Conflicts:  tuuli-browser-servo

%description
Tuuli is a native Sailfish OS browser: Silica QML chrome over a Rust core
that drives the Servo engine.  This package carries the in-process mock
engine for UI iteration; tuuli-browser-servo carries the real engine.

Web content is NOT sandboxed from the application (single-process engine;
see the About page).  Users who need a hardened browser should use
Sailfish Browser.

%prep
%setup -q -n %{name}-%{version}
tar xf %{SOURCE1}
mkdir -p .cargo
cat > .cargo/config.toml <<CFG
[source.crates-io]
replace-with = "vendored-sources"
[source.vendored-sources]
directory = "vendor"
[net]
offline = true
CFG

%build
export CARGO_HOME=$PWD/.cargo-home
export QMAKE=%{_libdir}/qt5/bin/qmake
cargo build --release --offline --frozen -p tuuli-browser --features sailfish

%install
install -D -m 0755 target/release/tuuli-browser %{buildroot}%{_bindir}/tuuli-browser
mkdir -p %{buildroot}%{_datadir}/%{name}
cp -r src/qml %{buildroot}%{_datadir}/%{name}/qml
mkdir -p %{buildroot}%{_datadir}/%{name}/filters
install -m 0644 tools/filters/README.md %{buildroot}%{_datadir}/%{name}/filters/README.md
desktop-file-install --dir %{buildroot}%{_datadir}/applications src/app/%{name}.desktop
for size in 86 108 128 172; do
    install -D -m 0644 icons/${size}x${size}/%{name}.png \
        %{buildroot}%{_datadir}/icons/hicolor/${size}x${size}/apps/%{name}.png
done

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
