# Tuuli — Servo-based Web Browser for Sailfish OS

**Codename:** Tuuli
**Package:** `tuuli-browser`
**Spec version:** 0.1 (draft)
**Date:** September 2026
**Target:** Sailfish OS 5.2+, aarch64, Jolla Phone (2026) and newer
**Engine:** Servo (libservo), pinned release, MPL-2.0
**App licence:** proposed MPL-2.0 (matches engine; avoids relicensing friction on shim code)

---

## 1. Summary

Tuuli is a native Sailfish OS browser: Silica QML chrome driving a Servo rendering engine through a
C ABI and a Qt Quick scene-graph integration layer. It exists to give Sailfish a browser whose engine
is maintained upstream on a monthly cadence and whose mobile support (touch, pinch zoom, mobile
viewport) is a first-class upstream concern rather than a downstream patch set.

It is not a replacement for Sailfish Browser. It ships alongside it, and for a long time it will be
the second browser on the device. Plan accordingly: everything in this spec assumes the user has a
working Gecko browser to fall back to.

### 1.1 Why a single-device target

Restricting to Jolla Phone (2026) and newer on SFOS 5.2+ removes most of the porting surface that
would otherwise dominate the project:

- One SoC family, one GPU, one EGL implementation to validate against.
- One screen geometry and DPI to tune the mobile viewport against.
- No 32-bit ARM, no Qt version fragmentation across old device adaptations.
- Community ports (Xperia 10 V etc.) may work; they are explicitly unsupported.

---

## 2. Goals and non-goals

### 2.1 Goals

- **G1** — Render mainstream mobile web content correctly enough for daily reading, search, news,
  webmail, forums, and documentation.
- **G2** — Feel like a Sailfish app: Silica components, pulley menus, edge gestures, ambience colours,
  cover actions, correct orientation behaviour.
- **G3** — 60 fps scrolling and pinch zoom on the target hardware for typical article-weight pages.
- **G4** — Track Servo upstream with a rebase cost measured in days per month, not weeks.
- **G5** — Be honest about isolation: no claim of security parity with Gecko or Chromium.

### 2.2 Non-goals

- **N1** — Replacing Sailfish Browser as the system default or registering as the system URL handler
  (until at least M4).
- **N2** — DRM / Widevine. Never.
- **N3** — WebExtensions or any Chrome/Firefox extension compatibility.
- **N4** — Harbour (Jolla Store) compliance. Distribution is Chum and OpenRepos.
- **N5** — Sync, accounts, or any server-side component.
- **N6** — Desktop, tablet, or landscape-primary layouts beyond basic orientation support.
- **N7** — Supporting SFOS releases below 5.2 or non-Jolla hardware.

---

## 3. Platform baseline

### 3.1 Device

| Property | Value |
|---|---|
| Device | Jolla Phone (2026), shipping since 2026-07-08 |
| SoC | MediaTek Dimensity 7100, 6 nm |
| CPU | 4× Cortex-A78 @ 2.4 GHz, 4× Cortex-A55 @ 2.0 GHz |
| GPU | Mali-G610 MC2 (GLES 3.2, Vulkan 1.x) |
| RAM | 8 GB base, 12 GB upgrade SKU |
| Display | 6.36" AMOLED, 1080 × 2260, ~394 ppi |
| Battery | 5500 mAh, user-replaceable |

Device specs published by Jolla are marked preliminary and were revised during the pre-order run.
**Verify SoC, GPU and panel refresh rate against a physical unit before finalising the performance
budgets in §11.**

### 3.2 OS and stack

- Sailfish OS 5.2, aarch64, RPM-based, systemd, Wayland via **lipstick**.
- **Qt 5.6.** Sailfish remains on Qt 5.6 for Qt licensing reasons; community Qt 6 work exists but is
  not the platform. Design for Qt 5.6 APIs only. Do not depend on anything newer.
- **Silica** QML components (proprietary, but QML import is fine for an MPL app).
- Graphics via **libhybris** against the Android BSP driver. This is the single most important
  platform fact in this document; see §5.3.
