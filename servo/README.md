# servo/

Everything that touches the real engine lives here, outside the cargo
workspace at the repository root.

| Path | What |
|---|---|
| `SERVO_TAG` | the pinned Servo release; `backend/Cargo.toml` pins the same tag as `tag = "v<SERVO_TAG>"` |
| `backend/` | `tuuli-servo-backend`: `ServoEngine`/`ServoWebView`, the `tuuli_core::engine` traits over libservo's Rust API |
| `app/` | `tuuli-browser-servo`: the `harbour-tuuli` binary linked with the backend (`tuuli_browser::run` + `create_engine`) |
| `build.sh` | cross-compiles `app/` for aarch64 SFOS against the SDK target root (`--sysroot`, or a local SDK) and packs the tarball `rpm/harbour-tuuli.spec --with servo` installs; the `rpm` workflow runs it |
| `patches/` | the public patch queue on top of the tag (`series`; empty) |

## Why these are not workspace members

`backend/Cargo.toml` depends on `servo = { git = ..., tag = ... }`.  Cargo
resolves a workspace as a whole, so if the backend were a member every
`cargo build`, `cargo test` and `cargo metadata` on a host — including
CI and IDEs — would clone Servo and resolve its dependency tree first.
Keeping the two Servo-linked packages as standalone packages (each with an
empty `[workspace]` table) and listing them under `exclude` in the root
manifest means the workspace builds with the mock engine only, and Servo
is touched by exactly two commands:

    cargo check --manifest-path servo/backend/Cargo.toml   # API reconciliation (CI, on push)
    servo/build.sh                                         # the device binary

## Backend status

`backend/src/lib.rs` is reconciled against what the `servo` crate at
0.5.0 exports (`ServoBuilder`, `WebViewBuilder`, `WebViewDelegate` and
its embedder controls, `ServoDelegate`, `RenderingContext`,
`EventLoopWaker`): `cargo check` on it passes (`docs/M0-CHECKLIST.md`).
Every `WebView` trait method the tag has no counterpart for is a logged
no-op and is listed in `docs/UPSTREAM.md`, with what 0.5.0 does offer;
nothing is worked around by reaching past the public API.

## Media

Servo's GStreamer media backend is the backend's `media` cargo feature
(`servo/app` forwards it; `servo/build.sh --media`), off by default: it
links the system GStreamer including `libgstwebrtc-1.0`, which the
Sailfish target has to provide and Harbour has to allow first
(docs/HARBOUR.md).  Without it Servo's dummy media backend is built in:
pages load and paint, `<audio>`/`<video>` do not play.

## Running with the mock engine instead

The Servo binary accepts `--mock-engine` (or `TUULI_ENGINE=mock` in the
environment) and then behaves exactly like the mock-engine build of the
package, which is useful for telling engine problems from chrome problems
on a device.
