# Architecture

Companion to [spec.md](spec.md) §4–§6; this documents how the tree
realises them and the decisions the spec left open.

## Layers

    QML (Silica)            src/qml/            import Tuuli 1.0
        |  properties / signals / invokables
    Tuuli plugin            src/plugin/         registers types, Browser singleton
        |
    libtuuli                src/lib/
        view/               TuuliWebView (QQuickFramebufferObject), WebViewRenderer,
                            QtRenderingContext, TuuliImageProvider
        input/              TouchConverter, GestureArbiter, InputMethodProxy, textdiff, cssgeometry
        model/              Tab, TabModel, SessionStore, HistoryModel, BookmarkModel,
                            PermissionStore, DownloadManager
        platform/           ConnmanProxy, TransferEngine, ClipboardBridge
        prefs/              Preferences, SearchEngines, UserAgent, servoprefs (name table)
        blocking/           CosmeticFilter
        perf/               PerfLog
        browsercontext      wiring, session/history/permission/download glue
        engine/             Engine + WebViewHandle + WebViewClient (the seam)
                              ServoEngine / ServoWebView   <- servo_capi.h (only these two include it)
                              MockEngine  / MockWebView    <- tests, host builds, UI iteration
        |
    libservo.so             servo/  (pinned tag, built out of tree)

`Tuuli::Engine` is the seam the spec asks for in §4.1: nothing above
`engine/` names a Servo type, so an out-of-process content model in M4 can
replace `ServoEngine` with an IPC proxy without touching the view, models
or QML.

## Threads

| Thread | Owns | Code |
|---|---|---|
| Qt GUI | QML, input, every engine call that mutates state, Servo's event loop | `TuuliWebView`, models, `ServoEngine::spinEventLoop` |
| Qt render | scene-graph GL context, FBO, engine init/teardown, paint | `WebViewRenderer`, `QtRenderingContext` |
| Servo internal | script, layout, style, net, image decode | libservo |

Rules, in code:

- `Engine::renderLock()` serialises `paint()` on the render thread against
  every GUI-thread engine call.  `ServoEngine` takes it inside each entry
  point; `WebViewRenderer::render()` takes it around `paint()`.
- Servo's `wake_up` callback can come from any Servo thread.  It only
  posts a `QEvent` to the engine object (`QCoreApplication::postEvent` is
  thread-safe); the GUI thread's event handler spins Servo's loop.
- Every other Servo callback is turned into a `QMetaObject::invokeMethod(…,
  Qt::QueuedConnection)` in a C trampoline before any Qt object is touched
  (`ServoWebView::Callbacks`).  This holds even though the callbacks are
  documented to arrive on the embedder thread; the spec asks for it and it
  costs one event-loop hop.
- No Qt object is created on a Servo thread.  `QTimer::singleShot` and
  friends are never used from trampolines: they need a Qt event
  dispatcher in the calling thread, which Rust threads do not have.

### Why the engine initialises on the render thread

`servo_init` compiles WebRender's shader set and needs a current GL
context, and libhybris drivers are strict about which thread a context is
current on (spec 5.3).  So the first `WebViewRenderer::render()` creates
the `QtRenderingContext` around the scene-graph `QOpenGLContext` and calls
`Engine::initializeOnRenderThread()`.  The engine posts `initialized()` to
the GUI thread, and `TabModel` materialises the current tab's webview only
then.  Until that moment tabs exist without engine webviews (they also do
after a context loss, and when the live-webview budget evicts them).

### Render-loop fallback

If M0 shows that paint-on-render-thread and event-loop-on-GUI-thread cannot
coexist on the hybris driver, the fallback is the basic (single-threaded)
scene-graph loop: `Settings → Developer → Single-threaded render loop`
sets `QSG_RENDER_LOOP=basic` at start-up, after which the "render thread"
*is* the GUI thread and the lock is uncontended.  No other code changes.

### Scene-graph invalidation

