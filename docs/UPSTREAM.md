# Upstream tracking

Spec 3.3 and 12.4 in practice.

## Cadence

- Pin to a Servo release tag (`servo/SERVO_TAG`).  Never track `main`.
- Rebase monthly, aligned with Servo's release rhythm.  Budget one to three
  days.  A rebase is: bump the tag, rebuild with `servo/build-libservo.sh`,
  reconcile `servo/capi/servo_capi.h` if the header check fails, update
  `src/lib/prefs/servoprefs.h` against the tag's pref names, run the host
  tests, run the device matrix.
- Keep the shim thin so churn lands in `src/lib/engine/servo*.cpp` only.

## Patch queue

`servo/patches/` with a `series` file, applied by the build script.  Every
patch names its upstream issue or PR.  Anything carried for more than two
rebases is proposed upstream or dropped.

## Items to land upstream

These are things the shim assumes of `servo_capi`.  Until each lands, the
corresponding shim call is a no-op or the feature is off; none of them is
worked around with Rust glue.

| Item | Spec | Used by |
|---|---|---|
| `SERVO_UA_PLATFORM_MOBILE_LINUX` (or a runtime device-type query) so `is_mobile()` engages on mobile Linux | 5.4 | `ServoEngine::initializeOnRenderThread` |
| `servo_webview_request_context_menu` hit test for an embedder-detected long-press | 6.2 | `TuuliWebView::onLongPressed` |
| `viewport_changed` with scroll offset, pinch zoom and content size | 8.4, 7.2 pulley handoff | `Tab`, `GestureArbiter` |
| `servo_webview_set_viewport_rect` (visible rect without resizing the surface) | 6.3 | `TuuliWebView::pushViewport` |
| IME callbacks with input type, current text, selection | 6.3 | `InputMethodProxy` |
| Per-webview UA override | 7.2 desktop mode | `Tab::applyDesktopMode` |
| Download callbacks with embedder-chosen destination | 7.1 | `DownloadManager` |
| Callbacks installable on auxiliary (window.open) webviews at creation | 4 | `ServoWebView(AuxiliaryTag)` |
| `servo_set_proxy` / proxy in instance config | 8.1 | `ConnmanProxy` |
| Clipboard get/set callbacks | 8.3 | `ServoEngine::Callbacks` |
| `servo_clear_site_data` by origin and kind (SiteDataManager) | 7.3 | `BrowserContext::clearBrowsingData` |
| Media session events | 8.2 (M3, MPRIS) | `Tab::mediaSession` |
| Request-interception API (stretch; the only route to network-level blocking) | 9.3 | not used yet |

## Compat bugs

File upstream with reduced test cases.  Track Servo's published WPT pass
rate; do not run WPT here (spec 13).
