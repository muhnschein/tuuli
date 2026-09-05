// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! `tuuli_core::engine::Engine` over libservo's Rust embedding API
//! (spec 3.3, 4.2 as amended for Rust).
//!
//! This is written against the `servo` crate's embedding API as of the
//! pinned tag (`Servo`, `ServoBuilder`, `WebViewBuilder`, `WebViewDelegate`,
//! `RenderingContext`, `EventLoopWaker`).  It is compiled only by
//! `servo/app`, which is the M0 engine build; names that moved upstream
//! since this file was written are reconciled there, in this file only.
//!
//! Threading: everything runs on the Qt GUI thread with the basic render
//! loop.  Servo's waker is the one cross-thread entry point; it calls the
//! `Waker` the browser installed, which posts a queued callback that
//! spins the event loop.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::ffi::c_void;
use std::rc::{Rc, Weak};
use std::sync::Arc;

use euclid::{Point2D, Rect as EuclidRect, Scale, Size2D};
use tuuli_core::engine::*;
use tuuli_core::geometry::{Point, Rect, Size};
use tuuli_core::input::TouchPhase;
use tuuli_core::proxy::ProxyConfig;

use servo::{
    AllowOrDenyRequest, AuthenticationRequest, ContextMenuAction, EventLoopWaker, InputEvent, InputMethodType, ImeEvent,
    CompositionEvent, CompositionState as ServoCompositionState, Key, KeyState, KeyboardEvent, LoadStatus as ServoLoadStatus,
    NavigationRequest, PermissionRequest as ServoPermissionRequest, RenderingContext as ServoRenderingContext,
    Servo, ServoBuilder, ServoDelegate, ServoError, SimpleDialog, TouchEvent, TouchEventType, TouchId, WebView,
    WebViewBuilder, WebViewDelegate, WebResourceLoad, Modifiers as ServoModifiers, DevicePoint, DeviceRect, DeviceIntRect,
    PhysicalSize, Image as ServoImage, MediaSessionEvent as ServoMediaSessionEvent,
};

// ---- RenderingContext over the Qt FBO ----------------------------------------------

/// Wraps the browser's [`tuuli_core::engine::RenderingContext`] (the Qt
/// scene-graph context + the QQuickFramebufferObject FBO) as a Servo
/// rendering context.  Servo never owns a context (spec 5.2).
struct QtFboRenderingContext {
    inner: Rc<dyn RenderingContext>,
    gleam: Rc<dyn gleam::gl::Gl>,
    glow: Arc<glow::Context>,
    size: Cell<(u32, u32)>,
}

impl QtFboRenderingContext {
    fn new(inner: Rc<dyn RenderingContext>) -> Rc<Self> {
        let loader = {
            let ctx = inner.clone();
            move |name: &str| ctx.proc_address(name)
        };
        let gleam: Rc<dyn gleam::gl::Gl> = if inner.is_gles() {
            unsafe { gleam::gl::GlesFns::load_with(|s| loader(s)) }
        } else {
            unsafe { gleam::gl::GlFns::load_with(|s| loader(s)) }
        };
        let glow = unsafe { glow::Context::from_loader_function(|s| loader(s)) };
        let size = Cell::new(inner.size());
        Rc::new(Self { inner, gleam, glow: Arc::new(glow), size })
    }

    fn update_size(&self) {
        self.size.set(self.inner.size());
    }
}

