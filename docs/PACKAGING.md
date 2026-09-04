# Packaging

Spec 12.2 asks for `tuuli-browser`, `libtuuli-qml`, `libservo` and
`tuuli-browser-debuginfo`, with the engine split so engine rebases and UI
iteration ship independently.

Two source packages deliver that:

| Spec file | Binary RPMs | Version follows |
|---|---|---|
| `rpm/tuuli-browser.spec` | `tuuli-browser`, `libtuuli-qml`, `tuuli-browser-debuginfo` | Tuuli releases |
| `rpm/libservo.spec` | `libservo`, `libservo-devel`, `libservo-debuginfo` | the pinned Servo tag |

A single spec with an engine subpackage would rebuild Servo on every UI
change and tie the engine's version to Tuuli's; two source packages are
what "independently shippable" means in RPM terms.

`libtuuli-qml` contains both `libtuuli.so.0` (the shim and models) and the
`Tuuli` QML plugin; splitting them further buys nothing.

## libservo modes

- default: installs the prebuilt tarball from `servo/build-libservo.sh`
  (`Source1`).  This is how development builds are made.
- `--with from_source`: builds with cargo inside the target from the tag
  tarball and vendored crates (`Source0`, `Source2`, `Source3`).  This is
  the mode Chum/OBS needs for reproducibility; it needs `rust`, `cargo`,
  `clang` and `llvm` in the target and a very patient build host.  Whether
  the target's toolchain can build SpiderMonkey at all is an M0 question.

## tuuli-browser modes

- default: `BuildRequires: libservo-devel`.
- `--with mock_engine`: no engine dependency, mock engine compiled in.
  CI uses this on the SDK target to keep the Qt 5.6 build honest; it is
  also a legitimate install for UI iteration on device.

## Distribution

Chum primary, OpenRepos secondary.  Not Harbour (spec 12.3): the engine
bundles non-allowed libraries and the packages are far outside Harbour's
rules.  The store descriptions must carry the threat-model disclosure from
[THREAT_MODEL.md](THREAT_MODEL.md).

## Desktop file

`src/app/tuuli-browser.desktop` declares no `MimeType` and no
`x-scheme-handler`: Tuuli does not register as a URL handler before M4
(spec N1).  `Exec` takes no `%U` for the same reason; `tuuli-browser
<url>` from a shell still works.
