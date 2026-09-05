// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tuuli's engine backend over the `servo` crate at the pinned tag
//! (`../SERVO_TAG`): [`tuuli_core::engine::Engine`] and
//! [`tuuli_core::engine::WebView`] implemented with `ServoBuilder`,
//! `WebViewBuilder`, the `WebViewDelegate` and the embedder controls.
//!
//! Written against Servo 0.5.0.  Where that API has no counterpart for a
//! method of the seam, the method is a logged no-op and the gap is listed
//! in `docs/UPSTREAM.md`; nothing is worked around by reaching past the
//! public API.  Everything here runs on the Qt GUI thread
//! (`docs/ARCHITECTURE.md`, Threads), which is the only thread that ever
//! touches `Servo`, a `WebView` or a delegate.

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::rc::{Rc, Weak};
use std::sync::Arc;

use euclid::{Point2D, Scale};
use tuuli_core::engine::WebView as CoreWebView;
use tuuli_core::engine::*;
use tuuli_core::geometry::{Point, Rect};
use tuuli_core::input::TouchPhase;
use tuuli_core::proxy::ProxyConfig;

use servo::user_contents::UserStyleSheet;
use servo::{
    AuthenticationRequest, CompositionEvent, CompositionState as ServoCompositionState,
    ContextMenu, DeviceIntPoint, DeviceIntRect, DeviceIntSize, DevicePoint, EditingActionEvent,
    EmbedderControl, EmbedderControlId, EventLoopWaker, ImeEvent, InputEvent, InputMethodType, Key,
    KeyState, KeyboardEvent, LoadStatus as ServoLoadStatus,
    MediaSessionEvent as ServoMediaSessionEvent, MediaSessionPlaybackState,
    Modifiers as ServoModifiers, MouseButton, MouseButtonAction, MouseButtonEvent, NamedKey,
    NavigationRequest, PermissionFeature, PermissionRequest as ServoPermissionRequest, PixelFormat,
    PrefValue, RenderingContext as ServoRenderingContext, ScreenGeometry, Servo, ServoBuilder,
    ServoDelegate, ServoError, SimpleDialog, StorageType, TouchEvent, TouchEventType, TouchId,
    TouchPointerType, UserAgentPlatform, UserContentManager, WebView, WebViewBuilder,
    WebViewDelegate, WebViewPoint,
};

/// The pinned Servo release, from `servo/SERVO_TAG`.
const SERVO_TAG: &str = include_str!("../../SERVO_TAG");

// ---- RenderingContext over the Qt FBO -------------------------------------------

/// Wraps the browser's [`tuuli_core::engine::RenderingContext`] (the Qt
/// scene-graph context + the QQuickFramebufferObject FBO) as a Servo
/// rendering context.  Servo never owns a context (spec 5.2): `make_current`
/// is a check, `present` is a no-op, and painting goes into whatever FBO
/// Qt bound for this frame.
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
        Rc::new(Self {
            inner,
            gleam,
            glow: Arc::new(glow),
            size,
        })
    }

    /// Qt owns the FBO size; refresh what we report before each paint.
    fn update_size(&self) {
        self.size.set(self.inner.size());
    }

    fn device_size(&self) -> DeviceIntSize {
        let (w, h) = self.size.get();
        DeviceIntSize::new(w.max(1) as i32, h.max(1) as i32)
    }
}

impl ServoRenderingContext for QtFboRenderingContext {
    fn prepare_for_rendering(&self) {
        // Qt bound the FBO before render(); make it explicit for WebRender,
        // which captures the current binding as its target.
        self.gleam
            .bind_framebuffer(gleam::gl::FRAMEBUFFER, self.inner.framebuffer_object());
    }

    fn read_to_image(&self, rect: DeviceIntRect) -> Option<servo::RgbaImage> {
        let (w, h) = (rect.size().width, rect.size().height);
        if w <= 0 || h <= 0 || !self.inner.is_current() {
            return None;
        }
        let mut data = vec![0u8; (w * h * 4) as usize];
        self.gleam
            .bind_framebuffer(gleam::gl::READ_FRAMEBUFFER, self.inner.framebuffer_object());
        self.gleam.read_pixels_into_buffer(
            rect.min.x,
            rect.min.y,
            w,
            h,
            gleam::gl::RGBA,
            gleam::gl::UNSIGNED_BYTE,
            &mut data,
        );
        servo::RgbaImage::from_raw(w as u32, h as u32, data)
    }