impl ServoRenderingContext for QtFboRenderingContext {
    fn prepare_for_rendering(&self) {
        // Qt bound the FBO before render(); make it explicit for WebRender's
        // begin_frame, which captures the current binding as its target.
        self.gleam.bind_framebuffer(gleam::gl::FRAMEBUFFER, self.inner.framebuffer_object());
    }
    fn present(&self) {}
    fn make_current(&self) -> Result<(), ServoError> {
        // Qt made the context current on this thread; anything else is a
        // threading bug we want to hear about, not paper over.
        if self.inner.is_current() {
            Ok(())
        } else {
            Err(ServoError::MakeCurrentFailed)
        }
    }
    fn gleam_gl_api(&self) -> Rc<dyn gleam::gl::Gl> {
        self.gleam.clone()
    }
    fn glow_gl_api(&self) -> Arc<glow::Context> {
        self.glow.clone()
    }
    fn create_texture(&self, _image: ServoImage) -> Option<(u32, Size2D<i32>)> {
        None
    }
    fn destroy_texture(&self, _texture_id: u32) {}
    fn size(&self) -> PhysicalSize<u32> {
        let (w, h) = self.size.get();
        PhysicalSize::new(w.max(1), h.max(1))
    }
    fn resize(&self, _size: PhysicalSize<u32>) {
        // Qt owns the FBO size; nothing to do.
    }
    fn read_to_image(&self, rect: DeviceIntRect) -> Option<servo::RgbaImage> {
        let (w, h) = (rect.size().width, rect.size().height);
        if w <= 0 || h <= 0 {
            return None;
        }
        let mut data = vec![0u8; (w * h * 4) as usize];
        self.gleam.bind_framebuffer(gleam::gl::READ_FRAMEBUFFER, self.inner.framebuffer_object());
        self.gleam.read_pixels_into_buffer(rect.min.x, rect.min.y, w, h, gleam::gl::RGBA, gleam::gl::UNSIGNED_BYTE, &mut data);
        servo::RgbaImage::from_raw(w as u32, h as u32, data)
    }
}

// ---- Waker ------------------------------------------------------------------------

struct QtWaker(Waker);

impl EventLoopWaker for QtWaker {
    fn clone_box(&self) -> Box<dyn EventLoopWaker> {
        Box::new(QtWaker(self.0.clone()))
    }
    fn wake(&self) {
        (self.0)();
    }
}

// ---- Engine -------------------------------------------------------------------------

struct EngineInner {
    servo: Option<Servo>,
    context: Option<Rc<QtFboRenderingContext>>,
}

pub struct ServoEngine {
    config: RefCell<EngineConfig>,
    inner: RefCell<EngineInner>,
    sink: RefCell<Option<EngineEventSink>>,
    waker: RefCell<Option<Waker>>,
    weak_self: RefCell<Weak<ServoEngine>>,
    downloads: RefCell<u64>,
}

impl ServoEngine {
    pub fn new() -> Rc<ServoEngine> {
        let e = Rc::new(ServoEngine {
            config: RefCell::new(EngineConfig::default()),
            inner: RefCell::new(EngineInner { servo: None, context: None }),
            sink: RefCell::new(None),
            waker: RefCell::new(None),
            weak_self: RefCell::new(Weak::new()),
            downloads: RefCell::new(1),
        });
        *e.weak_self.borrow_mut() = Rc::downgrade(&e);
        e
    }

    fn emit(&self, ev: EngineEvent) {
        if let Some(sink) = self.sink.borrow().clone() {
            sink(ev);
        }
    }

    fn next_download_id(&self) -> u64 {
        let mut n = self.downloads.borrow_mut();
        let id = *n;
        *n += 1;
        id
    }
}

/// Engine factory for `servo/app`.
pub fn create_engine() -> Rc<dyn Engine> {
    ServoEngine::new()
}

struct TuuliServoDelegate {
    engine: Weak<ServoEngine>,
}

impl ServoDelegate for TuuliServoDelegate {
    fn notify_error(&self, _servo: &Servo, error: ServoError) {
        if let Some(e) = self.engine.upgrade() {
            e.emit(EngineEvent::Crashed { reason: format!("{error:?}"), backtrace: String::new() });
        }
    }
}

