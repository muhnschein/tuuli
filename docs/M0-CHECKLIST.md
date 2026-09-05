# M0 — feasibility spike

Exit criteria, not a release (spec 10).  Fail fast on the three things that
could kill the project.  If M0 takes more than six weeks, re-evaluate the
whole approach.

## 1. Engine cross-build

- [ ] `cargo check --manifest-path servo/backend/Cargo.toml` passes
      against the pinned tag: the backend is reconciled with what
      `libservo` 0.5.0 actually exports (delegate method names, builder
      options, `RenderingContext`).  Every `WebView` method the tag has
      no counterpart for is a logged no-op, recorded in
      [UPSTREAM.md](UPSTREAM.md).
- [ ] The `rpm` workflow with engine `servo` completes: `servo/build.sh`
      builds against the SFOS 5.2 aarch64 target root lifted out of the
      SDK image, SpiderMonkey included, and qttypes/qmetaobject-rs build
      against the target's Qt 5.6 headers through `QT_INCLUDE_PATH`.
      Expect to iterate on the sysroot's development packages (the
      workflow's "Lift the target root" step lists them).
- [ ] `llvm-readelf` shows an aarch64 binary with no RUNPATH and only
      sysroot libraries in `NEEDED`.
- [ ] The workflow's validator step lists which of those libraries
      Harbour does not allow; record each in `ci/harbour/waivers.conf`
      and take the list to Jolla (`docs/HARBOUR.md`).
- [ ] The `harbour-tuuli` RPM installs on the device and `ldd` resolves
      against system libraries only (gstreamer, fontconfig, freetype,
      harfbuzz, EGL/GLESv2 via hybris).

## 2. WebRender on Mali-G610 through libhybris

- [ ] Engine initialisation inside the first `render()` succeeds
      (shader compile) with `QSG_RENDER_LOOP=basic`.
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

- [ ] The mock-engine RPM (`rpm` workflow, engine `mock`) runs the
      chrome on the device, launched as `sailjail /usr/bin/harbour-tuuli`
      (validates Qt 5.6, Silica, the sailjail profile, the data paths,
      and whether Transfer Engine is reachable from inside the sandbox).
- [ ] The servo RPM shows a real page; scroll and pinch through Servo's own
      touch pipeline.
- [ ] Frame statistics overlay reads well under 16.7 ms for an article page.
      This is not the §11 measurement, only a sanity check.

## Facts to record on a physical unit (spec 3.1, 15)

- [ ] SoC / GPU / driver strings from `dmesg` and `eglQueryString`.
- [ ] Panel refresh rate (`QScreen::refreshRate`).  Update
      `tools/budgets.json` `panel_hz`.
- [ ] `QScreen::devicePixelRatio` and `physicalDotsPerInch`; the value
      `derive_device_pixel_ratio` picks.
- [ ] Is the adaptation libhybris throughout?  Any mainline component?
- [ ] gst-droid decoder output format WebRender can sample without a copy.