    fn size(&self) -> dpi::PhysicalSize<u32> {
        let (w, h) = self.size.get();
        dpi::PhysicalSize::new(w.max(1), h.max(1))
    }

    fn resize(&self, _size: dpi::PhysicalSize<u32>) {
        // Qt owns the FBO size; `update_size` reads it back.
    }

    fn present(&self) {}

    fn make_current(&self) -> Result<(), surfman::Error> {
        // Qt made the context current on this thread before render(); a
        // paint from anywhere else is a threading bug we log rather than
        // paper over, since the trait's error type is surfman's.
        if !self.inner.is_current() {
            log::error!("Servo asked for the GL context off the render path");
        }
        Ok(())
    }

    fn gleam_gl_api(&self) -> Rc<dyn gleam::gl::Gl> {
        self.gleam.clone()
    }

    fn glow_gl_api(&self) -> Arc<glow::Context> {
        self.glow.clone()
    }
}

// ---- Waker ----------------------------------------------------------------------

/// Servo's cross-thread wake-up, forwarded to the browser's waker (a queued
/// callback onto the Qt GUI thread that spins the event loop).
struct QtWaker(Waker);

impl EventLoopWaker for QtWaker {
    fn clone_box(&self) -> Box<dyn EventLoopWaker> {
        Box::new(QtWaker(self.0.clone()))
    }
    fn wake(&self) {
        (self.0)();
    }
}

// ---- Engine ----------------------------------------------------------------------

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
}

impl ServoEngine {
    pub fn new() -> Rc<ServoEngine> {
        let e = Rc::new(ServoEngine {
            config: RefCell::new(EngineConfig::default()),
            inner: RefCell::new(EngineInner {
                servo: None,
                context: None,
            }),
            sink: RefCell::new(None),
            waker: RefCell::new(None),
            weak_self: RefCell::new(Weak::new()),
        });
        *e.weak_self.borrow_mut() = Rc::downgrade(&e);
        e
    }

    fn emit(&self, ev: EngineEvent) {
        if let Some(sink) = self.sink.borrow().clone() {
            sink(ev);
        }
    }

    fn context(&self) -> Option<Rc<QtFboRenderingContext>> {
        self.inner.borrow().context.clone()
    }

    /// Wraps a built Servo webview for the core, sharing the engine's
    /// context and this delegate.
    fn wrap(
        &self,
        webview: WebView,
        delegate: Rc<TuuliWebViewDelegate>,
        private: bool,
    ) -> Rc<ServoWebView> {
        let user_content = webview.user_content_manager();
        Rc::new(ServoWebView {
            webview: RefCell::new(Some(webview)),
            delegate,
            private,
            closed: Cell::new(false),
            user_content,
            stylesheets: RefCell::new(HashMap::new()),
        })
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
    fn notify_error(&self, error: ServoError) {
        if let Some(e) = self.engine.upgrade() {
            e.emit(EngineEvent::Crashed {
                reason: format!("{error:?}"),
                backtrace: String::new(),
            });
        }
    }
    fn show_console_message(&self, level: servo::ConsoleLogLevel, message: String) {
        log::debug!("console [{level:?}]: {message}");
    }
}

fn pref_value(value: &str) -> PrefValue {
    PrefValue::from_booleanish_str(value)
}

impl Engine for ServoEngine {
    fn name(&self) -> &'static str {
        "servo"
    }
    fn version(&self) -> String {
        SERVO_TAG.trim().to_string()
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
        if !ctx.is_current() {
            return Err("rendering context not current on the GUI thread".into());
        }
        let context = QtFboRenderingContext::new(ctx);

        let waker: Box<dyn EventLoopWaker> = match self.waker.borrow().clone() {
            Some(w) => Box::new(QtWaker(w)),
            None => return Err("engine waker not installed".into()),
        };

        // Spec 9.4 / 8.1: prefs and CA bundle from the browser config.
        // Spec 5.4: there is no mobile-Linux UserAgentPlatform yet
        // (docs/UPSTREAM.md); Android's string is what makes sites serve
        // their mobile layouts, and the browser's own UA overrides it.
        let user_agent = if !cfg.user_agent.is_empty() {
            cfg.user_agent.clone()
        } else if cfg.mobile_platform {
            UserAgentPlatform::Android.to_user_agent_string()
        } else {
            UserAgentPlatform::Desktop.to_user_agent_string()
        };
        let mut prefs = servo::Preferences {
            user_agent,
            ..Default::default()
        };
        if !cfg.proxy.http.is_empty() {
            prefs.network_http_proxy_uri = format!("http://{}", cfg.proxy.http);
        }
        if !cfg.proxy.https.is_empty() {
            prefs.network_https_proxy_uri = format!("http://{}", cfg.proxy.https);
        }
        if !cfg.proxy.no_proxy.is_empty() {
            prefs.network_http_no_proxy = cfg.proxy.no_proxy.join(",");
        }
        for (name, value) in &cfg.prefs {
            prefs.set_value(name, pref_value(value));
        }
        let mut opts = servo::Opts::default();
        if let Some(ca) = &cfg.certificate_path {
            opts.certificate_path = Some(ca.to_string_lossy().to_string());
        }
        opts.config_dir = Some(cfg.data_dir.clone());
        opts.ignore_certificate_errors = false;

        let servo = ServoBuilder::default()
            .opts(opts)
            .preferences(prefs)
            .event_loop_waker(waker)
            .build();
        servo.set_delegate(Rc::new(TuuliServoDelegate {
            engine: self.weak_self.borrow().clone(),
        }));

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
        // Servo shuts down when its last handle drops: ours here, the
        // webviews' when the core detaches them right after this.
        let mut inner = self.inner.borrow_mut();
        inner.servo = None;
        inner.context = None;
        drop(inner);
        self.emit(EngineEvent::ShutDown);
    }

