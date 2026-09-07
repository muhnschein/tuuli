# M0 — feasibility spike

Exit criteria, not a release (spec 10).  Fail fast on the three things that
could kill the project.  If M0 takes more than six weeks, re-evaluate the
whole approach.

## 1. Engine cross-build

- [x] `cargo check --manifest-path servo/backend/Cargo.toml` passes
      against the pinned tag (2026-09-05, host, with and without the
      `media` feature): the backend is reconciled with what the `servo`
      crate at 0.5.0 actually exports (`ServoBuilder`, `WebViewBuilder`,
      `WebViewDelegate` and its embedder controls, `RenderingContext`).
      Every `WebView` method the tag has no counterpart for is a logged
      no-op, recorded in [UPSTREAM.md](UPSTREAM.md).  One build-graph
      fact came out of it: Servo's storage crate links rusqlite 0.38, so
      tuuli-core's rusqlite is a range shared with it (root
      `Cargo.toml`; the mock build stays on 0.31 through the lockfile).
- [x] The `rpm` workflow with engine `servo` completes (run 16, commit
      5f395ed, 2026-09-06): `servo/build.sh` builds against the SFOS 5.2
      aarch64 target root lifted out of the SDK image, SpiderMonkey
      included, and qttypes/qmetaobject-rs build against the target's
      Qt 5.6 headers through `QT_INCLUDE_PATH`.  40 minutes of
      cross-compile on a 4-core runner, then the RPM.

      Seven runs of iteration got there, and what each cost is worth
      knowing before the next rebase: the sysroot copy (device nodes,
      unreadable files, ownership); the host linker, since `-fuse-ld=lld`
      has to be in the C flags and not only the Rust link line; the
      target having no `/usr/lib/gcc` of its own, so the SDK tooling's
      cross gcc directory is lifted into the sysroot; no unsuffixed
      `libgcc_s.so`/`libstdc++.so` to resolve `-lgcc_s`, which a device
      root has no reason to ship; Qt 5.6's `qtypetraits.h` against clang
      16+ (`-Wno-enum-constexpr-conversion`); and SpiderMonkey's configure
      probing with the compiler command alone, so the cross flags travel
      inside `CC`/`CXX` rather than in `CFLAGS`.  `servo/build.sh` now
      preflights all of that in seconds rather than tens of minutes.
- [x] `llvm-readelf` shows an aarch64 binary with no RUNPATH, and the
      libraries it needs are the target's (rpm's own dependency scan, run
      17): `ld-linux-aarch64.so.1`, `libc`, `libm`, `libz`, `libgcc_s`,
      `libstdc++`, `libfontconfig`, `libGLESv2`, `libsailfishapp`, and
      Qt5 Core/Gui/Qml/Quick/DBus.  No GStreamer (no `media` feature), no
      FreeType or HarfBuzz (inside the binary), no OpenSSL (rustls).
      `libgstwebrtc-1.0` and the rest join the list once the workflow
      runs with `media` on, which needs `gstreamer-webrtc-1.0.pc` in the
      target (the sysroot step installs it and warns if the SDK has none).

      Run 17 also exposed that `servo/build.sh`'s own NEEDED check had
      been a no-op: it parsed `--needed-libs` for a bracketed form that
      output never had, matched nothing, and looped zero times, so it
      could not have caught a library missing from the sysroot.  It reads
      the dynamic section's NEEDED entries now and fails if it parses
      none.  rpm's scan is what produced the list above.
- [x] The workflow's validator step lists which of those libraries
      Harbour does not allow.  **Without `media`, none.**  Jolla's own
      validator passes the Servo-linked package outright (`!END!PASS!`,
      empty RPATH, no vendor set), so `ci/harbour/waivers.conf` stays
      empty and the engine's linked libraries are not, as feared, a
      question for Jolla in this configuration.  They become one when
      `media` is on (`docs/HARBOUR.md`); that build is not attempted yet.
- [ ] The `harbour-tuuli` RPM installs on the device and `ldd` resolves
      against system libraries only (fontconfig, EGL/GLESv2 via hybris;
      FreeType and HarfBuzz are inside the binary, GStreamer absent until
      `media` is on).  The package is 38 MB.

## 2. WebRender on Mali-G610 through libhybris

- [x] The render loop is the basic one on the device, under the
      silica-qt5 booster as well as from `sailjail /usr/bin/harbour-tuuli`:
      the journal shows `tuuli: first frame rendered on the GUI thread`
      from both launch paths (Jolla Phone, SFOS 5.2, 2026-09-05, mock
      engine).  The design question in ARCHITECTURE.md is settled.
- [ ] Engine initialisation inside the first `render()` succeeds
      (shader compile).
- [ ] A page paints into the `QQuickFramebufferObject` FBO and appears the
      right way up (check `mirrorVertically`).
- [ ] Probe `EGL_KHR_fence_sync` and dmabuf import; record what is missing.
- [ ] Measure what the basic render loop costs the chrome's own
      animations (page transitions, pulley) against §11; there is no
      threaded fallback for the engine path, so the number goes into
      ARCHITECTURE.md as a fact, not a choice.
- [ ] Cover/minimise/sleep the app: confirm the persistent context survives
      or that the tear-down/re-init path (`RenderContextLost`) restores the
      tab.

## 3. Plausible frame rate in a bare window

- [x] The mock-engine RPM (`rpm` workflow, engine `mock`) runs the
      chrome on the device, from the app grid (the invoker enforcing the
      sandbox) and as `sailjail /usr/bin/harbour-tuuli`: Qt 5.6, Silica,
      the sailjail profile, the data paths and the engineering-English
      catalog all check out (2026-09-05).  Jolla's validator accepts the
      package outright.  At exit the driver logs two `invalid handle:
      (nil)` lines and "EGLDisplay was not properly terminated"; to look
      at once the engine is in, since that is when GL teardown order
      matters.
- [ ] Transfer Engine reachable from inside the sandbox: start a download
      and look for it in Settings → Transfers (needs the engine).
- [ ] The servo RPM shows a real page; scroll and pinch through Servo's own
      touch pipeline.
- [ ] Frame statistics overlay reads well under 16.7 ms for an article page.
      This is not the §11 measurement, only a sanity check.

## Facts to record on a physical unit (spec 3.1, 15)

The app prints most of these itself on its first frame (`tuuli: GL
vendor=... renderer=... version=...`, `tuuli: GL context GLES x.y, N
extensions; ...`, `tuuli: window WxH, Qt dpr, physical dpi, refresh,
content dpr`); copy the lines from the journal into this list.

- [x] SoC: MediaTek `mt6858`; Mali GPU through libhybris
      (`/vendor/lib64/egl/mt6858/libGLES_mali.so`), from the journal of the
      mock-engine run.  Driver and GL version strings: from the
      first-frame log of the next build.
- [ ] Panel refresh rate (`QScreen::refreshRate`).  Update
      `tools/budgets.json` `panel_hz`.
- [ ] `QScreen::devicePixelRatio` and `physicalDotsPerInch`; the value
      `derive_device_pixel_ratio` picks.
- [ ] Is the adaptation libhybris throughout?  Any mainline component?
- [ ] gst-droid decoder output format WebRender can sample without a copy.
