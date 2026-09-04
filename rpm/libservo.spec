# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# libservo: the Servo engine as a shared object with the servo_capi C ABI,
# versioned to the pinned Servo release (servo/SERVO_TAG).
#
# Building Servo (with SpiderMonkey) inside rpmbuild on the SDK target is
# slow and needs a Clang toolchain the target lacks (spec 12.1).  Two modes:
#
#   default            : install a prebuilt engine tarball produced by
#                        servo/build-libservo.sh (Source1)
#   --with from_source : build with cargo inside the target, using the
#                        vendored crate tarball (Source2) and cargo config
#                        (Source3).  For Chum/OBS reproducibility.
#
# Either way the resulting ABI must match servo/capi/servo_capi.h; the
# header shipped in -devel is the one cbindgen generated from the tag.

%bcond_with from_source

%define servo_tag 0.5.0

Name:       libservo
Version:    %{servo_tag}
Release:    1
Summary:    Servo web engine (libservo) with C ABI
License:    MPL-2.0
URL:        https://servo.org
Source0:    servo-%{servo_tag}.tar.xz
Source1:    libservo-%{servo_tag}-aarch64.tar.xz
Source2:    servo-%{servo_tag}-vendor.tar.xz
Source3:    cargo-config.toml

%if %{with from_source}
BuildRequires:  rust >= 1.85
BuildRequires:  cargo
BuildRequires:  clang
BuildRequires:  llvm
BuildRequires:  python3-base
BuildRequires:  pkgconfig(gstreamer-1.0)
BuildRequires:  pkgconfig(gstreamer-base-1.0)
BuildRequires:  pkgconfig(gstreamer-plugins-base-1.0)
BuildRequires:  pkgconfig(gstreamer-gl-1.0)
BuildRequires:  pkgconfig(fontconfig)
BuildRequires:  pkgconfig(freetype2)
BuildRequires:  pkgconfig(harfbuzz)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(glesv2)
BuildRequires:  pkgconfig(dbus-1)
BuildRequires:  pkgconfig(openssl)
BuildRequires:  pkgconfig(libudev)
BuildRequires:  pkgconfig(zlib)
%endif

Requires:   gstreamer1.0
Requires:   gstreamer1.0-droid
Requires:   gstreamer1.0-plugins-base
Requires:   gstreamer1.0-plugins-good
Requires:   fontconfig
Requires:   ca-certificates

%description
The Servo web engine built as a shared library, exposing the servo_capi C
ABI.  Consumed by Tuuli (libtuuli-qml).  Pinned to Servo release
%{servo_tag}; rebased monthly with Servo's release cadence.

%package devel
Summary:    Development files for libservo
Requires:   %{name} = %{version}-%{release}

%description devel
servo_capi.h and pkg-config metadata for building against libservo.

%prep
%if %{with from_source}
%setup -q -n servo-%{servo_tag}
tar xf %{SOURCE2}
mkdir -p .cargo
cp %{SOURCE3} .cargo/config.toml
%else
%setup -q -c -T
tar xf %{SOURCE1}
%endif

%build
%if %{with from_source}
export CARGO_HOME=$PWD/.cargo-home
export CC=clang
export CXX=clang++
export RUSTFLAGS="-C link-arg=-Wl,--as-needed"
# GStreamer media, no WebGPU, mobile viewport (spec 5.4).
cargo build --release --offline --frozen \
    -p servo_capi \
    --features "media-gstreamer,mobile" \
    --no-default-features
%endif

%install
mkdir -p %{buildroot}%{_libdir} %{buildroot}%{_includedir} %{buildroot}%{_libdir}/pkgconfig
%if %{with from_source}
install -m 0755 target/release/libservo_capi.so %{buildroot}%{_libdir}/libservo.so.0.5.0
install -m 0644 target/release/servo_capi.h %{buildroot}%{_includedir}/servo_capi.h
%else
install -m 0755 lib64/libservo.so.0.5.0 %{buildroot}%{_libdir}/libservo.so.0.5.0
install -m 0644 include/servo_capi.h %{buildroot}%{_includedir}/servo_capi.h
%endif
ln -s libservo.so.0.5.0 %{buildroot}%{_libdir}/libservo.so.0
ln -s libservo.so.0 %{buildroot}%{_libdir}/libservo.so
sed -e "s|@PREFIX@|%{_prefix}|" -e "s|@VERSION@|%{version}|" \
    %{_builddir}/%{?with_from_source:servo-%{servo_tag}}%{!?with_from_source:%{name}-%{version}}/servo_capi.pc.in \
    > %{buildroot}%{_libdir}/pkgconfig/servo_capi.pc 2>/dev/null || \
cat > %{buildroot}%{_libdir}/pkgconfig/servo_capi.pc <<PC
prefix=%{_prefix}
libdir=%{_libdir}
includedir=%{_includedir}

Name: servo_capi
Description: C ABI for the Servo web engine (libservo)
Version: %{version}
Libs: -L\${libdir} -lservo
Cflags: -I\${includedir}
PC

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%files
%defattr(-,root,root,-)
%{_libdir}/libservo.so.0
%{_libdir}/libservo.so.0.5.0

%files devel
%defattr(-,root,root,-)
%{_includedir}/servo_capi.h
%{_libdir}/libservo.so
%{_libdir}/pkgconfig/servo_capi.pc
