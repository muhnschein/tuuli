# SPDX-License-Identifier: MPL-2.0
#
# Harbour package for tuuli. Every Requires must appear in ci/harbour/allowed_requires.conf
# (or be a library from allowed_libraries.conf); see docs/HARBOUR.md. The OS floor
# (Sailfish OS >= 5.2.0) cannot be expressed as `Requires: sailfish-version` because
# Harbour rejects that dependency, so it is carried by the SDK target used to build
# (the __libc_start_main version check) and by the package versions below.
#
# Version is stamped from the release tag by CI (docs/RELEASING.md); keep 0.0.0 here.
Name:       harbour-tuuli
Summary:    Web browser
Version:    0.0.0
Release:    1
License:    MPL-2.0
URL:        https://github.com/muhnschein/tuuli
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  cmake
BuildRequires:  desktop-file-utils
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  qt5-qttools-linguist

Requires:   sailfishsilica-qt5 >= 1.1.123
Requires:   sailfish-components-webview-qt5 >= 1.7.0
Requires:   sailfish-components-webview-qt5-popups >= 1.7.0
Requires:   sailfish-components-webview-qt5-pickers >= 1.7.0
Requires:   qt5-plugin-imageformat-ico

%description
Web browser for Sailfish OS with a Silica interface over the platform web engine.

%prep
%setup -q -n %{name}-%{version}

%build
mkdir -p build
cd build
cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DTUULI_BUILD_TESTS=OFF \
    -DTUULI_REQUIRE_SAILFISHAPP=ON \
    -DTUULI_VERSION=%{version} \
    ..
make %{?_smp_mflags}

%install
cd build
make DESTDIR=%{buildroot} install
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/%{name}.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png
