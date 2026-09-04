# M0 — feasibility spike

Exit criteria, not a release (spec 10).  Fail fast on the three things that
could kill the project.  If M0 takes more than six weeks, re-evaluate the
whole approach.

## 1. Engine cross-build

- [ ] `servo/build-libservo.sh` completes against the SFOS 5.2 aarch64
      target root, SpiderMonkey included.
- [ ] `llvm-nm` shows the `servo_*` symbols; no host RUNPATH leaks.
- [ ] `tools/check-capi-header.sh` passes: the cbindgen header from the
      tag matches `servo/capi/servo_capi.h`.  If not, reconcile the header,
      the stub and `ServoEngine`/`ServoWebView` in one commit.
- [ ] `libservo` RPM installs on the device and `ldd` resolves against
      system libraries only (gstreamer, fontconfig, freetype, harfbuzz,
      EGL/GLESv2 via hybris).

Where the C ABI surface used by the shim is not in the tag, it is added
upstream in `servo_capi` (see [UPSTREAM.md](UPSTREAM.md)); the header in
this tree is the target, not a fiction to be worked around.

## 2. WebRender on Mali-G610 through libhybris

- [ ] `servo_init` on the Qt render thread succeeds (shader compile).
- [ ] A page paints into the `QQuickFramebufferObject` FBO and appears the
      right way up (check `mirrorVertically`).
- [ ] Probe `EGL_KHR_fence_sync` and dmabuf import; record what is missing.
- [ ] Try the threaded scene-graph loop first; if the render-thread paint /
      GUI-thread event loop split misbehaves, switch to
      `QSG_RENDER_LOOP=basic` (Settings → Developer) and record the
      outcome.  Both must be tried; the choice goes into ARCHITECTURE.md.
- [ ] Cover/minimise/sleep the app: confirm the persistent context survives
      or that the tear-down/re-init path (`renderContextLost`) restores the
      tab.

## 3. Plausible frame rate in a bare window

- [ ] The mock-engine RPM runs the chrome on the device (validates Qt 5.6,
      Silica, sailjail profile, paths).
- [ ] The servo RPM shows a real page; scroll and pinch through Servo's own
      touch pipeline.
- [ ] Frame statistics overlay reads well under 16.7 ms for an article page.
      This is not the §11 measurement, only a sanity check.

## Facts to record on a physical unit (spec 3.1, 15)

- [ ] SoC / GPU / driver strings from `dmesg` and `eglQueryString`.
- [ ] Panel refresh rate (`QScreen::refreshRate`).  Update
      `tools/budgets.json` `panel_hz`.
- [ ] `QScreen::devicePixelRatio` and `physicalDotsPerInch`; the value
      `Css::deriveDevicePixelRatio` picks.
- [ ] Is the adaptation libhybris throughout?  Any mainline component?
- [ ] gst-droid decoder output format WebRender can sample without a copy.