- **sailjail** (firejail-derived) sandboxing, declared in the `.desktop` file.
- GStreamer 1.x with **gst-droid / droidmedia** for hardware video decode.
- **Maliit** input method, **connman** networking, **geoclue** positioning, **PulseAudio** audio,
  **Nemo Transfer Engine** for downloads and sharing.

### 3.3 Engine

- Pin to a tagged Servo release (0.5.0, released August 2026, or later). Do not track `main`.
- Consume via **`servo_capi`**, the C ABI crate added upstream in May 2026, built as a shared object.
  Where `servo_capi` does not yet expose something we need, extend it upstream rather than reaching
  into `libservo` from Rust glue — this keeps our patch queue small and our contributions useful.

---

## 4. Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Silica QML chrome  (tabs, toolbar, overlays, settings) │
└───────────────┬─────────────────────────────────────────┘
                │ QML properties / signals / slots
┌───────────────▼─────────────────────────────────────────┐
│  libtuuli-qml  (C++17, Qt Quick plugin)                 │
│   • TuuliWebView : QQuickFramebufferObject              │
│   • gesture recognition, IME proxy, permission plumbing │
└───────────────┬─────────────────────────────────────────┘
                │ servo_capi (C ABI, opaque pointers)
┌───────────────▼─────────────────────────────────────────┐
│  libservo.so  (Rust: script, layout, style, WebRender)  │
└───────────────┬─────────────────────────────────────────┘
                │ EGL / GLES 3.2 via libhybris