impl Engine for ServoEngine {
    fn name(&self) -> &'static str {
        "servo"
    }
    fn version(&self) -> String {
        servo::VERSION.to_string()
    }
    fn configure(&self, config: EngineConfig) {
        *self.config.borrow_mut() = config;
    }
    fn config(&self) -> EngineConfig {
        self.config.borrow().clone()
    }

    fn initialize(&self, ctx: Rc<dyn RenderingContext>) -> Result<(), String> {
        if self.inner.borrow().servo.is_some() {
            return Ok(());
        }
        let cfg = self.config.borrow().clone();
        let context = QtFboRenderingContext::new(ctx);
        context.make_current().map_err(|e| format!("rendering context not current: {e:?}"))?;

        let waker: Box<dyn EventLoopWaker> = match self.waker.borrow().clone() {
            Some(w) => Box::new(QtWaker(w)),
            None => return Err("engine waker not installed".into()),
        };

        // Spec 9.4 / 8.1: prefs and CA bundle from the browser config.
        let mut prefs = servo::Preferences::default();
        for (name, value) in &cfg.prefs {
            prefs.set_value(name, servo::PrefValue::from_str_preserving_type(value));
        }
        let mut opts = servo::Opts::default();
        if let Some(ca) = &cfg.certificate_path {
            opts.certificate_path = Some(ca.to_string_lossy().to_string());
        }
        opts.config_dir = Some(cfg.data_dir.clone());
        opts.ignore_certificate_errors = false;
        let ua = if cfg.user_agent.is_empty() { None } else { Some(cfg.user_agent.clone()) };

        let servo = ServoBuilder::new(context.clone())
            .opts(opts)
            .preferences(prefs)
            .event_loop_waker(waker)
            .user_agent(ua)
            .build();
        servo.set_delegate(Rc::new(TuuliServoDelegate { engine: self.weak_self.borrow().clone() }));

        let mut inner = self.inner.borrow_mut();
        inner.servo = Some(servo);
        inner.context = Some(context);
        drop(inner);
        self.emit(EngineEvent::Initialized);
        Ok(())
    }

    fn is_initialized(&self) -> bool {
        self.inner.borrow().servo.is_some()
    }

    fn shutdown(&self) {
        let mut inner = self.inner.borrow_mut();
        if let Some(servo) = inner.servo.take() {
            servo.deinit();
        }
        inner.context = None;
        drop(inner);
        self.emit(EngineEvent::ShutDown);
    }

    fn create_webview(&self, sink: EventSink, private: bool, dpr: f64, size: (u32, u32)) -> Option<Rc<dyn WebView>> {
        let inner = self.inner.borrow();
        let servo = inner.servo.as_ref()?;
        let context = inner.context.as_ref()?.clone();
        let delegate = Rc::new(TuuliWebViewDelegate {
            engine: self.weak_self.borrow().clone(),
            sink: RefCell::new(Some(sink)),
            downloads: RefCell::new(HashMap::new()),
        });
        let mut builder = WebViewBuilder::new(servo, context)
            .delegate(delegate.clone())
            .hidpi_scale_factor(Scale::new(dpr as f32))
            .size(PhysicalSize::new(size.0.max(1), size.1.max(1)));
        if private {
            builder = builder.private_browsing(true);
        }
        let webview = builder.build();
        Some(Rc::new(ServoWebView { webview: RefCell::new(Some(webview)), delegate, private, closed: Cell::new(false) }))
    }

    fn spin_event_loop(&self) {
        let inner = self.inner.borrow();
        if let Some(servo) = inner.servo.as_ref() {
            servo.spin_event_loop();
        }
    }

    fn set_pref(&self, name: &str, value: &str) {
        let inner = self.inner.borrow();
        if let Some(servo) = inner.servo.as_ref() {
            servo.preferences().set_value(name, servo::PrefValue::from_str_preserving_type(value));
        }
    }

    fn set_proxy(&self, proxy: &ProxyConfig) {
        // libservo has no proxy API at this tag (docs/UPSTREAM.md); the
        // network stack honours the environment, set before init.
        if !proxy.http.is_empty() {
            std::env::set_var("http_proxy", format!("http://{}", proxy.http));
        }
        if !proxy.https.is_empty() {
            std::env::set_var("https_proxy", format!("http://{}", proxy.https));
        }
        if !proxy.no_proxy.is_empty() {
            std::env::set_var("no_proxy", proxy.no_proxy.join(","));
        }
    }

    fn clear_site_data(&self, origin: Option<&str>, kinds: u32) {
        let inner = self.inner.borrow();
        let Some(servo) = inner.servo.as_ref() else { return };
        let manager = servo.site_data_manager();
        match origin {
            Some(o) => {
                if let Ok(url) = url::Url::parse(o) {
                    if kinds & site_data::COOKIES != 0 {
                        manager.clear_cookies_for_origin(&url);
                    }
                    if kinds & (site_data::LOCAL_STORAGE | site_data::SESSION_STORAGE) != 0 {
                        manager.clear_storage_for_origin(&url);
                    }
                }
            }
            None => {
                if kinds & site_data::COOKIES != 0 {
                    manager.clear_all_cookies();
                }
                if kinds & (site_data::LOCAL_STORAGE | site_data::SESSION_STORAGE) != 0 {
                    manager.clear_all_storage();
                }
                if kinds & site_data::HTTP_CACHE != 0 {
                    manager.clear_http_cache();
                }
            }
        }
    }

    fn set_event_sink(&self, sink: EngineEventSink) {
        *self.sink.borrow_mut() = Some(sink);
    }
    fn set_waker(&self, waker: Waker) {
        *self.waker.borrow_mut() = Some(waker);
    }
}