`TuuliWebView` sets the window's persistent-context and persistent-scene-
graph flags, so covering or sleeping the app normally keeps the context.
If Qt nevertheless invalidates (`WebViewRenderer` destroyed), the renderer
calls `shutdownOnRenderThread()`, which tears the whole engine down rather
than leaking GL objects (spec 5.2).  The engine emits
`renderContextLost()`; `TabModel` detaches every webview but keeps URLs,
titles, scroll and zoom; the next render re-initialises and the current tab
is re-created lazily, restoring its viewport after load.  Session restore
and context loss share this path deliberately.

## Rendering

`TuuliWebView` derives from `QQuickFramebufferObject` (spec 5.1).  The FBO
is created with a combined depth/stencil attachment (WebRender needs
depth).  `QtRenderingContext` implements the `servo_capi` rendering-context
vtable: `make_current` is a check that the scene-graph context is current
on the calling thread, `swap_buffers` is a no-op, `framebuffer_object`
returns the FBO Qt bound, `get_proc_address` goes to `QOpenGLContext`.  The
reported GL version is GLES 3.2 whenever the driver reports GLES ≥ 3.0
(spec 5.2).  After Servo paints, the renderer calls
`QQuickWindow::resetOpenGLState()`.

When the engine paints nothing (mock engine, engine not ready, init
failure) the renderer clears the FBO with the item's `placeholderColor`.

## Input

`TouchConverter` turns `QTouchEvent` points into `TouchPoint`s carrying both
device px (for edge zones) and CSS px (for the engine), dropping stationary
points and cancelling everything on `TouchCancel`.  DPR is not hard-coded:
`Css::deriveDevicePixelRatio` uses Qt's ratio when it is above 1 and
otherwise derives it from the panel's physical DPI (Android's dpi/160 rule,
rounded to 0.25), with a developer override.

`GestureArbiter` (spec 6.2) owns one touch sequence at a time:

| Start zone / condition | Outcome |
|---|---|
| left/right/top screen edge | `accepted = false`: event ignored, lipstick's gesture proceeds |
| bottom edge | toolbar reveal progress signals; nothing to the engine |
| content, single finger, no movement past slop for `longPressMs` | `longPressed`; engine gets touch cancel; rest of sequence stays with Tuuli |
| content, vertical drag when content is already at that edge | engine gets touch cancel, item releases its grab, the Silica flickable takes over → pulley menu |
| anything else | forwarded verbatim; Servo scrolls, flings, pinches, double-tap zooms |

The item keeps the touch/mouse grab (`setKeepTouchGrab`) for every
engine-owned sequence, which is what stops the enclosing `SilicaFlickable`
from stealing drags.

`InputMethodProxy` (spec 6.3) is the state object behind the hidden QML
`TextInput` Maliit attaches to.  The engine's IME requests set the hints
and enter-key type; each committed edit in the proxy is diffed against the
engine's text (`diffText`) and expressed as arrow keys, backspaces and a
committed composition (`planImeEdit`), because the engine has no
"set selection" entry point.  The view adjusts the engine's viewport rect
and scrolls the caret rect into view when the keyboard shows, without
resizing the surface.

## Models and persistence

- `SessionStore` writes atomically (`QSaveFile`) on a 5 s debounce, on
  every backgrounding and on `aboutToQuit`, with a `cleanExit` flag so a
  crash is detected on the next start (spec 8.4).  Private tabs are never
  written (spec 7.3).
- `TabModel` keeps at most `maxLiveWebViews` engine webviews alive,
  evicting least-recently-used non-current tabs; evicted tabs keep their
  state and are re-created on activation.
- History and bookmarks are SQLite via QtSql; private tabs never write
  history.
- `PermissionStore` is denied-by-default JSON per origin; private tabs read
  it but never write it.
- Downloads are performed by the engine; `DownloadManager` picks a unique
  destination and mirrors progress to Nemo Transfer Engine, except for
  private tabs.

## Paths

Sailjail permits `~/.local/share/<Org>/<App>` and siblings for the
`OrganizationName`/`ApplicationName` in the `.desktop` file.  With
`org.tuuli`/`browser` that is `~/.local/share/org.tuuli/browser/`, which is
what `QStandardPaths::AppDataLocation` returns once `main()` sets the
organisation and application names.  The spec's `~/.local/share/tuuli/`
would not be reachable inside the sandbox, so the sailjail-derived path is
used and the spec text should be read with that substitution.
