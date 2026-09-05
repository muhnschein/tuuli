# Harbour

Tuuli is packaged for [Jolla's Harbour store](https://harbour.jolla.com),
not for Chum or OpenRepos.  Harbour's rules are not advice: a validator
failure is a guaranteed rejection, and several of them constrain things
that are expensive to change once code is built around them — the package
name, the install paths, the linker flags, which libraries the binary may
need.  So they are a CI gate, and the ones this project cannot satisfy on
its own are listed at the end as the questions to put to Jolla.

The mechanism follows [Postivene](https://github.com/muhnschein/postivene),
whose `docs/HARBOUR.md` records what each rule cost to learn; the scripts
here are that project's, adapted to this tree.

## The two checks

| | `ci/harbour-check.sh` | `sfdk check -s harbour` |
|---|---|---|
| Runs | every push and pull request (`ci.yml`) | when an RPM is built (`rpm.yml`) |
| Reads | the source tree | the built package |
| Needs | rpm, file, binutils, a host build | the Sailfish SDK, minutes of runner time |
| Authority | no | **yes** |

The second is Jolla's own `rpmvalidation.sh`, fetched at the commit
`ci/harbour/UPSTREAM` names and run against the RPM the SDK produces by
`ci/harbour-validate-rpm.sh`.  It is the one that decides.  The first
exists because the second cannot run on every push, and because a rule
broken in a pull request is cheaper to fix than one discovered at intake.

The validator step runs *after* the artifact upload, and findings recorded
in `ci/harbour/waivers.conf` do not fail it: a package Harbour would reject
is still one worth putting on a phone.  Anything not waived fails it.

The two are kept honest against each other: `ci/harbour/` holds the
validator's own allow-lists, copied verbatim by
`scripts/update-harbour-rules.sh`, and the validator step refuses to run if
those copies differ from the validator's at the pinned commit.
`ci/harbour-check.sh` reimplements the logic around them.
`ci/harbour-check-selftest.sh` breaks each rule in a throwaway copy of the
tree and asserts the check names it; a gate that only ever prints "ok" is
indistinguishable from one that has stopped looking.

## What the source check covers

Check IDs follow Jolla's own numbering.

**Naming** (1.1) — the `harbour-` prefix and lowercase package name;
Version digits and periods only; Release digits, underscores and periods,
*including the Release `rpm.yml` stamps onto each build*; only Harbour
architectures offered by the workflow.

**Layout** (1.2) — every path in `%files`, for each device architecture,
against the four locations Harbour permits (`/usr/bin/<NAME>`,
`/usr/share/<NAME>/`, the .desktop file, the icons); the .desktop file and
the binary present; nothing under `/home`; no debug directories; no
world- or group-writable, setuid or setgid install modes.

**The .desktop file** (1.3) — a non-empty `Name=`; `Exec=` and `Icon=`
exactly the package name; `Type=Application`;
`X-Nemo-Application-Type=silica-qt5`; `[X-Sailjail]`, never `[Sailjail]`,
and never empty.

**Sailjail** (1.4) — only the four allowed keys; `OrganizationName` and
`ApplicationName` against their regexes and the reserved-name list; every
permission on the whitelist; `ExecDBus` agreeing with `Exec`.

**Icons** (1.5) — all four sizes present, real PNGs, pixel dimensions
matching their directory names.

**QML** (1.6) — every import against the allow-list and the blocked-prefix
patterns; no absolute-path imports; relative imports resolving inside the
installed tree.  `import Tuuli 1.0`, the app's own types registered from
the binary, is allowed because it is under none of the blocked prefixes.

**The binary** (1.6.1, 1.7) — every linked library against the allowed
list; that it links `__libc_start_main`; and that it **exports `main()` as
a dynamic symbol**.  The source check reads the host build, which is
enough for the Qt and C++ libraries; the engine's libraries only show in
the device build (below).

**RPM metadata** (1.8) — no `Vendor:`; no `Provides:`, `Obsoletes:`,
`Conflicts:`, `Recommends:`, `Suggests:`, `Supplements:` or `Enhances:`;
every `Requires:` unversioned and on the allowed list; no scriptlets or
triggers; `libsailfishapp-launcher` required if and only if the
`sailfish-qml` launcher is used; `qt5-qtdeclarative-import-xmllistmodel`
required if `QtQuick.XmlListModel` is imported.

**Runtime policy** (2.1, 2.5, 2.6) — no hardcoded `/home/nemo` or
`/home/defaultuser`; the data path the app builds
(`crates/tuuli-core/src/paths.rs`) spelled the same way as the sandbox
grant it depends on; nothing written to a path the package installs.

## What it cannot cover

Anything that needs the built package or a device.  `rpm.yml`'s validator
step covers the first group; the rest is "Before submitting" below.

- The `Requires:` and `Provides:` **rpm generates** from the binary, as
  opposed to the ones the spec states.  For the Servo binary this is the
  list that matters (below).
- The `__libc_start_main` *version*: Harbour requires `@GLIBC_2.34`, which
  only a 5.x SDK's glibc provides.  `rpm.yml` defaults to **5.2.0.15**, the
  Jolla Phone's baseline, for the mock build; the Servo binary is linked
  against that same target root, so it carries the same version.
- The RPATH (1.6.3) and the real file modes in the package.
- That the app works under Sailjail.  Running it from a terminal or the
  IDE bypasses the sandbox entirely, so a missing permission does not
  surface until QA installs it.  Force it: `sailjail /usr/bin/harbour-tuuli`.
  The sandbox confines `$HOME`, not the read-only system tree: the test
  that shows it working is failing to read *another app's* directory under
  `~/.local/share/`.  Silica's pickers run inside the app's own process, so
  a file the grant does not cover is one the picker can offer and the app
  cannot open — which is why file uploads declare `UserDirs` beside
  `Pictures`, `Videos` and `Documents`.
- Everything in the quality bar QA applies by hand — no placeholder
  content, translated strings, `Theme` values rather than pixel counts,
  recoverable errors, a useful cover.

## Exporting `main()`

Harbour rejects a Silica app whose binary does not export `main()`: the
`silica-qt5` booster in mapplauncherd `dlopen()`s the binary and looks the
symbol up dynamically.  C++ apps mark it `Q_DECL_EXPORT`.

Rust has no equivalent.  `fn main` becomes an ordinary global symbol, which
lives only in `.symtab` — and rpmbuild strips `.symtab` on the way into the
package, so by the time Harbour looks there is nothing there.
`crates/tuuli-browser/build.rs` and `servo/app/build.rs` pass
`--dynamic-list=main.dynlist` at link time to put `main` in `.dynsym`,
where stripping cannot reach it.  `--dynamic-list` rather than
`--export-dynamic-symbol`, which needs binutils 2.35 and so may not exist
in the SDK, or `--export-dynamic`, which would export every symbol.

## QtWidgets, and the vendored qmetaobject

`qmetaobject-rs` builds its QML engine on `QApplication`, which comes from
QtWidgets — a library Harbour does not allow, since a Silica app is
expected to use QtGui's `QGuiApplication`.  Upstream carries that
unconditionally, with no feature to turn it off.

Tuuli creates its own application object (`SailfishApp::application`, or
`QGuiApplication` on a host) and never uses qmetaobject's, but the C++
that references `QApplication` is compiled into the crate regardless, and
a reference is enough for the linker to record the dependency.  So
`third_party/qmetaobject` is upstream 0.2.10 plus
`third_party/qmetaobject.patch`: three lines, swapping the include, the
member type and the constructor.  `qttypes` separately passes
`-lQt5Widgets` unconditionally; the two `build.rs` link with `--as-needed`,
which drops any library no symbol refers to — and that only works
*because* the patch removed the last reference.

Carrying someone else's crate in-tree is only safe while the difference
is visible, so `ci/vendor-check.sh` fetches the crates.io tarball, applies
the patch, and requires the result to match the vendored tree exactly.
The crate's own `tests/` are not vendored; cargo never builds a
dependency's tests.

## The SDK's Rust, and the two engines

The Sailfish SDK ships **Rust 1.75.0**, and builds the mock-engine package
with it: the workspace declares `rust-version = "1.75"`, `Cargo.lock` stays
in the v3 format that cargo 1.75 can read (`ci/check-lockfile.sh`), and
CI's `msrv` job compiles with that toolchain.

Servo needs a current Rust, and Rust does not mix compiler versions in one
link.  So the Servo binary is not built by the SDK at all: `servo/build.sh`
cross-compiles `servo/app` with Servo's pinned toolchain against a copy of
the SDK's target root, and `rpm/harbour-tuuli.spec --with servo` installs
the result.  Harbour validates the RPM, not the build process, so this is
within the rules; the glibc and Qt the binary links are the target's.

Both builds produce the same package name.  There is no `Conflicts:` or
`Provides:` between them (Harbour allows neither); the mock package is a
development build and is never submitted.

## The open questions for Jolla

The mock-engine package breaks no rule (the source check and the selftest
say so on every push; the validator will confirm on the first `rpm.yml`
run).  The Servo package is another matter, and the items below are the
ones this repository cannot settle by itself.  Each becomes a line in
`ci/harbour/waivers.conf` once the validator has named it precisely.

### 1. The engine's linked libraries

Harbour allows a fixed list of shared libraries (`ci/harbour/allowed_libraries.conf`).
Servo, statically linked into the binary, still needs the system's
GStreamer (`libgstreamer-1.0`, `libgstapp-1.0`, `libgstvideo-1.0` and
friends — the media path, spec 8.2, decodes through gst-droid, which is
only reachable through the system GStreamer), FreeType and HarfBuzz for
text, and possibly more (the validator's first run is the definitive
list).  Of those, only `libfontconfig`, `libEGL`/`libGLESv2`, `libdbus-1`,
`libssl`/`libcrypto` and GLib are on the list.

What can be done here: build FreeType and HarfBuzz into the binary (both
crates support it) and keep the dynamic list to GStreamer.  What cannot:
GStreamer's plugin loader has to be the system's copy, so bundling it under
`/usr/share/harbour-tuuli/lib/` — the one place Harbour permits a private
`.so` — would put two GStreamer cores in one process.  The ask is for the
GStreamer core libraries to be allowed, as they already are for Qt's own
multimedia stack (`qt5-qtmultimedia` is an allowed dependency and links
them), or for guidance on an accepted alternative.

### 2. Transfer Engine under Sailjail

Downloads are mirrored into Settings → Transfers over D-Bus
(`org.nemo.transferengine`, spec 8.3).  No permission on the whitelist
names that service.  If the sandbox blocks the call, downloads still
complete (the engine writes the file) and only the Transfers entry is
missing; whether a permission covers it, or is planned, is a question for
Jolla.  The M0 device run under `sailjail` answers whether it matters.

### 3. Binary size

A Servo-linked, LTO'd binary with debug info stripped is on the order of a
hundred megabytes.  Harbour's rules say nothing about size; the submission
form may.  Ask before the first upload.

### 4. A URL handler (M4)

Registering as a browser (`MimeType=`, `x-scheme-handler/http`) is not
declared before M4 (spec N1, `docs/PACKAGING.md`).  Whether Harbour accepts
a second default browser is a question for then, not now.

### 5. The threat model

Spec 9.2 is honest that web content is not sandboxed from the app.  The
store description will carry that text (`docs/THREAT_MODEL.md`); whether
QA accepts a browser on those terms is theirs to decide, and better asked
before the engine work is finished than after.

## Before submitting

1. Build the Servo RPM (`rpm.yml`, engine `servo`); the validator step
   runs automatically.  Read every warning, not just the errors.
2. Install on the Jolla Phone and launch it as
   `sailjail /usr/bin/harbour-tuuli`.
3. Exercise every permission-dependent path under the sandbox: a file
   upload from each picker, a download (does it appear in Transfers?),
   media playback, sharing a page.
4. Delete the cache directory while the app runs; confirm nothing breaks.
5. Confirm **Version** was bumped, not just Release.  Harbour refuses an
   update that does not sort higher than the one in the Store.
6. Set "From OS version" to 5.2.0 on the submission form.  The spec cannot
   say so: `sailfish-version` is not an allowed dependency.