// ---- WebView --------------------------------------------------------------------------

struct TuuliWebViewDelegate {
    engine: Weak<ServoEngine>,
    sink: RefCell<Option<EventSink>>,
    /// Tuuli download id -> Servo download handle.
    downloads: RefCell<HashMap<u64, servo::Download>>,
}

impl TuuliWebViewDelegate {
    fn emit(&self, ev: WebViewEvent) {
        if let Some(sink) = self.sink.borrow().clone() {
            sink(ev);
        }
    }
}

fn to_input_type(t: InputMethodType) -> InputType {
    match t {
        InputMethodType::Text => InputType::Text,
        InputMethodType::Url => InputType::Url,
        InputMethodType::Email => InputType::Email,
        InputMethodType::Number => InputType::Number,
        InputMethodType::Password => InputType::Password,
        InputMethodType::Tel => InputType::Tel,
        InputMethodType::Search => InputType::Search,
        InputMethodType::Date => InputType::Date,
        InputMethodType::Time => InputType::Time,
        InputMethodType::DatetimeLocal => InputType::DateTime,
        InputMethodType::Month => InputType::Month,
        InputMethodType::Week => InputType::Week,
        InputMethodType::Color => InputType::Color,
        _ => InputType::None,
    }
}

impl WebViewDelegate for TuuliWebViewDelegate {
    fn notify_url_changed(&self, _webview: WebView, url: url::Url) {
        self.emit(WebViewEvent::UrlChanged(url.to_string()));
    }
    fn notify_page_title_changed(&self, _webview: WebView, title: Option<String>) {
        self.emit(WebViewEvent::TitleChanged(title.unwrap_or_default()));
    }
    fn notify_load_status_changed(&self, _webview: WebView, status: ServoLoadStatus) {
        self.emit(WebViewEvent::LoadStatus(match status {
            ServoLoadStatus::Started => LoadStatus::Started,
            ServoLoadStatus::HeadParsed => LoadStatus::HeadParsed,
            ServoLoadStatus::Complete => LoadStatus::Complete,
        }));
    }
    fn notify_favicon_changed(&self, _webview: WebView, favicon: ServoImage) {
        let (w, h) = (favicon.width, favicon.height);
        if let Some(data) = favicon.as_rgba8() {
            self.emit(WebViewEvent::Favicon(RgbaImage { width: w, height: h, data: data.to_vec() }));
        }
    }
    fn notify_history_changed(&self, _webview: WebView, entries: Vec<url::Url>, current: usize) {
        self.emit(WebViewEvent::History { can_go_back: current > 0, can_go_forward: current + 1 < entries.len() });
    }
    fn notify_new_frame_ready(&self, _webview: WebView) {
        self.emit(WebViewEvent::FrameReady);
    }
    fn notify_scroll_changed(&self, webview: WebView, scroll: DevicePoint, zoom: f32, content: PhysicalSize<f32>) {
        let dpr = webview.hidpi_scale_factor().get() as f64;
        self.emit(WebViewEvent::Viewport {
            scroll: Point::new(scroll.x as f64 / dpr, scroll.y as f64 / dpr),
            zoom: zoom as f64,
            content: Size::new(content.width as f64 / dpr, content.height as f64 / dpr),
        });
    }
    fn show_ime(&self, webview: WebView, input_type: InputMethodType, text: Option<(String, i32)>, multiline: bool, position: DeviceIntRect) {
        let dpr = webview.hidpi_scale_factor().get() as f64;
        let (text, _cursor) = text.unwrap_or_default();
        self.emit(WebViewEvent::ImeShow {
            input_type: to_input_type(input_type),
            text,
            multiline,
            cursor_rect: Rect::new(
                position.min.x as f64 / dpr,
                position.min.y as f64 / dpr,
                position.size().width as f64 / dpr,
                position.size().height as f64 / dpr,
            ),
        });
    }
    fn hide_ime(&self, _webview: WebView) {
        self.emit(WebViewEvent::ImeHide);
    }
    fn request_permission(&self, _webview: WebView, request: ServoPermissionRequest) {
        let kind = match request.feature() {
            servo::PermissionFeature::Geolocation => PermissionKind::Geolocation,
            servo::PermissionFeature::Notifications => PermissionKind::Notifications,
            servo::PermissionFeature::Camera => PermissionKind::Camera,
            servo::PermissionFeature::Microphone => PermissionKind::Microphone,
            servo::PermissionFeature::PersistentStorage => PermissionKind::PersistentStorage,
            servo::PermissionFeature::Midi => PermissionKind::Midi,
            servo::PermissionFeature::Bluetooth => PermissionKind::Bluetooth,
            servo::PermissionFeature::ClipboardRead => PermissionKind::ClipboardRead,
            servo::PermissionFeature::ClipboardWrite => PermissionKind::ClipboardWrite,
            _ => PermissionKind::PersistentStorage,
        };
        let origin = request.origin().to_string();
        self.emit(WebViewEvent::Permission(PermissionRequest::new(kind, origin, move |ok| {
            if ok {
                request.allow();
            } else {
                request.deny();
            }
        })));
    }
    fn request_navigation(&self, _webview: WebView, request: NavigationRequest) {
        // Tuuli does not filter navigations (no network-level blocking, spec 9.3).
        request.allow();
    }
    fn request_authentication(&self, _webview: WebView, request: AuthenticationRequest) {
        request.cancel();
    }
    fn show_simple_dialog(&self, _webview: WebView, dialog: SimpleDialog) {
        let (kind, message, default) = match &dialog {
            SimpleDialog::Alert { message, .. } => (DialogKind::Alert, message.clone(), String::new()),
            SimpleDialog::Confirm { message, .. } => (DialogKind::Confirm, message.clone(), String::new()),
            SimpleDialog::Prompt { message, default, .. } => (DialogKind::Prompt, message.clone(), default.clone()),
        };
        self.emit(WebViewEvent::Dialog(DialogRequest::new(kind, message, default, move |answer| match (dialog, answer) {
            (SimpleDialog::Alert { response_sender, .. }, _) => {
                let _ = response_sender.send(());
            }
            (SimpleDialog::Confirm { response_sender, .. }, a) => {
                let _ = response_sender.send(if a.is_some() { servo::AlertResponse::Ok } else { servo::AlertResponse::Cancel });
            }
            (SimpleDialog::Prompt { response_sender, .. }, a) => {
                let _ = response_sender.send(match a {
                    Some(v) => servo::PromptResponse::Ok(v),
                    None => servo::PromptResponse::Cancel,
                });
            }
        })));
    }
    fn show_context_menu(&self, webview: WebView, result: servo::ContextMenuResult, position: DevicePoint) {
        let dpr = webview.hidpi_scale_factor().get() as f64;
        let info = ContextMenuInfo {
            css: Point::new(position.x as f64 / dpr, position.y as f64 / dpr),
            link_url: result.link_url.map(|u| u.to_string()),
            image_url: result.image_url.map(|u| u.to_string()),
            selected_text: result.selected_text.unwrap_or_default(),
            editable: result.is_editable,
        };
        result.dismiss(ContextMenuAction::Dismissed);
        self.emit(WebViewEvent::ContextMenu(info));
    }
    fn request_download(&self, _webview: WebView, download: servo::Download) {
        let Some(engine) = self.engine.upgrade() else { return };
        let id = engine.next_download_id();
        let url = download.url().to_string();
        let name = download.suggested_filename().unwrap_or_default();
        let mime = download.mime_type().unwrap_or_default();
        let total = download.total_bytes().map(|b| b as i64).unwrap_or(-1);
        self.downloads.borrow_mut().insert(id, download);
        let sink = self.sink.borrow().clone();
        let downloads = Rc::downgrade(&Rc::new(()));
        let _ = downloads;
        let delegate_downloads: *const RefCell<HashMap<u64, servo::Download>> = &self.downloads;
        self.emit(WebViewEvent::DownloadRequested(DownloadRequest::new(id, url, name, mime, total, move |dest| {
            // SAFETY: the delegate outlives every request it emits; requests
            // are answered on the GUI thread while the webview is alive.
            let downloads = unsafe { &*delegate_downloads };
            let Some(handle) = downloads.borrow_mut().remove(&id) else { return };
            match dest {
                Some(path) => {
                    let sink = sink.clone();
                    handle.accept_to(path, move |progress| {
                        if let Some(sink) = &sink {
                            match progress {
                                servo::DownloadProgress::Progress { received, total } => {
                                    sink(WebViewEvent::DownloadProgress { id, received: received as i64, total: total.map(|t| t as i64).unwrap_or(-1) })
                                }
                                servo::DownloadProgress::Finished => sink(WebViewEvent::DownloadFinished { id, ok: true, error: String::new() }),
                                servo::DownloadProgress::Failed(e) => sink(WebViewEvent::DownloadFinished { id, ok: false, error: e }),
                            }
                        }
                    });
                }
                None => handle.reject(),
            }
        })));
    }
    fn notify_media_session_event(&self, _webview: WebView, event: ServoMediaSessionEvent) {
        let info = match event {
            ServoMediaSessionEvent::SetMetadata(m) => MediaSessionInfo {
                event: MediaSessionEvent::Metadata,
                title: m.title,
                artist: m.artist,
                album: m.album,
                position_seconds: 0.0,
                duration_seconds: 0.0,
            },
            ServoMediaSessionEvent::PlaybackStateChange(state) => MediaSessionInfo {
                event: match state {
                    servo::MediaSessionPlaybackState::Playing => MediaSessionEvent::Playing,
                    servo::MediaSessionPlaybackState::Paused => MediaSessionEvent::Paused,
                    _ => MediaSessionEvent::None,
                },
                title: String::new(),
                artist: String::new(),
                album: String::new(),
                position_seconds: 0.0,
                duration_seconds: 0.0,
            },
            ServoMediaSessionEvent::SetPositionState(p) => MediaSessionInfo {
                event: MediaSessionEvent::Position,
                title: String::new(),
                artist: String::new(),
                album: String::new(),
                position_seconds: p.position,
                duration_seconds: p.duration,
            },
        };
        self.emit(WebViewEvent::MediaSession(info));
    }
    fn show_notification(&self, _webview: WebView, notification: servo::Notification) {
        self.emit(WebViewEvent::Notification { title: notification.title, body: notification.body, icon: notification.icon_url.map(|u| u.to_string()) });
    }
    fn request_create_new(&self, _parent: WebView, _builder: WebViewBuilder) -> Option<WebView> {
        // Handled as a new tab by the browser; the URL arrives via the
        // navigation of the new view.  See docs/UPSTREAM.md.
        None
    }
    fn request_open_new_tab(&self, _webview: WebView, url: url::Url) {
        self.emit(WebViewEvent::NewWebViewRequested { url: Some(url.to_string()) });
    }
    fn notify_closed(&self, _webview: WebView) {
        self.emit(WebViewEvent::Closed);
    }
    fn load_web_resource(&self, _webview: WebView, load: WebResourceLoad) {
        // No interception (spec 9.3); let it through.
        drop(load);
    }
}

