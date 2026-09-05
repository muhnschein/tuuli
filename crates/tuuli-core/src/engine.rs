// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The seam between the browser and the web engine (spec 4.1).
//!
//! Nothing above this module names a Servo type.  `tuuli-servo` implements
//! [`Engine`] and [`WebView`] over libservo; [`crate::mock`] implements
//! them in-process for tests and UI iteration.
//!
//! Threading: everything here is single-threaded on the Qt GUI thread
//! (libservo's types are not `Send`).  The only cross-thread entry point is
//! the waker handed to [`Engine::set_waker`], which may be called from any
//! engine thread and must only schedule a call to
//! [`Engine::spin_event_loop`] on the GUI thread.  Events are delivered to
//! sinks **only** from inside `spin_event_loop`, never re-entrantly from a
//! call the browser makes into the engine.

use std::ffi::c_void;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::Arc;

use crate::geometry::{Point, Rect, Size};
use crate::input::TouchPhase;
use crate::proxy::ProxyConfig;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LoadStatus {
    Started,
    HeadParsed,
    Complete,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum InputType {
    #[default]
    None,
    Text,
    Url,
    Email,
    Number,
    Password,
    Tel,
    Search,
    Date,
    Time,
    DateTime,
    Month,
    Week,
    Color,
}

impl InputType {
    pub fn from_index(i: i32) -> InputType {
        use InputType::*;
        match i {
            1 => Text,
            2 => Url,
            3 => Email,
            4 => Number,
            5 => Password,
            6 => Tel,
            7 => Search,
            8 => Date,
            9 => Time,
            10 => DateTime,
            11 => Month,
            12 => Week,
            13 => Color,
            _ => None,
        }
    }
    pub fn index(self) -> i32 {
        self as i32
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub enum PermissionKind {
    Geolocation,
    Notifications,
    Camera,
    Microphone,
    PersistentStorage,
    Midi,
    Bluetooth,
    ClipboardRead,
    ClipboardWrite,
}

impl PermissionKind {
    pub const ALL: [PermissionKind; 9] = [
        PermissionKind::Geolocation,
        PermissionKind::Notifications,
        PermissionKind::Camera,
        PermissionKind::Microphone,
        PermissionKind::PersistentStorage,
        PermissionKind::Midi,
        PermissionKind::Bluetooth,
        PermissionKind::ClipboardRead,
        PermissionKind::ClipboardWrite,
    ];
    pub fn name(self) -> &'static str {
        match self {
            PermissionKind::Geolocation => "geolocation",
            PermissionKind::Notifications => "notifications",
            PermissionKind::Camera => "camera",
            PermissionKind::Microphone => "microphone",
            PermissionKind::PersistentStorage => "persistent-storage",
            PermissionKind::Midi => "midi",
            PermissionKind::Bluetooth => "bluetooth",
            PermissionKind::ClipboardRead => "clipboard-read",
            PermissionKind::ClipboardWrite => "clipboard-write",
        }
    }
    pub fn index(self) -> u32 {
        self as u32
    }
    pub fn from_index(i: u32) -> Option<PermissionKind> {
        Self::ALL.get(i as usize).copied()
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CompositionState {
    Start,
    Update,
    End,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EditingAction {
    Copy,
    Cut,
    Paste,
    SelectAll,
}

impl EditingAction {
    pub fn from_index(i: i32) -> Option<EditingAction> {
        match i {
            0 => Some(EditingAction::Copy),
            1 => Some(EditingAction::Cut),
            2 => Some(EditingAction::Paste),
            3 => Some(EditingAction::SelectAll),
            _ => None,
        }
    }
}

/// Bit set for [`Engine::clear_site_data`].
pub mod site_data {
    pub const COOKIES: u32 = 1 << 0;
    pub const LOCAL_STORAGE: u32 = 1 << 1;
    pub const SESSION_STORAGE: u32 = 1 << 2;
    pub const HTTP_CACHE: u32 = 1 << 3;
    pub const ALL: u32 = 0xFFFF;
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MediaSessionEvent {
    Metadata,
    Playing,
    Paused,
    None,
    Position,
}

#[derive(Clone, Debug, PartialEq)]
pub struct MediaSessionInfo {
    pub event: MediaSessionEvent,
    pub title: String,
    pub artist: String,
    pub album: String,
    pub position_seconds: f64,
    pub duration_seconds: f64,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct EngineConfig {
    /// Empty = engine default for the platform.
    pub user_agent: String,
    /// Spec 5.4: engage the engine's mobile UA platform / viewport rules.
    pub mobile_platform: bool,
    /// Spec 8.1: system CA bundle.
    pub certificate_path: Option<PathBuf>,
    pub data_dir: PathBuf,
    pub cache_dir: PathBuf,
    pub proxy: ProxyConfig,
    /// Engine preference overrides, `(name, value)`.
    pub prefs: Vec<(String, String)>,
    /// Spec 8.2: gst-droid path.
    pub hardware_video_decode: bool,
    pub layout_threads: u32,
}

/// RGBA8, row stride `width * 4`.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct RgbaImage {
    pub width: u32,
    pub height: u32,
    pub data: Vec<u8>,
}

impl RgbaImage {
    pub fn solid(width: u32, height: u32, rgba: [u8; 4]) -> Self {
        let mut data = Vec::with_capacity((width * height * 4) as usize);
        for _ in 0..width * height {
            data.extend_from_slice(&rgba);
        }
        Self {
            width,
            height,
            data,
        }
    }
    pub fn is_empty(&self) -> bool {
        self.width == 0
            || self.height == 0
            || self.data.len() < (self.width * self.height * 4) as usize
    }
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct ContextMenuInfo {
    pub css: Point,
    pub link_url: Option<String>,
    pub image_url: Option<String>,
    pub selected_text: String,
    pub editable: bool,
}

/// One-shot permission request (spec 8.3).  Dropping it unanswered denies:
/// "denied by default" is enforced by the type, not by convention.
pub struct PermissionRequest {
    pub kind: PermissionKind,
    pub origin: String,
    responder: Option<Box<dyn FnOnce(bool)>>,
}

impl std::fmt::Debug for PermissionRequest {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("PermissionRequest")
            .field("kind", &self.kind)
            .field("origin", &self.origin)
            .finish()
    }
}

impl PermissionRequest {
    pub fn new(
        kind: PermissionKind,
        origin: impl Into<String>,
        responder: impl FnOnce(bool) + 'static,
    ) -> Self {
        Self {
            kind,
            origin: origin.into(),
            responder: Some(Box::new(responder)),
        }
    }
    pub fn allow(&mut self) {
        if let Some(r) = self.responder.take() {
            r(true);
        }
    }
    pub fn deny(&mut self) {
        if let Some(r) = self.responder.take() {
            r(false);
        }
    }
    pub fn answered(&self) -> bool {
        self.responder.is_none()
    }
}

impl Drop for PermissionRequest {
    fn drop(&mut self) {
        self.deny();
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DialogKind {
    Alert,
    Confirm,
    Prompt,
}

/// window.alert / confirm / prompt.  Dropping it unanswered dismisses.
pub struct DialogRequest {
    pub kind: DialogKind,
    pub message: String,
    pub default_value: String,
    responder: Option<Box<dyn FnOnce(Option<String>)>>,
}

impl std::fmt::Debug for DialogRequest {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DialogRequest")
            .field("kind", &self.kind)
            .field("message", &self.message)
            .finish()
    }
}

impl DialogRequest {
    pub fn new(
        kind: DialogKind,
        message: impl Into<String>,
        default_value: impl Into<String>,
        responder: impl FnOnce(Option<String>) + 'static,
    ) -> Self {
        Self {
            kind,
            message: message.into(),
            default_value: default_value.into(),
            responder: Some(Box::new(responder)),
        }
    }
    /// `Some(value)` accepts (the value is the prompt text, empty otherwise).
    pub fn accept(&mut self, value: String) {
        if let Some(r) = self.responder.take() {
            r(Some(value));
        }
    }
    pub fn dismiss(&mut self) {
        if let Some(r) = self.responder.take() {
            r(None);
        }
    }
    pub fn answered(&self) -> bool {
        self.responder.is_none()
    }
}

impl Drop for DialogRequest {
    fn drop(&mut self) {
        self.dismiss();
    }
}

/// The engine performs the transfer; the browser chooses the destination
/// (spec 7.1).  Progress and completion arrive as [`WebViewEvent`]s keyed
/// by `id`.  Dropping it unanswered rejects.
pub struct DownloadRequest {
    pub id: u64,
    pub url: String,
    pub suggested_name: String,
    pub mime_type: String,
    /// -1 when unknown.
    pub total_bytes: i64,
    responder: Option<Box<dyn FnOnce(Option<PathBuf>)>>,
}

impl std::fmt::Debug for DownloadRequest {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DownloadRequest")
            .field("id", &self.id)
            .field("url", &self.url)
            .finish()
    }
}

impl DownloadRequest {
    pub fn new(
        id: u64,
        url: impl Into<String>,
        suggested_name: impl Into<String>,
        mime_type: impl Into<String>,
        total_bytes: i64,
        responder: impl FnOnce(Option<PathBuf>) + 'static,
    ) -> Self {
        Self {
            id,
            url: url.into(),
            suggested_name: suggested_name.into(),
            mime_type: mime_type.into(),
            total_bytes,
            responder: Some(Box::new(responder)),
        }
    }
    pub fn accept(&mut self, destination: PathBuf) {
        if let Some(r) = self.responder.take() {
            r(Some(destination));
        }
    }
    pub fn reject(&mut self) {
        if let Some(r) = self.responder.take() {
            r(None);
        }
    }
}

impl Drop for DownloadRequest {
    fn drop(&mut self) {
        self.reject();
    }
}

/// Everything a webview reports.  Delivered from inside
/// [`Engine::spin_event_loop`] on the GUI thread.
#[derive(Debug)]
pub enum WebViewEvent {
    UrlChanged(String),
    TitleChanged(String),
    LoadStatus(LoadStatus),
    Favicon(RgbaImage),
    History {
        can_go_back: bool,
        can_go_forward: bool,
    },
    FrameReady,
    /// Root scroll offset, pinch zoom and content size (CSS px).
    Viewport {
        scroll: Point,
        zoom: f64,
        content: Size,
    },
    ImeShow {
        input_type: InputType,
        text: String,
        multiline: bool,
        cursor_rect: Rect,
    },
    ImeHide,
    ImeSelection {
        text: String,
        cursor: usize,
        anchor: usize,
    },
    Permission(PermissionRequest),
    Dialog(DialogRequest),
    ContextMenu(ContextMenuInfo),
    DownloadRequested(DownloadRequest),
    DownloadProgress {
        id: u64,
        received: i64,
        total: i64,
    },
    DownloadFinished {
        id: u64,
        ok: bool,
        error: String,
    },
    MediaSession(MediaSessionInfo),
    Notification {
        title: String,
        body: String,
        icon: Option<String>,
    },
    /// window.open / target=_blank without an engine-created view.
    NewWebViewRequested {
        url: Option<String>,
    },
    Closed,
}

pub type EventSink = Rc<dyn Fn(WebViewEvent)>;

/// Engine-level events.
pub enum EngineEvent {
    Initialized,
    Crashed {
        reason: String,
        backtrace: String,
    },
    /// The engine created a webview itself (window.open); the browser
    /// adopts it into a tab.
    AuxiliaryWebView(Rc<dyn WebView>),
    ShutDown,
}

impl std::fmt::Debug for EngineEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            EngineEvent::Initialized => write!(f, "Initialized"),
            EngineEvent::Crashed { reason, .. } => write!(f, "Crashed({reason})"),
            EngineEvent::AuxiliaryWebView(_) => write!(f, "AuxiliaryWebView"),
            EngineEvent::ShutDown => write!(f, "ShutDown"),
        }
    }
}

pub type EngineEventSink = Rc<dyn Fn(EngineEvent)>;
pub type Waker = Arc<dyn Fn() + Send + Sync>;

/// Embedder-owned GL rendering context (spec 5.2): the scene graph's
/// context, current on the GUI thread (basic render loop) whenever the
/// engine is initialised, painted or shut down.  The engine never creates
/// its own EGL context.
pub trait RenderingContext {
    /// Current FBO size, device px.
    fn size(&self) -> (u32, u32);
    /// The framebuffer the engine must paint into (bound by the caller).
    fn framebuffer_object(&self) -> u32;
    fn proc_address(&self, name: &str) -> *const c_void;
    fn is_current(&self) -> bool;
    fn gl_version(&self) -> (u32, u32);
    fn is_gles(&self) -> bool;
}

/// One engine webview.  All methods on the GUI thread.
pub trait WebView {
    fn is_private(&self) -> bool;
    /// Re-targets event delivery (a tab adopting an engine-created view).
    fn set_client(&self, sink: EventSink);

    fn load(&self, url: &str);
    fn reload(&self);
    fn stop(&self);
    fn go_back(&self);
    fn go_forward(&self);

    fn set_visible(&self, visible: bool);
    fn set_focused(&self, focused: bool);
    fn set_size(&self, width: u32, height: u32);
    /// Visible viewport inside the surface (VKB avoidance, spec 6.3), device px.
    fn set_viewport_rect(&self, rect: Rect);
    fn set_device_pixel_ratio(&self, dpr: f64);
    fn set_pinch_zoom(&self, zoom: f64);
    fn set_page_zoom(&self, zoom: f64);
    fn scroll_to(&self, css: Point);

    /// Points in CSS px relative to the webview origin (spec 6.1).
    fn touch(&self, phase: TouchPhase, id: i32, css: Point);
    /// `key` is a W3C `KeyboardEvent.key` name; modifiers bit 0 shift,
    /// 1 ctrl, 2 alt, 3 meta.
    fn key(&self, down: bool, key: &str, modifiers: u32);
    fn ime_composition(&self, state: CompositionState, text: &str);
    fn ime_dismissed(&self);
    fn editing_action(&self, action: EditingAction);
    /// Hit test for an embedder-detected long-press (spec 6.2); the result
    /// arrives as [`WebViewEvent::ContextMenu`].
    fn request_context_menu(&self, css: Point);

    fn find(&self, text: &str, case_sensitive: bool);
    fn find_next(&self, forward: bool);
    fn find_clear(&self);

    fn add_user_stylesheet(&self, id: &str, css: &str);
    fn remove_user_stylesheet(&self, id: &str);
    fn set_user_agent_override(&self, ua: Option<&str>);
    fn evaluate_javascript(&self, script: &str);
    fn capture(&self) -> Option<RgbaImage>;
    fn cancel_download(&self, id: u64);

    /// GL current, target FBO bound.  Returns true if content was painted.
    fn paint(&self) -> bool;
    fn close(&self);
}

pub trait Engine {
    fn name(&self) -> &'static str;
    fn version(&self) -> String;

    fn configure(&self, config: EngineConfig);
    fn config(&self) -> EngineConfig;

    /// GL current.  The engine compiles its shaders here (spec 5.3);
    /// failure is final for the process.
    fn initialize(&self, ctx: Rc<dyn RenderingContext>) -> Result<(), String>;
    fn is_initialized(&self) -> bool;
    /// GL current.  Tears down GL state; emits [`EngineEvent::ShutDown`].
    fn shutdown(&self);

    fn create_webview(
        &self,
        sink: EventSink,
        private: bool,
        dpr: f64,
        size: (u32, u32),
    ) -> Option<Rc<dyn WebView>>;
    /// Runs pending engine work and delivers events.  Top-level only.
    fn spin_event_loop(&self);

    fn set_pref(&self, name: &str, value: &str);
    fn set_proxy(&self, proxy: &ProxyConfig);
    /// `None` clears everything (SiteDataManager, spec 7.3).
    fn clear_site_data(&self, origin: Option<&str>, kinds: u32);

    fn set_event_sink(&self, sink: EngineEventSink);
    fn set_waker(&self, waker: Waker);
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;

    #[test]
    fn permission_request_denies_when_dropped() {
        let answered = Rc::new(Cell::new(None));
        {
            let a = answered.clone();
            let _req = PermissionRequest::new(
                PermissionKind::Geolocation,
                "https://a.example",
                move |ok| a.set(Some(ok)),
            );
        }
        assert_eq!(answered.get(), Some(false));

        let a = answered.clone();
        let mut req =
            PermissionRequest::new(PermissionKind::Camera, "https://a.example", move |ok| {
                a.set(Some(ok))
            });
        req.allow();
        assert!(req.answered());
        assert_eq!(answered.get(), Some(true));
        req.deny(); // no double answer
        assert_eq!(answered.get(), Some(true));
    }

    #[test]
    fn dialog_and_download_requests_answer_once() {
        let got: Rc<Cell<Option<Option<usize>>>> = Rc::new(Cell::new(None));
        let g = got.clone();
        let mut d = DialogRequest::new(DialogKind::Prompt, "name?", "x", move |v| {
            g.set(Some(v.map(|s| s.len())))
        });
        d.accept("hello".into());
        assert_eq!(got.get(), Some(Some(5)));
        d.dismiss();
        assert_eq!(got.get(), Some(Some(5)));

        let dest = Rc::new(std::cell::RefCell::new(None));
        let dd = dest.clone();
        {
            let _r = DownloadRequest::new(1, "https://x/y", "y", "", -1, move |p| {
                *dd.borrow_mut() = Some(p)
            });
        }
        assert_eq!(*dest.borrow(), Some(None));
    }

    #[test]
    fn enum_indices_round_trip() {
        for k in PermissionKind::ALL {
            assert_eq!(PermissionKind::from_index(k.index()), Some(k));
        }
        assert_eq!(
            InputType::from_index(InputType::Email.index()),
            InputType::Email
        );
        assert_eq!(InputType::from_index(99), InputType::None);
        assert_eq!(EditingAction::from_index(2), Some(EditingAction::Paste));
    }
}