    fn create_webview(
        &self,
        sink: EventSink,
        private: bool,
        dpr: f64,
        size: (u32, u32),
    ) -> Option<Rc<dyn CoreWebView>> {
        let inner = self.inner.borrow();
        let servo = inner.servo.as_ref()?;
        let context = inner.context.as_ref()?.clone();
        let delegate = Rc::new(TuuliWebViewDelegate::new(
            self.weak_self.borrow().clone(),
            Some(sink),
        ));
        // Private browsing has no engine-side profile in 0.5.0
        // (docs/UPSTREAM.md); the browser keeps private tabs out of
        // history, session, permissions and downloads itself (spec 7.3).
        let webview = WebViewBuilder::new(servo, context)
            .delegate(delegate.clone())
            .hidpi_scale_factor(Scale::new(dpr as f32))
            .user_content_manager(Rc::new(UserContentManager::new(servo)))
            .build();
        drop(inner);
        webview.resize(dpi::PhysicalSize::new(size.0.max(1), size.1.max(1)));
        Some(self.wrap(webview, delegate, private))
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
            servo.set_preference(name, pref_value(value));
        }
    }

    fn set_proxy(&self, proxy: &ProxyConfig) {
        let inner = self.inner.borrow();
        let Some(servo) = inner.servo.as_ref() else {
            return;
        };
        let http = if proxy.http.is_empty() {
            String::new()
        } else {
            format!("http://{}", proxy.http)
        };
        let https = if proxy.https.is_empty() {
            String::new()
        } else {
            format!("http://{}", proxy.https)
        };
        servo.set_preference("network_http_proxy_uri", PrefValue::Str(http));
        servo.set_preference("network_https_proxy_uri", PrefValue::Str(https));
        servo.set_preference(
            "network_http_no_proxy",
            PrefValue::Str(proxy.no_proxy.join(",")),
        );
    }

    fn clear_site_data(&self, origin: Option<&str>, kinds: u32) {
        let inner = self.inner.borrow();
        let Some(servo) = inner.servo.as_ref() else {
            return;
        };
        let manager = servo.site_data_manager();
        let mut types = StorageType::empty();
        if kinds & site_data::COOKIES != 0 {
            types |= StorageType::Cookies;
        }
        if kinds & site_data::LOCAL_STORAGE != 0 {
            types |= StorageType::Local;
        }
        if kinds & site_data::SESSION_STORAGE != 0 {
            types |= StorageType::Session;
        }
        match origin {
            Some(site) => {
                if !types.is_empty() {
                    manager.clear_site_data(&[site], types);
                }
            }
            None => {
                if types.contains(StorageType::Cookies) {
                    manager.clear_cookies(None);
                }
                let storage = types & !StorageType::Cookies;
                if !storage.is_empty() {
                    let sites: Vec<String> = manager
                        .site_data(storage)
                        .into_iter()
                        .map(|s| s.name())
                        .collect();
                    let refs: Vec<&str> = sites.iter().map(String::as_str).collect();
                    if !refs.is_empty() {
                        manager.clear_site_data(&refs, storage);
                    }
                }
                if kinds & site_data::HTTP_CACHE != 0 {
                    servo.network_manager().clear_cache();
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

// ---- WebView delegate ---------------------------------------------------------------

struct TuuliWebViewDelegate {
    engine: Weak<ServoEngine>,
    sink: RefCell<Option<EventSink>>,
    /// The IME control currently shown, so its hide can be told apart from
    /// a dialog's or a menu's.
    ime_control: Cell<Option<EmbedderControlId>>,
}

impl TuuliWebViewDelegate {
    fn new(engine: Weak<ServoEngine>, sink: Option<EventSink>) -> Self {
        Self {
            engine,
            sink: RefCell::new(sink),
            ime_control: Cell::new(None),
        }
    }

    fn emit(&self, ev: WebViewEvent) {
        if let Some(sink) = self.sink.borrow().clone() {
            sink(ev);
        }
    }

    fn dpr(webview: &WebView) -> f64 {
        webview.hidpi_scale_factor().get() as f64
    }

    fn css_rect(webview: &WebView, rect: DeviceIntRect) -> Rect {
        let dpr = Self::dpr(webview);
        Rect::new(
            rect.min.x as f64 / dpr,
            rect.min.y as f64 / dpr,
            rect.size().width as f64 / dpr,
            rect.size().height as f64 / dpr,
        )
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
    }
}

fn to_permission_kind(feature: PermissionFeature) -> Option<PermissionKind> {
    Some(match feature {
        PermissionFeature::Geolocation => PermissionKind::Geolocation,
        PermissionFeature::Notifications | PermissionFeature::Push => PermissionKind::Notifications,
        PermissionFeature::Camera => PermissionKind::Camera,
        PermissionFeature::Microphone | PermissionFeature::Speaker => PermissionKind::Microphone,
        PermissionFeature::PersistentStorage => PermissionKind::PersistentStorage,
        PermissionFeature::Midi => PermissionKind::Midi,
        PermissionFeature::Bluetooth => PermissionKind::Bluetooth,
        // No prompt in the chrome for these; the request is dropped, which
        // denies it (spec 8.3: denied by default).
        PermissionFeature::DeviceInfo
        | PermissionFeature::BackgroundSync
        | PermissionFeature::ScreenWakeLock(_)
        | PermissionFeature::Gamepad => return None,
    })
}

/// Servo's favicon as the core's RGBA image, whatever the decoded format.
fn to_rgba(image: &servo::Image) -> Option<RgbaImage> {
    let (w, h) = (image.width, image.height);
    let src = image.data();
    let n = (w as usize) * (h as usize);
    let data = match image.format {
        PixelFormat::RGBA8 => src.to_vec(),
        PixelFormat::BGRA8 => src
            .chunks_exact(4)
            .flat_map(|p| [p[2], p[1], p[0], p[3]])
            .collect(),
        PixelFormat::RGB8 => src
            .chunks_exact(3)
            .flat_map(|p| [p[0], p[1], p[2], 255])
            .collect(),
        PixelFormat::K8 => src.iter().flat_map(|&k| [k, k, k, 255]).collect(),
        PixelFormat::KA8 => src
            .chunks_exact(2)
            .flat_map(|p| [p[0], p[0], p[0], p[1]])
            .collect(),
    };
    if data.len() != n * 4 {
        return None;
    }
    Some(RgbaImage {
        width: w,
        height: h,
        data,
    })
}

impl WebViewDelegate for TuuliWebViewDelegate {
    fn screen_geometry(&self, _webview: WebView) -> Option<ScreenGeometry> {
        let size = self.engine.upgrade()?.context()?.device_size();
        Some(ScreenGeometry {
            size,
            available_size: size,
            window_rect: DeviceIntRect::from_origin_and_size(DeviceIntPoint::zero(), size),
        })
    }
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
    fn notify_favicon_changed(&self, webview: WebView) {
        let image = webview.favicon().and_then(|img| to_rgba(&img));
        if let Some(image) = image {
            self.emit(WebViewEvent::Favicon(image));
        }
    }
    fn notify_history_changed(&self, _webview: WebView, entries: Vec<url::Url>, current: usize) {
        self.emit(WebViewEvent::History {
            can_go_back: current > 0,
            can_go_forward: current + 1 < entries.len(),
        });
    }
    fn notify_new_frame_ready(&self, _webview: WebView) {
        self.emit(WebViewEvent::FrameReady);
    }
    fn notify_closed(&self, _webview: WebView) {
        self.emit(WebViewEvent::Closed);
    }
    fn notify_crashed(&self, _webview: WebView, reason: String, backtrace: Option<String>) {
        if let Some(e) = self.engine.upgrade() {
            e.emit(EngineEvent::Crashed {
                reason,
                backtrace: backtrace.unwrap_or_default(),
            });
        }
    }
    fn notify_media_session_event(&self, _webview: WebView, event: ServoMediaSessionEvent) {
        let blank = || MediaSessionInfo {
            event: MediaSessionEvent::None,
            title: String::new(),
            artist: String::new(),
            album: String::new(),
            position_seconds: 0.0,
            duration_seconds: 0.0,
        };
        let info = match event {
            ServoMediaSessionEvent::SetMetadata(m) => MediaSessionInfo {
                event: MediaSessionEvent::Metadata,
                title: m.title,
                artist: m.artist,
                album: m.album,
                ..blank()
            },
            ServoMediaSessionEvent::PlaybackStateChange(state) => MediaSessionInfo {
                event: match state {
                    MediaSessionPlaybackState::Playing => MediaSessionEvent::Playing,
                    MediaSessionPlaybackState::Paused => MediaSessionEvent::Paused,
                    _ => MediaSessionEvent::None,
                },
                ..blank()
            },
            ServoMediaSessionEvent::SetPositionState(p) => MediaSessionInfo {
                event: MediaSessionEvent::Position,
                position_seconds: p.position,
                duration_seconds: p.duration,
                ..blank()
            },
        };
        self.emit(WebViewEvent::MediaSession(info));
    }
    fn request_navigation(&self, _webview: WebView, request: NavigationRequest) {
        // Tuuli does not filter navigations (no network-level blocking, spec 9.3).
        request.allow();
    }
    fn request_create_new(&self, _parent: WebView, request: servo::CreateNewWebViewRequest) {
        // window.open: build the auxiliary view on the shared context and
        // hand it to the browser, which adopts it as a tab and attaches
        // its sink (EngineEvent::AuxiliaryWebView).
        let Some(engine) = self.engine.upgrade() else {
            return;
        };
        let Some(context) = engine.context() else {
            return;
        };
        let delegate = Rc::new(TuuliWebViewDelegate::new(self.engine.clone(), None));
        let webview = request
            .builder(context.clone())
            .delegate(delegate.clone())
            .hidpi_scale_factor(Scale::new(_parent.hidpi_scale_factor().get()))
            .build();
        webview.resize(context.size());
        let wrapped: Rc<dyn CoreWebView> = engine.wrap(webview, delegate, false);
        engine.emit(EngineEvent::AuxiliaryWebView(wrapped));
    }
    fn request_permission(&self, webview: WebView, request: ServoPermissionRequest) {
        let Some(kind) = to_permission_kind(request.feature()) else {
            request.deny();
            return;
        };
        let origin = webview
            .url()
            .map(|u| u.origin().ascii_serialization())
            .unwrap_or_default();
        self.emit(WebViewEvent::Permission(PermissionRequest::new(
            kind,
            origin,
            move |ok| {
                if ok {
                    request.allow();
                } else {
                    request.deny();
                }
            },
        )));
    }
    fn request_authentication(&self, _webview: WebView, request: AuthenticationRequest) {
        // No credentials prompt before M2; dropping the request cancels it.
        drop(request);
    }
    fn show_embedder_control(&self, webview: WebView, control: EmbedderControl) {
        match control {
            EmbedderControl::InputMethod(ime) => {
                self.ime_control.set(Some(ime.id()));
                let text = ime.text();
                let cursor = ime
                    .insertion_point()
                    .map(|p| p as usize)
                    .unwrap_or(text.chars().count());
                self.emit(WebViewEvent::ImeShow {
                    input_type: to_input_type(ime.input_method_type()),
                    text: text.clone(),
                    multiline: ime.multiline(),
                    cursor_rect: Self::css_rect(&webview, ime.position()),
                });
                self.emit(WebViewEvent::ImeSelection {
                    text,
                    cursor,
                    anchor: cursor,
                });
            }
            EmbedderControl::SimpleDialog(dialog) => {
                let (kind, message, default) = match &dialog {
                    SimpleDialog::Alert(d) => {
                        (DialogKind::Alert, d.message().to_string(), String::new())
                    }
                    SimpleDialog::Confirm(d) => {
                        (DialogKind::Confirm, d.message().to_string(), String::new())
                    }
                    SimpleDialog::Prompt(d) => (
                        DialogKind::Prompt,
                        d.message().to_string(),
                        d.current_value().to_string(),
                    ),
                };
                self.emit(WebViewEvent::Dialog(DialogRequest::new(
                    kind,
                    message,
                    default,
                    move |answer| match dialog {
                        SimpleDialog::Alert(d) => d.confirm(),
                        SimpleDialog::Confirm(d) => {
                            if answer.is_some() {
                                d.confirm()
                            } else {
                                d.dismiss()
                            }
                        }
                        SimpleDialog::Prompt(mut d) => match answer {
                            Some(value) => {
                                d.set_current_value(&value);
                                d.confirm();
                            }
                            None => d.dismiss(),
                        },
                    },
                )));
            }
            EmbedderControl::ContextMenu(menu) => {
                self.emit(WebViewEvent::ContextMenu(context_menu_info(
                    &webview, &menu,
                )));
                // The chrome shows its own Silica menu; Servo's is dismissed.
                menu.dismiss();
            }
            // Pickers get their default answers (the Drop impls cancel);
            // Silica pickers for them are M2 work.
            EmbedderControl::SelectElement(control) => drop(control),
            EmbedderControl::ColorPicker(control) => drop(control),
            EmbedderControl::FilePicker(control) => drop(control),
        }
    }
    fn hide_embedder_control(&self, _webview: WebView, id: EmbedderControlId) {
        if self.ime_control.get() == Some(id) {
            self.ime_control.set(None);
            self.emit(WebViewEvent::ImeHide);
        }
    }
    fn show_notification(&self, _webview: WebView, notification: servo::Notification) {
        self.emit(WebViewEvent::Notification {
            title: notification.title,
            body: notification.body,
            icon: notification.icon_url.map(|u| u.to_string()),
        });
    }
    fn show_console_message(
        &self,
        _webview: WebView,
        level: servo::ConsoleLogLevel,
        message: String,
    ) {
        log::debug!("console [{level:?}]: {message}");
    }
}

fn context_menu_info(webview: &WebView, menu: &ContextMenu) -> ContextMenuInfo {
    let position = menu.position();
    let dpr = TuuliWebViewDelegate::dpr(webview);
    let info = menu.element_info();
    ContextMenuInfo {
        css: Point::new(position.min.x as f64 / dpr, position.min.y as f64 / dpr),
        link_url: info.link_url.as_ref().map(|u| u.to_string()),
        image_url: info.image_url.as_ref().map(|u| u.to_string()),
        // 0.5.0 reports that there is a selection, not its text
        // (docs/UPSTREAM.md); the chrome's Copy uses the editing action.
        selected_text: String::new(),
        editable: info
            .flags
            .contains(servo::ContextMenuElementInformationFlags::EditableText),
    }
}

// ---- WebView -------------------------------------------------------------------------

pub struct ServoWebView {
    webview: RefCell<Option<WebView>>,
    delegate: Rc<TuuliWebViewDelegate>,
    private: bool,
    closed: Cell<bool>,
    user_content: Option<Rc<UserContentManager>>,
    /// Tuuli stylesheet id -> the sheet registered with the engine.
    stylesheets: RefCell<HashMap<String, Rc<UserStyleSheet>>>,
}

impl ServoWebView {
    fn with<R>(&self, f: impl FnOnce(&WebView) -> R) -> Option<R> {
        if self.closed.get() {
            return None;
        }
        self.webview.borrow().as_ref().map(f)
    }

    fn page_point(css: Point) -> WebViewPoint {
        WebViewPoint::Page(Point2D::new(css.x as f32, css.y as f32))
    }

    /// A script the page cannot observe as ours beyond its effect.
    fn run_script(&self, script: String) {
        self.with(|w| w.evaluate_javascript(script, |_| {}));
    }
}

fn to_servo_key(key: &str) -> Key {
    match key {
        "Enter" => Key::Named(NamedKey::Enter),
        "Backspace" => Key::Named(NamedKey::Backspace),
        "Delete" => Key::Named(NamedKey::Delete),
        "Tab" => Key::Named(NamedKey::Tab),
        "Escape" => Key::Named(NamedKey::Escape),
        "ArrowLeft" => Key::Named(NamedKey::ArrowLeft),
        "ArrowRight" => Key::Named(NamedKey::ArrowRight),
        "ArrowUp" => Key::Named(NamedKey::ArrowUp),
        "ArrowDown" => Key::Named(NamedKey::ArrowDown),
        "Home" => Key::Named(NamedKey::Home),
        "End" => Key::Named(NamedKey::End),
        "PageUp" => Key::Named(NamedKey::PageUp),
        "PageDown" => Key::Named(NamedKey::PageDown),
        "Shift" => Key::Named(NamedKey::Shift),
        "Control" => Key::Named(NamedKey::Control),
        "Alt" => Key::Named(NamedKey::Alt),
        "Meta" => Key::Named(NamedKey::Meta),
        "Unidentified" => Key::Named(NamedKey::Unidentified),
        other => Key::Character(other.to_string()),
    }
}

fn to_servo_modifiers(modifiers: u32) -> ServoModifiers {
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
    m
}

/// JavaScript string literal for `s`.
fn js_string(s: &str) -> String {
    let mut out = String::with_capacity(s.len() + 2);
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '<' => out.push_str("\\u003c"),
            c => out.push(c),
        }
    }
    out.push('"');
    out
}

impl CoreWebView for ServoWebView {
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
        // No stop-loading entry point in 0.5.0 (docs/UPSTREAM.md).
        self.run_script("window.stop()".into());
    }
    fn go_back(&self) {
        self.with(|w| {
            if w.can_go_back() {
                w.go_back(1);
            }
        });
    }
    fn go_forward(&self) {
        self.with(|w| {
            if w.can_go_forward() {
                w.go_forward(1);
            }
        });
    }
    fn set_visible(&self, visible: bool) {
        self.with(|w| {
            if visible {
                w.show();
            } else {
                w.hide();
            }
            w.set_throttled(!visible);
        });
    }
    fn set_focused(&self, focused: bool) {
        self.with(|w| if focused { w.focus() } else { w.blur() });
    }
    fn set_size(&self, width: u32, height: u32) {
        if let Some(ctx) = self.delegate.engine.upgrade().and_then(|e| e.context()) {
            ctx.update_size();
        }
        self.with(|w| w.resize(dpi::PhysicalSize::new(width.max(1), height.max(1))));
    }
    fn set_viewport_rect(&self, _rect: Rect) {
        // No visible-rect-without-resize in 0.5.0 (docs/UPSTREAM.md); the
        // keyboard inset is handled by scrolling the caret into view.
    }
    fn set_device_pixel_ratio(&self, dpr: f64) {
        self.with(|w| w.set_hidpi_scale_factor(Scale::new(dpr as f32)));
    }
    fn set_pinch_zoom(&self, zoom: f64) {
        self.with(|w| {
            let current = w.pinch_zoom() as f64;
            if current > 0.0 && (zoom - current).abs() > f64::EPSILON {
                let size = w.size();
                let center = DevicePoint::new(size.width / 2.0, size.height / 2.0);
                w.adjust_pinch_zoom((zoom / current) as f32, center);
            }
        });
    }
    fn set_page_zoom(&self, zoom: f64) {
        self.with(|w| w.set_page_zoom(zoom as f32));
    }
    fn scroll_to(&self, css: Point) {
        // No absolute scroll entry point in 0.5.0; the page's own API is.
        self.run_script(format!("window.scrollTo({}, {})", css.x, css.y));
    }
    fn touch(&self, phase: TouchPhase, id: i32, css: Point) {
        self.with(|w| {
            let ty = match phase {
                TouchPhase::Down => TouchEventType::Down,
                TouchPhase::Move => TouchEventType::Move,
                TouchPhase::Up => TouchEventType::Up,
                TouchPhase::Cancel => TouchEventType::Cancel,
            };
            w.notify_input_event(InputEvent::Touch(TouchEvent::new(
                ty,
                TouchId(id),
                Self::page_point(css),
                TouchPointerType::Touch,
            )));
        });
    }
    fn key(&self, down: bool, key: &str, modifiers: u32) {
        self.with(|w| {
            let event = keyboard_types::KeyboardEvent {
                state: if down { KeyState::Down } else { KeyState::Up },
                key: to_servo_key(key),
                modifiers: to_servo_modifiers(modifiers),
                ..Default::default()
            };
            w.notify_input_event(InputEvent::Keyboard(KeyboardEvent::new(event)));
        });
    }
    fn ime_composition(&self, state: CompositionState, text: &str) {
        self.with(|w| {
            let state = match state {
                CompositionState::Start => ServoCompositionState::Start,
                CompositionState::Update => ServoCompositionState::Update,
                CompositionState::End => ServoCompositionState::End,
            };
            w.notify_input_event(InputEvent::Ime(ImeEvent::Composition(CompositionEvent {
                state,
                data: text.to_string(),
            })));
        });
    }
    fn ime_dismissed(&self) {
        self.with(|w| w.notify_input_event(InputEvent::Ime(ImeEvent::Dismissed)));
    }
    fn editing_action(&self, action: EditingAction) {
        match action {
            EditingAction::Copy => {
                self.with(|w| {
                    w.notify_input_event(InputEvent::EditingAction(EditingActionEvent::Copy))
                });
            }
            EditingAction::Cut => {
                self.with(|w| {
                    w.notify_input_event(InputEvent::EditingAction(EditingActionEvent::Cut))
                });
            }
            EditingAction::Paste => {
                self.with(|w| {
                    w.notify_input_event(InputEvent::EditingAction(EditingActionEvent::Paste))
                });
            }
            // Not an editing action in 0.5.0; the page's API does it.
            EditingAction::SelectAll => self.run_script("document.execCommand('selectAll')".into()),
        }
    }
    fn request_context_menu(&self, css: Point) {
        // No hit test at a point in 0.5.0 (docs/UPSTREAM.md).  A secondary
        // click is what makes script fire `contextmenu`, which comes back
        // as an EmbedderControl::ContextMenu with the element's links.
        self.with(|w| {
            let point = Self::page_point(css);
            w.notify_input_event(InputEvent::MouseButton(MouseButtonEvent::new(
                MouseButtonAction::Down,
                MouseButton::Right,
                point,
            )));
            w.notify_input_event(InputEvent::MouseButton(MouseButtonEvent::new(
                MouseButtonAction::Up,
                MouseButton::Right,
                point,
            )));
        });
    }
    fn find(&self, text: &str, case_sensitive: bool) {
        // No find-in-page in 0.5.0 (docs/UPSTREAM.md); the page's window.find
        // is what there is.
        self.run_script(format!(
            "window.find({}, {}, false, true)",
            js_string(text),
            case_sensitive
        ));
    }
    fn find_next(&self, forward: bool) {
        self.run_script(format!("window.find(undefined, false, {}, true)", !forward));
    }
    fn find_clear(&self) {
        self.run_script("window.getSelection && window.getSelection().removeAllRanges()".into());
    }
    fn add_user_stylesheet(&self, id: &str, css: &str) {
        let Some(manager) = &self.user_content else {
            return;
        };
        let url = url::Url::parse(&format!("tuuli://stylesheet/{id}")).ok();
        let Some(url) = url else { return };
        let sheet = Rc::new(UserStyleSheet::new(css.to_string(), url));
        if let Some(old) = self
            .stylesheets
            .borrow_mut()
            .insert(id.to_string(), sheet.clone())
        {
            manager.remove_stylesheet(old);
        }
        manager.add_stylesheet(sheet);
    }
    fn remove_user_stylesheet(&self, id: &str) {
        let Some(manager) = &self.user_content else {
            return;
        };
        if let Some(sheet) = self.stylesheets.borrow_mut().remove(id) {
            manager.remove_stylesheet(sheet);
        }
    }
    fn set_user_agent_override(&self, _ua: Option<&str>) {
        // The UA is one preference for the whole engine in 0.5.0
        // (docs/UPSTREAM.md); per-tab desktop mode waits for it.
    }
    fn evaluate_javascript(&self, script: &str) {
        self.run_script(script.to_string());
    }
    fn capture(&self) -> Option<RgbaImage> {
        // The FBO as painted, read back while the context is current.
        let ctx = self.delegate.engine.upgrade().and_then(|e| e.context())?;
        let size = ctx.device_size();
        let image = ctx.read_to_image(DeviceIntRect::from_origin_and_size(
            DeviceIntPoint::zero(),
            size,
        ))?;
        Some(RgbaImage {
            width: image.width(),
            height: image.height(),
            data: image.into_raw(),
        })
    }
    fn cancel_download(&self, _id: u64) {
        // No downloads in 0.5.0 (docs/UPSTREAM.md).
    }
    fn paint(&self) -> bool {
        if let Some(ctx) = self.delegate.engine.upgrade().and_then(|e| e.context()) {
            ctx.update_size();
        }
        self.with(|w| w.paint()).is_some()
    }
    fn close(&self) {
        if self.closed.replace(true) {
            return;
        }
        *self.delegate.sink.borrow_mut() = None;
        self.stylesheets.borrow_mut().clear();
        if let Some(w) = self.webview.borrow_mut().take() {
            drop(w);
        }
    }
}
