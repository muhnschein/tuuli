# Packaging

Tuuli is packaged for **Jolla's Harbour store**; [HARBOUR.md](HARBOUR.md)
is the map of what that constrains and what is still open.  Chum and
OpenRepos are not targets.

## One spec, two engines

`rpm/harbour-tuuli.spec` builds one package, `harbour-tuuli`:

| Mode | Engine | Binary built | By |
|---|---|---|---|
| default | mock | `cargo build -p tuuli-browser --features sailfish` inside the SDK target, with the SDK's Rust 1.75 | `rpm.yml`, engine `mock` |
| `--with servo` | Servo | `servo/app`, cross-compiled by `servo/build.sh` with Servo's toolchain against the SDK target root, installed from `rpm/harbour-tuuli-servo-<version>-<arch>.tar.xz` | `rpm.yml`, engine `servo` |

Spec 12.2 originally asked for `tuuli-browser`, `libtuuli-qml`, `libservo`
and a debuginfo package.  With the engine consumed as a Rust crate there is
no C ABI to split a shared `libservo` on and no Qt Quick plugin, and
Harbour permits neither a second package with `Conflicts:` nor a shared
library outside `/usr/share/<NAME>/lib/`.  So the engine is statically
linked into the one binary, the two builds share a package name, and the
independence the spec wanted (engine rebases and UI iteration shipping
separately) comes from the QML chrome being data files under
`/usr/share/harbour-tuuli/qml` — editable in place on a device — and from
the mock build being a minutes-long SDK job while the Servo build is an
hours-long cross-compile.

The mock package is a development build.  It is never submitted.

## What the package contains

    /usr/bin/harbour-tuuli
    /usr/share/harbour-tuuli/qml/**          the Silica chrome
    /usr/share/harbour-tuuli/filters/README.md
    /usr/share/harbour-tuuli/LICENSE
    /usr/share/applications/harbour-tuuli.desktop
    /usr/share/icons/hicolor/{86x86,108x108,128x128,172x172}/apps/harbour-tuuli.png

Nothing else: those are the four locations Harbour allows.  `Requires:` is
`sailfishsilica-qt5` alone — unversioned, because the validator rejects
version operators, and alone because Transfer Engine and `Sailfish.Share`
are reached through D-Bus and a QML import that every image carries and
neither has an allowed package to name.

## Building

`.github/workflows/rpm.yml` is the supported path: dispatch it from the
Actions tab (engine, arch, SDK version) or push a `v*` or `build-*` tag.
It runs `mb2` inside the pinned `coderus/sailfishos-platform-sdk` image as
that image's own build user, stamps `Release: 1.<run number>` so every
build installs over the last, uploads the RPM, and then runs Jolla's
validator on it.  [BUILDING.md](BUILDING.md) has the same steps for a
local SDK.

## Desktop file

`src/app/harbour-tuuli.desktop` declares no `MimeType` and no
`x-scheme-handler`: Tuuli does not register as a URL handler before M4
(spec N1).  `Exec` takes no `%U` for the same reason; `harbour-tuuli
<url>` from a shell still works.  The `[X-Sailjail]` section is the
sandbox profile; `Location`, `Camera` and `Microphone` are added only with
the milestone that uses them (spec 9.1).  `OrganizationName` and
`ApplicationName` are both the package name, which is what libsailfishapp
sets on the application object and what `crates/tuuli-core/src/paths.rs`
builds the data path from: `~/.local/share/harbour-tuuli/harbour-tuuli/`.