pub struct ServoWebView {
    webview: RefCell<Option<WebView>>,
    delegate: Rc<TuuliWebViewDelegate>,
    private: bool,
    closed: Cell<bool>,
}

impl ServoWebView {
    fn with<R>(&self, f: impl FnOnce(&WebView) -> R) -> Option<R> {
        if self.closed.get() {
            return None;
        }
        self.webview.borrow().as_ref().map(f)
    }
}

fn to_servo_key(key: &str) -> Key {
    match key {
        "Enter" => Key::Named(servo::NamedKey::Enter),
        "Backspace" => Key::Named(servo::NamedKey::Backspace),
        "Delete" => Key::Named(servo::NamedKey::Delete),
        "Tab" => Key::Named(servo::NamedKey::Tab),
        "Escape" => Key::Named(servo::NamedKey::Escape),
        "ArrowLeft" => Key::Named(servo::NamedKey::ArrowLeft),
        "ArrowRight" => Key::Named(servo::NamedKey::ArrowRight),
        "ArrowUp" => Key::Named(servo::NamedKey::ArrowUp),
        "ArrowDown" => Key::Named(servo::NamedKey::ArrowDown),
        "Home" => Key::Named(servo::NamedKey::Home),
        "End" => Key::Named(servo::NamedKey::End),
        "PageUp" => Key::Named(servo::NamedKey::PageUp),
        "PageDown" => Key::Named(servo::NamedKey::PageDown),
        "Shift" => Key::Named(servo::NamedKey::Shift),
        "Control" => Key::Named(servo::NamedKey::Control),
        "Alt" => Key::Named(servo::NamedKey::Alt),
        "Meta" => Key::Named(servo::NamedKey::Meta),
        "Unidentified" => Key::Named(servo::NamedKey::Unidentified),
        other => Key::Character(other.to_string()),
    }
}

