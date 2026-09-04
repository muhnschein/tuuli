# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# tuuli-browser: app, QML, Silica UI, .desktop, sailjail profile
# libtuuli-qml:  Qt Quick plugin and C++ shim
#
# The engine (libservo) is a separate source package, rpm/libservo.spec,
# so engine rebases and UI iteration ship independently (spec 12.2).
#
#   --with mock_engine   build against the in-process mock engine instead
#                        of libservo (UI iteration, CI on the SDK target)

%bcond_with mock_engine

Name:       tuuli-browser
Version:    0.1.0
Release:    1
Summary:    Servo-based web browser for Sailfish OS
License:    MPL-2.0
URL:        https://github.com/muhnschein/tuuli
Source0:    %{name}-%{version}.tar.bz2

BuildRequires:  cmake >= 3.10
BuildRequires:  gcc-c++
BuildRequires:  pkgconfig(Qt5Core) >= 5.6
BuildRequires:  pkgconfig(Qt5Gui)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5DBus)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.3
BuildRequires:  desktop-file-utils
%if %{without mock_engine}
BuildRequires:  libservo-devel >= 0.5.0
%endif

Requires:   sailfishsilica-qt5 >= 0.10.9
Requires:   libtuuli-qml = %{version}-%{release}
Requires:   qt5-qtdeclarative-import-folderlistmodel
Requires:   declarative-transferengine-qt5
Requires:   sailfish-share
Requires:   sailjail

%description
Tuuli is a native Sailfish OS browser: Silica QML chrome driving the Servo
rendering engine through a C ABI and a Qt Quick scene-graph integration
layer.  It ships alongside Sailfish Browser as a second, experimental
browser.

Web content is NOT sandboxed from the application (single-process engine;
see the About page).  Users who need a hardened browser should use
Sailfish Browser.

%package -n libtuuli-qml
Summary:    Qt Quick plugin and engine shim for Tuuli
%if %{without mock_engine}
Requires:   libservo >= 0.5.0
%endif

%description -n libtuuli-qml
The `import Tuuli 1.0` Qt Quick plugin and the C++ shim (libtuuli) that
drive the Servo engine through servo_capi.

%prep
%setup -q -n %{name}-%{version}

%build
%if %{with mock_engine}
%define tuuli_engine mock
%else
%define tuuli_engine servo
%endif
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=%{_prefix} \
    -DCMAKE_INSTALL_LIBDIR=%{_libdir} \
    -DTUULI_ENGINE=%{tuuli_engine} \
    -DTUULI_BUILD_TESTS=OFF \
    -DTUULI_BUILD_APP=ON
%make_build

%install
cd build
%make_install
desktop-file-install --delete-original \
    --dir %{buildroot}%{_datadir}/applications \
    %{buildroot}%{_datadir}/applications/*.desktop

%post -n libtuuli-qml -p /sbin/ldconfig
%postun -n libtuuli-qml -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%files -n libtuuli-qml
%defattr(-,root,root,-)
%{_libdir}/libtuuli.so.*
%{_libdir}/qt5/qml/Tuuli
