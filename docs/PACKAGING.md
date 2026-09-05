# Packaging

Spec 12.2 originally asked for `tuuli-browser`, `libtuuli-qml`, `libservo`
and `tuuli-browser-debuginfo`.  With the engine consumed as a Rust crate
there is no C ABI to split a shared `libservo` on and no Qt Quick plugin:
Rust has no stable dylib ABI, and a `cdylib` would reintroduce the C ABI
the design dropped.  The engine is therefore statically linked into the
`tuuli-browser` binary, and the spec's independence requirement (engine
rebases and UI iteration shipping separately) is met by two source
packages instead:

| Spec file | Binary RPMs | Engine | Built |
|---|---|---|---|
| `rpm/tuuli-browser.spec` | `tuuli-browser`, `tuuli-browser-debuginfo` | mock | on the SDK target with cargo, from vendored crates |
| `rpm/tuuli-browser-servo.spec` | `tuuli-browser-servo` (`Provides`/`Conflicts: tuuli-browser`), debuginfo | Servo (`%servo_tag`) | from the tarball `servo/build.sh` produces, or `--with from_source` |

The QML chrome is installed as files by both packages
(`/usr/share/tuuli-browser/qml`), so UI iteration on a device never needs
the engine rebuilt: edit the files in place or reinstall the mock package.
An engine rebase rebuilds `tuuli-browser-servo` only.

## tuuli-browser (mock engine)

`Source0` is the git archive and `Source1` the `cargo vendor` tarball;
`tools/vendor.sh` makes both.  The spec unpacks the vendor tree, writes a
`.cargo/config.toml` that redirects crates.io to it and builds
`cargo build --release --offline --frozen -p tuuli-browser --features
sailfish`.  `QMAKE` points qttypes at the target's Qt.  CI runs exactly
this on the SDK target, which is the only place the Qt 5.6 constraint
(spec 3.2) is enforced; it is also a legitimate install for UI iteration
on a device.

## tuuli-browser-servo

- default: installs `bin/tuuli-browser` from
  `tuuli-browser-servo-<version>-aarch64.tar.xz` (`Source1`), the output
  of `servo/build.sh`.  This is how development builds are made.
- `--with from_source`: builds `servo/app` with cargo inside the target
  from the git archive (`Source0`) and the vendored crates of the Servo
  dependency tree (`Source2`, also from `servo/build.sh`).  This is the
  mode Chum/OBS needs for reproducibility; it needs `rust`, `cargo`,
  `clang` and `llvm` in the target and a very patient build host.
  Whether the target's toolchain can build SpiderMonkey at all is an M0
  question (spec 12.1).

The package pulls in `gstreamer1.0-droid`, `fontconfig` and
`ca-certificates` because the engine uses the system decoders, fonts and
CA bundle (spec 8).

## Distribution

Chum primary, OpenRepos secondary.  Not Harbour (spec 12.3): the engine
bundles non-allowed libraries and the packages are far outside Harbour's
rules.  The store descriptions must carry the threat-model disclosure from
[THREAT_MODEL.md](THREAT_MODEL.md).

## Desktop file

`src/app/tuuli-browser.desktop` declares no `MimeType` and no
`x-scheme-handler`: Tuuli does not register as a URL handler before M4
(spec N1).  `Exec` takes no `%U` for the same reason; `tuuli-browser
<url>` from a shell still works.  The `[X-Sailjail]` section is the
sandbox profile; `Location`, `Camera` and `Microphone` are added only with
the milestone that uses them (spec 9.1).