impl tuuli_core::engine::WebView for ServoWebView {
    fn is_private(&self) -> bool {
        self.private
    }
    fn set_client(&self, sink: EventSink) {
        *self.delegate.sink.borrow_mut() = Some(sink);
    }
    fn load(&self, url: &str) {
        if let Ok(u) = url::Url::parse(url) {
            self.with(|w| w.load(u));
        }
    }
    fn reload(&self) {
        self.with(|w| w.reload());
    }
    fn stop(&self) {
        self.with(|w| w.stop());
    }
    fn go_back(&self) {
        self.with(|w| w.go_back(1));
    }
    fn go_forward(&self) {
        self.with(|w| w.go_forward(1));
    }
    fn set_visible(&self, visible: bool) {
        self.with(|w| if visible { w.show(true) } else { w.hide() });
    }
    fn set_focused(&self, focused: bool) {
        self.with(|w| if focused { w.focus() } else { w.blur() });
    }
    fn set_size(&self, width: u32, height: u32) {
        self.delegate.engine.upgrade().and_then(|e| e.inner.borrow().context.clone()).map(|c| c.update_size());
        self.with(|w| w.resize(PhysicalSize::new(width.max(1), height.max(1))));
    }
    fn set_viewport_rect(&self, rect: Rect) {
        self.with(|w| {
            w.move_resize(DeviceRect::from_origin_and_size(
                Point2D::new(rect.x as f32, rect.y as f32),
                Size2D::new(rect.width.max(1.0) as f32, rect.height.max(1.0) as f32),
            ))
        });
    }
    fn set_device_pixel_ratio(&self, dpr: f64) {
        self.with(|w| w.set_hidpi_scale_factor(Scale::new(dpr as f32)));
    }
    fn set_pinch_zoom(&self, zoom: f64) {
        self.with(|w| w.set_pinch_zoom(zoom as f32));
    }
    fn set_page_zoom(&self, zoom: f64) {
        self.with(|w| w.set_zoom(zoom as f32));
    }
    fn scroll_to(&self, css: Point) {
        self.with(|w| {
            let dpr = w.hidpi_scale_factor().get();
            w.scroll_to(DevicePoint::new(css.x as f32 * dpr, css.y as f32 * dpr));
        });
    }
    fn touch(&self, phase: TouchPhase, id: i32, css: Point) {
        self.with(|w| {
            let dpr = w.hidpi_scale_factor().get();
            let ty = match phase {
                TouchPhase::Down => TouchEventType::Down,
                TouchPhase::Move => TouchEventType::Move,
                TouchPhase::Up => TouchEventType::Up,
                TouchPhase::Cancel => TouchEventType::Cancel,
            };
            w.notify_input_event(InputEvent::Touch(TouchEvent::new(ty, TouchId(id), DevicePoint::new(css.x as f32 * dpr, css.y as f32 * dpr))));
        });
    }
    fn key(&self, down: bool, key: &str, modifiers: u32) {
        self.with(|w| {
            let mut m = ServoModifiers::empty();
            if modifiers & 1 != 0 {
                m |= ServoModifiers::SHIFT;
            }
            if modifiers & 2 != 0 {
                m |= ServoModifiers::CONTROL;
            }
            if modifiers & 4 != 0 {
                m |= ServoModifiers::ALT;
            }
            if modifiers & 8 != 0 {
                m |= ServoModifiers::META;
            }
            let mut ev = KeyboardEvent::default();
            ev.event.state = if down { KeyState::Down } else { KeyState::Up };
            ev.event.key = to_servo_key(key);
            ev.event.modifiers = m;
            w.notify_input_event(InputEvent::Keyboard(ev));
        });
    }
    fn ime_composition(&self, state: CompositionState, text: &str) {
        self.with(|w| {
            let state = match state {
                CompositionState::Start => ServoCompositionState::Start,
                CompositionState::Update => ServoCompositionState::Update,
                CompositionState::End => ServoCompositionState::End,
            };
            w.notify_input_event(InputEvent::Ime(ImeEvent::Composition(CompositionEvent { state, data: text.to_string() })));
        });
    }
    fn ime_dismissed(&self) {
        self.with(|w| w.notify_input_event(InputEvent::Ime(ImeEvent::Dismissed)));
    }
    fn editing_action(&self, action: EditingAction) {
        self.with(|w| {
            let a = match action {
                EditingAction::Copy => servo::EditingActionEvent::Copy,
                EditingAction::Cut => servo::EditingActionEvent::Cut,
                EditingAction::Paste => servo::EditingActionEvent::Paste,
                EditingAction::SelectAll => servo::EditingActionEvent::SelectAll,
            };
            w.notify_input_event(InputEvent::EditingAction(a));
        });
    }
    fn request_context_menu(&self, css: Point) {
        // Servo's context-menu hit test is driven by its own input pipeline
        // (contextmenu event); the browser detects the long-press and asks
        // for the result here.  See docs/UPSTREAM.md for the capi item.
        self.with(|w| {
            let dpr = w.hidpi_scale_factor().get();
            w.request_context_menu_at(DevicePoint::new(css.x as f32 * dpr, css.y as f32 * dpr));
        });
    }
    fn find(&self, text: &str, case_sensitive: bool) {
        self.with(|w| w.find(text, case_sensitive));
    }
    fn find_next(&self, forward: bool) {
        self.with(|w| w.find_next(forward));
    }
    fn find_clear(&self) {
        self.with(|w| w.find_clear());
    }
    fn add_user_stylesheet(&self, id: &str, css: &str) {
        self.with(|w| w.add_user_stylesheet(id, css));
    }
    fn remove_user_stylesheet(&self, id: &str) {
        self.with(|w| w.remove_user_stylesheet(id));
    }
    fn set_user_agent_override(&self, ua: Option<&str>) {
        self.with(|w| w.set_user_agent(ua.map(|s| s.to_string())));
    }
    fn evaluate_javascript(&self, script: &str) {
        self.with(|w| w.evaluate_javascript(script, |_| {}));
    }
    fn capture(&self) -> Option<RgbaImage> {
        self.with(|w| w.paint_to_image().map(|img| RgbaImage { width: img.width(), height: img.height(), data: img.into_raw() })).flatten()
    }
    fn cancel_download(&self, id: u64) {
        if let Some(handle) = self.delegate.downloads.borrow_mut().remove(&id) {
            handle.cancel();
        }
    }
    fn paint(&self) -> bool {
        if let Some(engine) = self.delegate.engine.upgrade() {
            if let Some(ctx) = engine.inner.borrow().context.as_ref() {
                ctx.update_size();
            }
        }
        self.with(|w| w.paint()).unwrap_or(false)
    }
    fn close(&self) {
        if self.closed.replace(true) {
            return;
        }
        *self.delegate.sink.borrow_mut() = None;
        if let Some(w) = self.webview.borrow_mut().take() {
            drop(w);
        }
    }
}

// Keep the euclid rect type referenced for readers reconciling the API.
#[allow(dead_code)]
type _DeviceRectAlias = EuclidRect<f32, servo::DevicePixel>;
#[allow(dead_code)]
fn _unused(_: *const c_void) {}
