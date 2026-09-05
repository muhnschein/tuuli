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
around locally beyond what the last column says.  The column is the M0
reconciliation of `servo/backend` against the `servo` crate at 0.5.0.

| Item | Spec | Used by | In 0.5.0 |
|---|---|---|---|
| A mobile-Linux `UserAgentPlatform` (or a runtime device-type query) so `is_mobile()` engages | 5.4 | `create_engine` | No: `Desktop`, `Android`, `OpenHarmony`, `Ios`.  On a mobile platform the backend sets the `user_agent` pref to Servo's Android string. |
| Context-menu hit test for an embedder-detected long-press | 6.2 | `WebViewItem` long press → `WebView::request_context_menu` | Partly: a `ContextMenu` embedder control arrives with the element's link and image details when the page raises `contextmenu`; the backend raises it by synthesising a right click at the long-press point.  No selected-text field. |
| Viewport delegate call with scroll offset, pinch zoom and content size | 8.4, 7.2 pulley handoff | `Tab`, `GestureArbiter::set_content_edges` | No.  `WebView::pinch_zoom()` can be polled; there is no scroll offset or content size, so the pulley handoff stays on the chrome's edge detection. |
| Visible-rect update without resizing the surface | 6.3 | `WebView::set_viewport_rect` | No; logged no-op. |
| IME delegate with input type, current text and selection | 6.3 | `InputMethodState` | Yes: `InputMethodControl` (type, text, insertion point, multiline, element rect) through `show_embedder_control` / `hide_embedder_control`. |
| Per-webview UA override | 7.2 desktop mode | `WebView::set_user_agent_override` | No: `user_agent` is one global pref; logged no-op. |
| Download delegate with an embedder-chosen destination | 7.1 | `DownloadManager` | No download API at all; `WebView::cancel_download` is a logged no-op and downloads are off. |
| Delegate installable on auxiliary (window.open) webviews at creation | 4 | `EngineEvent::AuxiliaryWebView` | Yes: `CreateNewWebViewRequest::builder()` returns a `WebViewBuilder`, so the delegate is set before `build()`. |
| Proxy configuration on the builder or as a runtime call | 8.1 | `Engine::set_proxy`, `ProxyConfig::from_connman` | As prefs: `network_http_proxy_uri`, `network_https_proxy_uri`, `network_http_no_proxy`, set at initialisation.  Whether a runtime `set_preference` reaches the open connection pool is to be verified on the device. |
| Clipboard delegate | 8.3 | `ClipboardObject` | Yes: `ClipboardDelegate` per webview (`WebViewBuilder::clipboard_delegate`).  Not wired yet: the backend leaves Servo's default, an in-process buffer with the `clipboard` feature off. |
| Site-data clearing by origin and kind | 7.3 | `Engine::clear_site_data` | Yes: `SiteDataManager::clear_site_data(sites, StorageType)`, `clear_cookies`, `NetworkManager::clear_cache`. |
| Media-session events | 8.2 (M3, MPRIS) | `Tab::media_session` | Yes: `notify_media_session_event` and `WebView::notify_media_session_action_event`. |
| Request interception (stretch; the only route to network-level blocking) | 9.3 | not used yet | Yes: `load_web_resource` with `WebResourceLoad`.  Not used. |
| Stop loading | 4 | `WebView::stop` | No `WebView::stop`; the backend evaluates `window.stop()` in the page. |
| Find in page | 6 | `WebView::find`, `find_next`, `find_clear` | No; the backend evaluates `window.find` in the page, which selects but does not highlight all matches. |
| Programmatic scroll | 7.2 | `WebView::scroll_to` | No; the backend evaluates `window.scrollTo`. |
| Private (in-memory) profiles | 7.3 | `Engine::create_webview(private)` | No per-webview storage partition: a private tab shares the profile's cookies and storage, and is private only on the browser's side (history, session, permissions, downloads).  The backend logs this when it creates one. |

## Compat bugs

File upstream with reduced test cases.  Track Servo's published WPT pass
rate; do not run WPT here (spec 13).
