# Upstream tracking

Spec 3.3 and 12.4 in practice.

## Cadence

- Pin to a Servo release tag (`servo/SERVO_TAG`, and the matching `tag =`
  in `servo/backend/Cargo.toml`).  Never track `main`.
- Rebase monthly, aligned with Servo's release rhythm.  Budget one to three
  days.  A rebase is: bump both pins; `cargo check --manifest-path
  servo/backend/Cargo.toml` and reconcile `servo/backend/src/lib.rs`
  against the tag's `WebViewDelegate`, `ServoDelegate` and
  `RenderingContext`; update the pref name table in
  `crates/tuuli-core/src/prefs.rs`; run the host tests; run
  `servo/build.sh`; run the device matrix.
- Keep the backend thin so churn lands in `servo/backend` only.  Nothing
  above `tuuli_core::engine` names a Servo type.

## Patch queue

`servo/patches/` with a `series` file.  When the queue is non-empty,
`servo/build.sh` clones the tag, applies the patches and points cargo at
the patched checkout through a `[patch]` section in
`servo/app/.cargo/config.toml`; with an empty queue cargo builds the tag
straight from git.  Every patch names its upstream issue or PR.  Anything
carried for more than two rebases is proposed upstream or dropped.

## Items to land upstream

These are things the backend needs from libservo's API.  Until each
exists in the pinned tag, the corresponding `WebView` trait method in the
backend is a logged no-op or the feature is off; none of them is worked
around locally.  Which rows are already covered by 0.5.0 is settled by
the M0 reconciliation of `servo/backend`.

| Item | Spec | Used by |
|---|---|---|
| A mobile-Linux `UserAgentPlatform` (or a runtime device-type query) so `is_mobile()` engages | 5.4 | `create_engine` |
| Context-menu hit test for an embedder-detected long-press | 6.2 | `WebViewItem` long press → `WebView::request_context_menu` |
| Viewport delegate call with scroll offset, pinch zoom and content size | 8.4, 7.2 pulley handoff | `Tab`, `GestureArbiter::set_content_edges` |
| Visible-rect update without resizing the surface | 6.3 | `WebView::set_viewport_rect` |
| IME delegate with input type, current text and selection | 6.3 | `InputMethodState` |
| Per-webview UA override | 7.2 desktop mode | `WebView::set_user_agent_override` |
| Download delegate with an embedder-chosen destination | 7.1 | `DownloadManager` |
| Delegate installable on auxiliary (window.open) webviews at creation | 4 | `EngineEvent::AuxiliaryWebView` |
| Proxy configuration on the builder or as a runtime call | 8.1 | `Engine::set_proxy`, `ProxyConfig::from_connman` |
| Clipboard delegate | 8.3 | `ClipboardObject` |
| Site-data clearing by origin and kind | 7.3 | `Engine::clear_site_data` |
| Media-session events | 8.2 (M3, MPRIS) | `Tab::media_session` |
| Request interception (stretch; the only route to network-level blocking) | 9.3 | not used yet |

## Compat bugs

File upstream with reduced test cases.  Track Servo's published WPT pass
rate; do not run WPT here (spec 13).