┌───────────────▼─────────────────────────────────────────┐
│  Mali-G610 driver (Android blob)                        │
└─────────────────────────────────────────────────────────┘
```

### 4.1 Process model

**M1–M3: single process.** Servo is predominantly multi-threaded rather than multi-process; its
optional content-process isolation is not the well-trodden path and its sandboxing is not
production-grade. Pretending otherwise would be worse than being explicit about it.

**Consequence:** an engine crash takes the whole app down, and a content compromise reaches
everything the sailjail profile permits. Mitigations:

- Keep the sailjail profile as tight as the feature set allows (§9.1).
- Persist session state to disk aggressively (§8.4) so a crash costs a second, not a session.
- Revisit out-of-process content in M4 once upstream's story firms up. Design the C++ shim behind an
  interface that could later be swapped for an IPC proxy without touching QML.

### 4.2 Threading

| Thread | Owns |
|---|---|
| Qt main (GUI) | QML scene, input events, all `servo_capi` calls that mutate webview state |
| Qt render | GL context, `QQuickFramebufferObject::Renderer`, Servo paint + swap |
| Servo internal | script, layout, style, networking, image decode (Servo's own pools) |

`servo_capi` calls are made from the GUI thread and marshalled onto the render thread only where the
API requires an active GL context. All embedder callbacks from Servo are posted to the GUI thread via
`QMetaObject::invokeMethod(Qt::QueuedConnection)` before touching any Qt object. No Qt object is ever
touched from a Rust-owned thread.

---

## 5. Rendering and compositing

### 5.1 Approach: FBO into the Qt scene graph

`TuuliWebView` derives from `QQuickFramebufferObject`. Its `Renderer::render()` runs on the Qt render
thread with the scene-graph GL context current, drives one Servo frame, and leaves the result in the
FBO texture that Qt then composites into the QML scene.

This is chosen over a `wl_subsurface` because:

- Silica chrome must overlay the page with translucency (pulley menus, ambience-tinted toolbars,
  the tab overview transition). Subsurfaces make that awkward or impossible.
- lipstick's handling of app-created subsurfaces is not a well-supported path.
- FBO keeps everything inside one Qt-managed swap, so we inherit Sailfish's frame pacing.

The cost is one extra full-screen texture blit per frame and the render thread being blocked for the
duration of Servo's paint. §11 sets the budget that makes this acceptable; §14 lists the subsurface
fallback as a live risk mitigation.

### 5.2 Rendering context ownership

**Servo must not create its own EGL context.** Implement the `RenderingContext` interface (exposed
through `servo_capi`) as a wrapper over the `QOpenGLContext` Qt has already created, so both sides
share one context and one hybris EGL display. Creating a second context risks divergent EGL configs
and driver-specific failures on the Mali blob.

Requirements on the wrapper:

- Report GLES 3.2 capability so WebRender takes its modern path.
- Handle `make_current` / `swap_buffers` as no-ops where Qt already owns them; Servo paints into a
  bound FBO, not a window surface.
- Survive Qt scene-graph invalidation (app minimised to cover, device sleep) by tearing down and
  recreating the Servo rendering context rather than leaking GPU allocations.

### 5.3 libhybris caveats

The GL driver is an Android blob reached through libhybris. Expect:

- **Thread-affinity strictness.** The blob is less forgiving than Mesa about contexts being made
  current on unexpected threads. Constrain all GL to the Qt render thread.
- **EGL extension gaps.** Do not assume `EGL_KHR_fence_sync` or dmabuf import are available; probe
  and fall back.
- **Driver-specific shader compile failures.** WebRender's shader set is validated against Mesa and
  Android GPUs, but Mali-G610-under-hybris is not in Servo's CI. Budget spike time in M0 (§10) purely
  for "does WebRender's shader set compile and run here at all."

An M0 exit criterion is a WebRender-rendered page on the device, produced before any UI work begins.

### 5.4 Zoom, scroll and viewport

- Use Servo's existing pinch-zoom viewport support rather than reimplementing it in QML. Upstream
  gained proper pinch-zoom viewport panning in late 2025 and has been iterating since.
- Set `UserAgentPlatform` to a mobile value so mobile UA string, `<meta name="viewport">` handling and
  touch-event defaults engage. Upstream currently derives `is_mobile()` from Android and OpenHarmony;
  there is an open discussion about mobile Linux joining that set. **Contribute this upstream** —
  either a runtime device-type query or an explicit build/pref override — rather than carrying a patch.
- Overscroll behaviour follows Silica conventions, not Android's: no glow, no rubber-band bounce
  beyond what Silica's own flickables do.

---

## 6. Input

### 6.1 Event routing

Qt touch events arrive on `TuuliWebView`, are converted to Servo touch input events, and are
forwarded with their raw coordinates in CSS pixels after device-pixel-ratio conversion. Servo owns
the async touch pipeline (scroll, fling, pinch); the shim does not implement its own kinetic scroller.

DPR is `QScreen::devicePixelRatio()` on the target panel. At ~394 ppi this is likely 2.0; confirm on
hardware and do not hardcode.

### 6.2 Gesture arbitration

Sailfish's edge gestures belong to lipstick and must not be swallowed:

| Gesture | Owner |
|---|---|
| Swipe from left/right screen edge | lipstick (app close / switch) — never consumed |
| Swipe from top edge | lipstick (top menu) — never consumed |
| Swipe from bottom edge | Tuuli — reveals toolbar |
| Single-finger drag in page | Servo (scroll) |
| Two-finger pinch in page | Servo (zoom) |
| Long-press in page | Tuuli — context menu, with Servo hit-test for the target |
| Double-tap | Servo — smart zoom to element |

Long-press must trigger on a hold without requiring incidental movement. This is a known irritation in
the community Gecko builds; do not reproduce it.

### 6.3 Text input

Servo signals focus of an editable element with an input-type hint. The shim maps this onto a hidden
QML `TextInput` proxy that Maliit attaches to:

1. Servo reports editable focus + input type + current value + selection.
2. Shim shows the proxy, sets `inputMethodHints` from the type (`url`, `email`, `number`, `password`).
3. Maliit composes into the proxy; each committed change is forwarded to Servo as key/composition
   events.
4. Servo's selection and caret updates flow back to keep the proxy in sync.

Also required: keep the focused element visible above the VKB by adjusting the Servo viewport rect
when the keyboard shows, rather than resizing the whole surface.

---

## 7. User interface

Portrait-first, Silica throughout, ambience-aware.

### 7.1 Views

- **Start page** — recent tabs, bookmarks grid, search field with keyboard focused on cold start.
- **Page view** — full-bleed content; auto-hiding bottom toolbar (URL, tab count, back, overflow).
- **Tab overview** — Silica grid of live-ish thumbnails; swipe-to-close, long-press to reorder.
- **Settings** — engine prefs, privacy, downloads location, UA override, developer toggles.
- **Downloads** — backed by Nemo Transfer Engine so downloads appear in the system transfers UI.
- **Cover** — page title + favicon; cover actions: new tab, reload.

### 7.2 Pulley menus

- Page view, top pulley: reload, new tab, share, add bookmark.
- Page view, bottom pulley: find in page, desktop-mode toggle, page info.
- Tab overview, top pulley: new tab, new private tab, close all.

### 7.3 Private browsing

Servo exposes per-webview private browsing and a `SiteDataManager` capable of clearing cookies,
`sessionStorage` and `localStorage` by origin. Map private tabs onto Servo's private contexts and
route all storage, history, cache and download bookkeeping by privacy flag. Never mix a private and a
non-private document in the same webview.

Visual treatment follows Silica dark-accent conventions; no separate window concept (Sailfish apps are
single-window).

---

## 8. Platform integration

### 8.1 Networking

- Servo uses hyper + rustls with its own trust configuration. Point it at the system CA bundle
  (`/etc/pki/tls/certs/`) rather than shipping our own roots.
- Read proxy configuration from connman.
- **Captive portals:** SFOS ships `sailfish-captiveportal`, which launches a browser for the portal
  login. Tuuli registers as an alternative handler but does not take over the default until M4.

### 8.2 Media

- Servo's media pipeline is GStreamer-based, which is a genuine stroke of luck: Sailfish already has
  GStreamer 1.x with **gst-droid** hardware decoders (the same path AppSupport and the camera use).
- **Requirement:** `<video>` H.264/VP9 playback routes through gst-droid for hardware decode. Software
  decode of 1080p on the A78 cluster will cook the battery and is acceptable only as a fallback.
- Audio out via `pulsesink`. Register with the Sailfish audio policy so playback ducks and pauses
  correctly on calls.
- MPRIS metadata export so media controls work from the lock screen. M3.

### 8.3 Permissions and hardware

| Capability | Backing | Milestone |
|---|---|---|
| Geolocation | geoclue, via Servo's permission callback | M3 |
| Camera / microphone (getUserMedia) | gst-droid sources | M4 |
| Notifications | Nemo notifications | M4 |
| Clipboard | Qt clipboard bridged to Servo | M2 |
| Share | Nemo Transfer Engine share UI | M2 |

Every permission prompt is a Silica dialog, denied by default, with per-origin persistence.

### 8.4 Session persistence

Write session state (tabs, scroll offsets, form state where cheap) to disk on a 5-second debounce and
on `aboutToQuit`, plus on every backgrounding. Given the single-process model (§4.1), fast and
complete session restore is the primary crash mitigation, so treat it as a correctness feature rather
than a convenience.

Storage lives under `~/.local/share/tuuli/`, respecting XDG and the sailjail profile.

### 8.5 Fonts

Servo's Linux font backend uses fontconfig + FreeType, both present. Verify emoji and CJK fallback
against the Sailfish font set; ship no fonts of our own.

---

## 9. Security and privacy

### 9.1 sailjail profile

Declare in `tuuli-browser.desktop`:

```
[X-Sailjail]
OrganizationName=org.tuuli
ApplicationName=browser
Permissions=Internet;Audio;Downloads;Pictures;Videos;Documents;UserDirs
```

Add `Location`, `Camera` and `Microphone` only at the milestone that uses them. Do not pre-declare
permissions for features that do not exist yet.

### 9.2 Threat model, stated plainly

Tuuli in M1–M3 offers **no meaningful sandbox between web content and the app's own privileges.**
Servo's sandboxing is incomplete upstream, and we run single-process. Web content that achieves code
execution gets everything sailjail grants the app.

This is disclosed in the app's About page and in the store description. Users who need a hardened
browser should use Sailfish Browser. Removing this caveat requires out-of-process content plus a
functioning seccomp policy, which is an M4-or-later question.

### 9.3 Content blocking

Servo has per-webview user scripts and user stylesheets (with add and remove, as of early 2026) but no
WebRequest-style network interception API.

- **M3:** cosmetic filtering via user stylesheets from an EasyList-derived cosmetic rule set.
- **Stretch:** network-level blocking via a custom protocol handler or an upstream request-interception
  API. If we build the latter, build it upstream.
- Do not ship a half-working blocker and call it ad blocking.

### 9.4 Defaults

- Third-party cookies blocked.
- DNT / GPC sent.
- Referrer policy `strict-origin-when-cross-origin`.
- Search defaults to a non-tracking engine, user-changeable, no default-search revenue arrangement of
  any kind.

---

## 10. Milestones

### M0 — Feasibility spike (exit criteria, not a release)

The point of M0 is to fail fast on the three things that could kill the project.

1. `libservo` + `servo_capi` cross-compiles for `aarch64-unknown-linux-gnu` against the SFOS 5.2
   target sysroot, including **mozjs / SpiderMonkey**, which is the hardest dependency by a wide
   margin.
2. WebRender's shader set compiles and renders on Mali-G610 through libhybris EGL.
3. A page renders on the device in a bare Qt window at a plausible frame rate.

No QML, no chrome, no tabs. If M0 takes more than six weeks, re-evaluate the whole approach.

### M1 — Single-tab viewer

One webview, hardcoded URL bar, touch scroll, pinch zoom, back/forward. FBO integration into a minimal
QML scene. Crashes are acceptable. Ships to nobody.

### M2 — Usable browser

Tabs, tab overview, session persistence, history, bookmarks, downloads via Transfer Engine, Maliit
text input, clipboard, share, find in page, Silica chrome complete. First Chum release, flagged
clearly as experimental.

### M3 — Daily-drive candidate

Private browsing, geolocation, `<video>` with hardware decode, MPRIS, cosmetic content blocking,
per-origin permissions, cover actions, orientation handling, performance work against §11 budgets.

### M4 — Hardening and integration

Out-of-process content evaluation, getUserMedia, notifications, captive-portal handler registration,
optional system default browser registration.

---

## 11. Performance budgets

Measured on Jolla Phone (2026), 8 GB SKU, on Wi-Fi, against a fixed corpus of ten real pages
(a news article, a webmail inbox, a forum thread, a docs site, a wiki page, a maps-free search results
page, a GitHub file view, a Mastodon timeline, a webshop product page, a heavy JS SPA).

| Metric | Budget |
|---|---|
| Cold start to first paint of start page | ≤ 1200 ms |
| Warm start to first paint | ≤ 400 ms |
| Time to first contentful paint, article page | ≤ 2000 ms |
| Sustained scroll frame rate, article page | ≥ 58 fps at panel refresh |
| Dropped frames during pinch zoom | < 3% |
| RSS, one tab, article page | ≤ 450 MB |
| RSS, eight tabs | ≤ 1.4 GB |
| Battery: 30 min continuous reading | ≤ 8% of a 5500 mAh cell |

Panel refresh rate is unconfirmed; if the display is 90 or 120 Hz, restate the frame-rate budgets
against the real value rather than leaving them at 60.

Budget failures are release blockers for M3, not M2.

---

## 12. Build and packaging

### 12.1 Toolchain

- Build inside the **Sailfish Platform SDK** target root for correct glibc and system library versions.
- Rust toolchain pinned by Servo's `rust-toolchain.toml`; do not float it.
- Cross-compile: `--target aarch64-unknown-linux-gnu`, with `CC`, `CXX`, `AR` and
  `PKG_CONFIG_SYSROOT_DIR` pointed at the SDK target root for C/C++ dependencies.
- SpiderMonkey needs a recent Clang and builds its own object tree; it will not use the SDK's aged GCC.
  Expect to supply a separate Clang and to spend real time here.
- Build `libservo` as a shared object consumed by the Qt plugin, not statically linked into the app
  binary, to keep link times and RPM rebuild times sane.

### 12.2 RPM

- `tuuli-browser` — app, QML, Silica UI, `.desktop`, sailjail profile.
- `libtuuli-qml` — Qt Quick plugin and C++ shim.
- `libservo` — engine shared object, versioned to the pinned Servo release.
- `tuuli-browser-debuginfo`.

Split the engine into its own subpackage so engine rebases and UI iteration are independently
shippable.

### 12.3 Distribution

**Chum** primary, **OpenRepos** secondary. Not Harbour: bundled non-allowed libraries and a large
statically-built Rust engine will not pass, and there is no point pretending otherwise.

### 12.4 Upstream tracking

- Pin to a Servo release tag. Rebase on a monthly cadence, aligned with Servo's release rhythm.
- `libservo` and `servo_capi` are pre-1.0 and churn. Budget one to three days per rebase and keep the
  C++ shim thin so churn is absorbed in one layer.
- Maintain a public patch queue. Anything carried for more than two rebases should be proposed
  upstream or abandoned.

---

## 13. Testing

- **Unit:** shim logic (event conversion, DPR maths, gesture arbitration) as host-side Qt tests.
- **Rendering:** reference screenshots for the ten-page corpus, compared per rebase to catch engine
  regressions early.
- **Manual device matrix:** the corpus plus a checklist of Sailfish integrations (VKB, share, transfer,
  cover, orientation, ambience) run before each Chum release.
- **Performance:** automated timing runs against §11 budgets, on-device, per release.
- **No WPT.** Web-platform conformance is upstream Servo's problem, and duplicating their CI is a poor
  use of a small team's time. Track their published pass rate instead and file compat bugs upstream
  with reduced test cases.

---

## 14. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| WebRender shaders fail on Mali-G610 via libhybris | Fatal | M0 exit criterion; fail fast |
| mozjs cross-build against SFOS sysroot proves intractable | Fatal | M0 exit criterion |
| Servo web compat too poor for daily use | High | Honest positioning as second browser; upstream compat bugs |
| FBO blit cost breaks frame budget | High | Fallback to `wl_subsurface` with QML chrome as separate surface |
| Qt 5.6 lacks something the integration needs | Medium | Verified in M0; Qt 5.6 has `QQuickFramebufferObject` |
| `libservo` API churn outpaces maintenance capacity | Medium | Pin releases, thin shim, upstream patches |
| No sandbox is unacceptable to users | Medium | Disclose plainly; tight sailjail profile; M4 work |
| Battery regression vs Gecko | Medium | §11 budget as release gate; hardware decode mandatory |
| Jolla Phone hardware differs from published preliminary specs | Low | Verify on device before M1 |

---

## 15. Open questions

1. Panel refresh rate — 60, 90 or 120 Hz? Determines §11 frame budgets.
2. Is the Jolla Phone (2026) adaptation libhybris-based, or is any part of the graphics stack mainline?
   This spec assumes libhybris throughout; confirm before M0.
3. Does upstream Servo want mobile-Linux as a recognised `UserAgentPlatform`, and can we land it?
4. Can `servo_capi` reach the coverage we need by M2, or do we need a Rust-side shim in the interim?
5. Does gst-droid's decoder expose the surface format WebRender can sample without a CPU copy?
6. Does Kumo's Servo backend (Wayland mobile Linux, added spring 2026) have solved problems we can
   borrow rather than rediscover? Talk to them early.

---

## 16. References

- Servo releases and `servo_capi` — github.com/servo/servo
- Servo embedding docs — book.servo.org, doc.servo.org
- Kumo, Wayland mobile browser with Servo and WebKit backends — github.com/catacombing/kumo
- Sailfish Browser (Gecko/EmbedLite reference architecture) — github.com/sailfishos/sailfish-browser
- Community Gecko ESR140/ESR153 ports for SFOS 5.1/5.2 — github.com/smatkovi/gecko-dev
