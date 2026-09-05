// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The browser: owns the engine, the tabs and the stores and wires them
//! together (session persistence, history, permissions, downloads,
//! cosmetic filtering, proxy, perf).  The Qt `Browser` singleton is a thin
//! wrapper over this.  Everything is single-threaded; see [`crate`].
//!
//! Flow: the Qt layer calls into the browser (user actions) or calls
//! [`Browser::spin`] (engine wake-up), then [`Browser::pump`] to apply the
//! glue and collect what the UI must react to.

use std::cell::RefCell;
use std::path::{Path, PathBuf};
use std::rc::Rc;

use crate::bookmarks::BookmarkStore;
use crate::cosmetic::CosmeticFilter;
use crate::downloads::{DownloadEvent, DownloadManager, TransferEngine};
use crate::engine::*;
use crate::geometry::Rect;
use crate::history::HistoryStore;
use crate::paths::AppPaths;
use crate::perflog::PerfLog;
use crate::permissions::{Decision, PermissionStore};
use crate::prefs::Preferences;
use crate::proxy::ProxyConfig;
use crate::search;
use crate::session::{Session, SessionStore};
use crate::tabs::{SharedTabList, TabEvent, TabId, TabList};
use crate::useragent;

pub const COSMETIC_STYLESHEET_ID: &str = "tuuli-cosmetic";

/// What the chrome has to react to after a [`Browser::pump`].
pub enum BrowserEvent {
    /// Model-level tab changes, for the Qt list model.
    Tab(TabEvent),
    Download(DownloadEvent),
    /// Spec 8.3: every prompt is a dialog, denied by default.
    PermissionPrompt {
        tab: TabId,
        private: bool,
        request: PermissionRequest,
    },
    DialogPrompt {
        tab: TabId,
        private: bool,
        request: DialogRequest,
    },
    ContextMenu {
        tab: TabId,
        info: ContextMenuInfo,
    },
    ImeShow {
        tab: TabId,
        input_type: InputType,
        text: String,
        multiline: bool,
        cursor_rect: Rect,
    },
    ImeHide {
        tab: TabId,
    },
    ImeSelection {
        tab: TabId,
        text: String,
        cursor: usize,
        anchor: usize,
    },
    Notification {
        title: String,
        body: String,
    },
    DownloadStarted {
        file_name: String,
    },
    EngineInitialized,
    EngineCrashed {
        reason: String,
    },
    RenderContextLost,
    MediaSession {
        tab: TabId,
        info: MediaSessionInfo,
    },
    FrameReady {
        tab: TabId,
    },
    HistoryChanged,
    BookmarksChanged,
    PermissionsChanged,
    ProxyChanged,
    SessionSaveRequested,
}

impl std::fmt::Debug for BrowserEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BrowserEvent::Tab(t) => write!(f, "Tab({t:?})"),
            BrowserEvent::Download(d) => write!(f, "Download({d:?})"),
            BrowserEvent::PermissionPrompt { tab, .. } => write!(f, "PermissionPrompt({tab})"),
            BrowserEvent::DialogPrompt { tab, .. } => write!(f, "DialogPrompt({tab})"),
            BrowserEvent::ContextMenu { tab, .. } => write!(f, "ContextMenu({tab})"),
            BrowserEvent::ImeShow { tab, .. } => write!(f, "ImeShow({tab})"),
            BrowserEvent::ImeHide { tab } => write!(f, "ImeHide({tab})"),
            BrowserEvent::ImeSelection { tab, .. } => write!(f, "ImeSelection({tab})"),
            BrowserEvent::Notification { title, .. } => write!(f, "Notification({title})"),
            BrowserEvent::DownloadStarted { file_name } => {
                write!(f, "DownloadStarted({file_name})")
            }
            BrowserEvent::EngineInitialized => write!(f, "EngineInitialized"),
            BrowserEvent::EngineCrashed { reason } => write!(f, "EngineCrashed({reason})"),
            BrowserEvent::RenderContextLost => write!(f, "RenderContextLost"),
            BrowserEvent::MediaSession { tab, .. } => write!(f, "MediaSession({tab})"),
            BrowserEvent::FrameReady { tab } => write!(f, "FrameReady({tab})"),
            BrowserEvent::HistoryChanged => write!(f, "HistoryChanged"),
            BrowserEvent::BookmarksChanged => write!(f, "BookmarksChanged"),
            BrowserEvent::PermissionsChanged => write!(f, "PermissionsChanged"),
            BrowserEvent::ProxyChanged => write!(f, "ProxyChanged"),
            BrowserEvent::SessionSaveRequested => write!(f, "SessionSaveRequested"),
        }
    }
}

pub struct Browser {
    pub paths: AppPaths,
    pub engine: Rc<dyn Engine>,
    pub tabs: SharedTabList,
    pub history: HistoryStore,
    pub bookmarks: BookmarkStore,
    pub permissions: PermissionStore,
    pub session: SessionStore,
    pub downloads: DownloadManager,
    pub prefs: Preferences,
    pub filter: CosmeticFilter,
    pub perf: PerfLog,
    proxy: ProxyConfig,
    engine_events: Rc<RefCell<Vec<EngineEvent>>>,
    restored_after_crash: bool,
    engine_error: Option<String>,
    started: bool,
    pending: Vec<BrowserEvent>,
}

impl Browser {
    /// Opens the stores under `paths`.  `transfers` is the Qt layer's
    /// Transfer Engine client, if any.
    pub fn new(
        engine: Rc<dyn Engine>,
        paths: AppPaths,
        transfers: Option<Box<dyn TransferEngine>>,
    ) -> Result<Self, String> {
        paths
            .create_all()
            .map_err(|e| format!("cannot create data directories: {e}"))?;
        let prefs = Preferences::load(&paths.prefs_file());
        let history =
            HistoryStore::open(&paths.history_db()).map_err(|e| format!("history db: {e}"))?;
        let bookmarks =
            BookmarkStore::open(&paths.bookmarks_db()).map_err(|e| format!("bookmarks db: {e}"))?;
        let permissions = PermissionStore::new(Some(paths.permissions_file()));
        let session = SessionStore::new(paths.session_file());
        let mut downloads =
            DownloadManager::new(prefs.download_dir(&paths.download_dir), transfers);
        downloads.set_directory(prefs.download_dir(&paths.download_dir));
        let mut perf = PerfLog::new(paths.perf_log());
        perf.set_enabled(prefs.perf_logging);
        let tabs = TabList::new_shared(engine.clone());
        tabs.borrow_mut()
            .set_max_live_webviews(prefs.max_live_webviews);

        let engine_events: Rc<RefCell<Vec<EngineEvent>>> = Rc::new(RefCell::new(Vec::new()));
        let sink_target = engine_events.clone();
        engine.set_event_sink(Rc::new(move |ev| sink_target.borrow_mut().push(ev)));

        let mut b = Self {
            paths,
            engine,
            tabs,
            history,
            bookmarks,
            permissions,
            session,
            downloads,
            prefs,
            filter: CosmeticFilter::new(),
            perf,
            proxy: ProxyConfig::default(),
            engine_events,
            restored_after_crash: false,
            engine_error: None,
            started: false,
            pending: Vec::new(),
        };
        b.configure_engine();
        Ok(b)
    }

    pub fn restored_after_crash(&self) -> bool {
        self.restored_after_crash
    }
    pub fn engine_error(&self) -> Option<&str> {
        self.engine_error.as_deref()
    }
    pub fn proxy(&self) -> &ProxyConfig {
        &self.proxy
    }
    pub fn version(&self) -> &'static str {
        crate::VERSION
    }

    fn configure_engine(&mut self) {
        let mut cfg = EngineConfig {
            user_agent: self.prefs.user_agent_override.clone(),
            mobile_platform: true,
            certificate_path: None,
            data_dir: self.paths.engine_data_dir(),
            cache_dir: self.paths.engine_cache_dir(),
            proxy: self.proxy.clone(),
            prefs: self.prefs.engine_prefs(),
            hardware_video_decode: true,
            layout_threads: 0,
        };
        if cfg.user_agent.is_empty() {
            cfg.user_agent = useragent::mobile(&self.engine.version(), crate::VERSION);
        }
        // Spec 8.1: the system CA bundle, never our own roots.
        for c in [
            "/etc/pki/tls/certs/ca-bundle.crt",
            "/etc/ssl/certs/ca-certificates.crt",
        ] {
            if Path::new(c).exists() {
                cfg.certificate_path = Some(PathBuf::from(c));
                break;
            }
        }
        self.engine.configure(cfg);
    }

    /// Startup: filters, session restore, command-line URLs.  Writes a
    /// session marked "running" so a crash is detected next time.
    pub fn start(&mut self, args: &[String]) {
        if self.started {
            return;
        }
        self.reload_cosmetic_rules();
        self.restore_session();
        self.started = true;
        for a in args.iter().skip(1) {
            if a.starts_with('-') {
                continue;
            }
            if let Some(url) = self.resolve_input(a) {
                self.open_url(&url, false, true);
            }
        }
        let snapshot = self.tabs.borrow().snapshot();
        let _ = self.session.save_now(&snapshot);
    }

    pub fn restore_session(&mut self) {
        let existed = self.session.exists();
        match self.session.load() {
            Ok(s) => {
                self.restored_after_crash = existed && !s.clean_exit && !s.tabs.is_empty();
                if self.prefs.restore_session && !s.tabs.is_empty() {
                    self.tabs.borrow_mut().restore(&s);
                }
            }
            Err(_) => self.restored_after_crash = false,
        }
    }

    pub fn save_session_now(&mut self) {
        let s = self.tabs.borrow().snapshot();
        let _ = self.session.save_now(&s);
    }

    /// Spec 8.4: every backgrounding flushes.
    pub fn on_application_inactive(&mut self) {
        self.save_session_now();
    }

    pub fn on_about_to_quit(&mut self) {
        let mut s: Session = self.tabs.borrow().snapshot();
        s.clean_exit = true;
        let _ = self.session.save_now(&s);
        let _ = self.prefs.save(&self.paths.prefs_file());
    }

    // ---- engine ------------------------------------------------------------

    /// Engine wake-up: run its loop, then the glue.
    pub fn spin(&mut self) {
        self.engine.spin_event_loop();
    }

    /// GL current: called by the view item on its first render.
    pub fn initialize_engine(&mut self, ctx: Rc<dyn RenderingContext>) -> Result<(), String> {
        match self.engine.initialize(ctx) {
            Ok(()) => Ok(()),
            Err(e) => {
                self.engine_error = Some(e.clone());
                self.pending
                    .push(BrowserEvent::EngineCrashed { reason: e.clone() });
                Err(e)
            }
        }
    }

    /// GL current: scene-graph invalidation (spec 5.2).
    pub fn on_render_context_lost(&mut self) {
        self.engine.shutdown();
        self.tabs.borrow_mut().on_render_context_lost();
        self.pending.push(BrowserEvent::RenderContextLost);
    }

    // ---- glue --------------------------------------------------------------

    /// Whether [`pump`](Self::pump) would return anything.
    pub fn has_pending_events(&self) -> bool {
        !self.pending.is_empty()
            || !self.engine_events.borrow().is_empty()
            || self.tabs.borrow().has_events()
    }

    /// Applies everything the stores and the UI need from the queued engine,
    /// tab and download events, and returns what the chrome must react to.
    pub fn pump(&mut self) -> Vec<BrowserEvent> {
        let engine_events: Vec<EngineEvent> = std::mem::take(&mut *self.engine_events.borrow_mut());
        for ev in engine_events {
            match ev {
                EngineEvent::Initialized => {
                    self.push_engine_prefs();
                    self.tabs.borrow_mut().on_engine_initialized();
                    self.pending.push(BrowserEvent::EngineInitialized);
                }
                EngineEvent::Crashed { reason, .. } => {
                    self.engine_error = Some(reason.clone());
                    let _ = self.session.flush();
                    self.save_session_now();
                    self.pending.push(BrowserEvent::EngineCrashed { reason });
                }
                EngineEvent::AuxiliaryWebView(wv) => {
                    self.tabs.borrow_mut().adopt_webview(wv);
                }
                EngineEvent::ShutDown => {}
            }
        }

        let tab_events = self.tabs.borrow_mut().take_events();
        let mut session_dirty = false;
        for ev in tab_events {
            match ev {
                TabEvent::SessionDirty => session_dirty = true,
                TabEvent::Navigation { id, url, private } => {
                    let title = self
                        .tabs
                        .borrow()
                        .by_id(id)
                        .map(|t| t.title.clone())
                        .unwrap_or_default();
                    if self.history.add_visit(&url, &title, private) {
                        self.pending.push(BrowserEvent::HistoryChanged);
                    }
                    self.apply_cosmetic_filter(id);
                    self.perf.navigation_started(id, &url);
                }
                TabEvent::TitleCommitted {
                    url,
                    title,
                    private,
                    ..
                } => {
                    if self.history.update_title(&url, &title, private) {
                        self.pending.push(BrowserEvent::HistoryChanged);
                    }
                }
                TabEvent::LoadFinished { id } => self.perf.load_finished(id),
                TabEvent::FrameReady { id } => {
                    self.perf.mark_first_paint(!self.restored_after_crash);
                    let n = self.tabs.borrow().len();
                    self.perf.frame_ready(id, n);
                    self.pending.push(BrowserEvent::FrameReady { tab: id });
                }
                TabEvent::WebViewAttached { id } => self.apply_cosmetic_filter(id),
                TabEvent::Permission {
                    id,
                    private,
                    mut request,
                } => {
                    if !self.permissions.answer_from_store(&mut request) {
                        self.pending.push(BrowserEvent::PermissionPrompt {
                            tab: id,
                            private,
                            request,
                        });
                    }
                }
                TabEvent::Dialog {
                    id,
                    private,
                    request,
                } => self.pending.push(BrowserEvent::DialogPrompt {
                    tab: id,
                    private,
                    request,
                }),
                TabEvent::ContextMenu { id, info } => self
                    .pending
                    .push(BrowserEvent::ContextMenu { tab: id, info }),
                TabEvent::DownloadRequested {
                    id,
                    private,
                    request,
                } => {
                    let file_name = request.suggested_name.clone();
                    self.downloads.handle_request(id, request, private);
                    self.pending
                        .push(BrowserEvent::DownloadStarted { file_name });
                }
                TabEvent::DownloadProgress {
                    id,
                    download,
                    received,
                    total,
                } => self.downloads.progress(id, download, received, total),
                TabEvent::DownloadFinished {
                    id,
                    download,
                    ok,
                    error,
                } => self.downloads.finished(id, download, ok, &error),
                TabEvent::MediaSession { id, info } => self
                    .pending
                    .push(BrowserEvent::MediaSession { tab: id, info }),
                TabEvent::Notification { title, body, .. } => self
                    .pending
                    .push(BrowserEvent::Notification { title, body }),
                TabEvent::ImeShow {
                    id,
                    input_type,
                    text,
                    multiline,
                    cursor_rect,
                } => self.pending.push(BrowserEvent::ImeShow {
                    tab: id,
                    input_type,
                    text,
                    multiline,
                    cursor_rect,
                }),
                TabEvent::ImeHide { id } => self.pending.push(BrowserEvent::ImeHide { tab: id }),
                TabEvent::ImeSelection {
                    id,
                    text,
                    cursor,
                    anchor,
                } => self.pending.push(BrowserEvent::ImeSelection {
                    tab: id,
                    text,
                    cursor,
                    anchor,
                }),
                TabEvent::Removed { .. } | TabEvent::Reset => {
                    self.pending.push(BrowserEvent::Tab(ev));
                    if self.tabs.borrow().private_count() == 0 {
                        for (tab, engine_id) in self.downloads.clear_private() {
                            self.cancel_engine_download(tab, engine_id);
                        }
                    }
                }
                other => self.pending.push(BrowserEvent::Tab(other)),
            }
        }
        if session_dirty && self.started {
            let snapshot = self.tabs.borrow().snapshot();
            self.session.schedule_save(snapshot);
            self.pending.push(BrowserEvent::SessionSaveRequested);
        }
        for ev in self.downloads.take_events() {
            self.pending.push(BrowserEvent::Download(ev));
        }
        std::mem::take(&mut self.pending)
    }

    fn cancel_engine_download(&self, tab: TabId, engine_id: u64) {
        if let Some(wv) = self
            .tabs
            .borrow()
            .by_id(tab)
            .and_then(|t| t.webview.clone())
        {
            wv.cancel_download(engine_id);
        }
    }

    pub fn cancel_download(&mut self, id: u64) {
        if let Some((tab, engine_id)) = self.downloads.cancel(id) {
            self.cancel_engine_download(tab, engine_id);
        }
    }

    pub fn remove_download(&mut self, id: u64) {
        if let Some((tab, engine_id)) = self.downloads.remove(id) {
            self.cancel_engine_download(tab, engine_id);
        }
    }

    fn push_engine_prefs(&self) {
        for (name, value) in self.prefs.engine_prefs() {
            self.engine.set_pref(&name, &value);
        }
    }

    /// Preferences changed in the UI: re-derive everything that depends on them.
    pub fn apply_prefs(&mut self) {
        self.prefs.normalize();
        let _ = self.prefs.save(&self.paths.prefs_file());
        self.push_engine_prefs();
        self.downloads
            .set_directory(self.prefs.download_dir(&self.paths.download_dir));
        self.tabs
            .borrow_mut()
            .set_max_live_webviews(self.prefs.max_live_webviews);
        self.perf.set_enabled(self.prefs.perf_logging);
        let ids: Vec<TabId> = self.tabs.borrow().iter().map(|t| t.id).collect();
        for id in ids {
            self.apply_cosmetic_filter(id);
        }
    }

    pub fn set_proxy(&mut self, proxy: ProxyConfig) {
        if proxy == self.proxy {
            return;
        }
        self.proxy = proxy;
        self.engine.set_proxy(&self.proxy);
        self.pending.push(BrowserEvent::ProxyChanged);
    }

    // ---- cosmetic filtering (spec 9.3) ------------------------------------

    pub fn reload_cosmetic_rules(&mut self) -> usize {
        self.filter.clear();
        let n = self.filter.load_dir(&self.paths.filters_dir());
        let ids: Vec<TabId> = self.tabs.borrow().iter().map(|t| t.id).collect();
        for id in ids {
            self.apply_cosmetic_filter(id);
        }
        n
    }

    fn apply_cosmetic_filter(&self, id: TabId) {
        let tabs = self.tabs.borrow();
        let Some(tab) = tabs.by_id(id) else { return };
        if !tab.has_webview() {
            return;
        }
        if !self.prefs.cosmetic_filtering || self.filter.is_empty() {
            tab.remove_user_stylesheet(COSMETIC_STYLESHEET_ID);
            return;
        }
        let css = self.filter.stylesheet_for(&tab.host(), 50);
        if css.is_empty() {
            tab.remove_user_stylesheet(COSMETIC_STYLESHEET_ID);
        } else {
            tab.set_user_stylesheet(COSMETIC_STYLESHEET_ID, &css);
        }
    }

    // ---- navigation -----------------------------------------------------------

    pub fn resolve_input(&self, input: &str) -> Option<String> {
        search::resolve(input, &self.prefs.search_engine)
    }

    /// Never mixes a private and a non-private document in one webview (7.3).
    pub fn open_url(&mut self, url: &str, private: bool, in_new_tab: bool) {
        if url.is_empty() {
            return;
        }
        let mut tabs = self.tabs.borrow_mut();
        let current = tabs.current().map(|t| (t.id, t.private));
        match current {
            Some((id, p)) if !in_new_tab && p == private => tabs.load(id, url),
            _ => {
                tabs.new_tab(url, private, true);
            }
        }
    }

    pub fn open_input(&mut self, input: &str, private: bool, in_new_tab: bool) {
        if let Some(url) = self.resolve_input(input) {
            self.open_url(&url, private, in_new_tab);
        }
    }

    // ---- privacy ----------------------------------------------------------------

    pub fn clear_browsing_data(
        &mut self,
        history: bool,
        cookies: bool,
        cache: bool,
        storage: bool,
        permissions: bool,
    ) {
        if history && self.history.clear() {
            self.pending.push(BrowserEvent::HistoryChanged);
        }
        let mut kinds = 0;
        if cookies {
            kinds |= site_data::COOKIES;
        }
        if cache {
            kinds |= site_data::HTTP_CACHE;
        }
        if storage {
            kinds |= site_data::LOCAL_STORAGE | site_data::SESSION_STORAGE;
        }
        if kinds != 0 {
            self.engine.clear_site_data(None, kinds);
        }
        if permissions {
            self.permissions.clear_all();
            self.pending.push(BrowserEvent::PermissionsChanged);
        }
    }

    /// Spec 7.3: private contexts never persist decisions.
    pub fn remember_permission(
        &mut self,
        origin: &str,
        kind: PermissionKind,
        allow: bool,
        private: bool,
    ) {
        if private {
            return;
        }
        self.permissions.set_decision(
            origin,
            kind,
            if allow {
                Decision::Allow
            } else {
                Decision::Deny
            },
        );
        self.pending.push(BrowserEvent::PermissionsChanged);
    }

    pub fn bookmark_toggle(&mut self, url: &str, title: &str) -> bool {
        let now_bookmarked = if self.bookmarks.contains(url) {
            self.bookmarks.remove(url);
            false
        } else {
            self.bookmarks.add(url, title)
        };
        self.pending.push(BrowserEvent::BookmarksChanged);
        now_bookmarked
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock::{mock_download_request, MockEngine};

    fn browser() -> (Rc<MockEngine>, Browser, tempfile::TempDir) {
        let dir = tempfile::tempdir().unwrap();
        let engine = MockEngine::new();
        let mut b = Browser::new(engine.clone(), AppPaths::under(dir.path()), None).unwrap();
        b.start(&["harbour-tuuli".into()]);
        (engine, b, dir)
    }

    fn spin_pump(engine: &Rc<MockEngine>, b: &mut Browser) -> Vec<BrowserEvent> {
        engine.spin_event_loop();
        b.pump()
    }

    #[test]
    fn start_writes_running_session_and_initialises_on_render() {
        let (engine, mut b, dir) = browser();
        assert!(dir.path().join("data/session.json").exists());
        assert!(!b.restored_after_crash());
        b.open_input("example.org", false, true);
        struct NoCtx;
        impl RenderingContext for NoCtx {
            fn size(&self) -> (u32, u32) {
                (1, 1)
            }
            fn framebuffer_object(&self) -> u32 {
                0
            }
            fn proc_address(&self, _: &str) -> *const std::ffi::c_void {
                std::ptr::null()
            }
            fn is_current(&self) -> bool {
                true
            }
            fn gl_version(&self) -> (u32, u32) {
                (3, 2)
            }
            fn is_gles(&self) -> bool {
                true
            }
        }
        b.initialize_engine(Rc::new(NoCtx)).unwrap();
        let events = spin_pump(&engine, &mut b);
        assert!(events
            .iter()
            .any(|e| matches!(e, BrowserEvent::EngineInitialized)));
        assert!(b.tabs.borrow().current().unwrap().has_webview());
        assert_eq!(
            engine
                .state
                .borrow()
                .prefs
                .get(crate::prefs::servo_prefs::SEND_DNT)
                .map(String::as_str),
            Some("true")
        );
        let events = spin_pump(&engine, &mut b);
        assert!(events
            .iter()
            .any(|e| matches!(e, BrowserEvent::HistoryChanged)));
        assert_eq!(b.history.total_count(), 1);
        assert_eq!(b.history.search("", 1)[0].title, "example.org");
    }

    #[test]
    fn session_restore_and_crash_detection() {
        let dir = tempfile::tempdir().unwrap();
        {
            let engine = MockEngine::new();
            let mut b = Browser::new(engine.clone(), AppPaths::under(dir.path()), None).unwrap();
            b.start(&["x".into()]);
            b.open_url("https://a.example/", false, true);
            b.open_url("https://b.example/", false, true);
            b.pump();
            b.on_about_to_quit();
        }
        {
            let engine = MockEngine::new();
            let mut b = Browser::new(engine.clone(), AppPaths::under(dir.path()), None).unwrap();
            b.start(&["x".into()]);
            assert!(!b.restored_after_crash());
            assert_eq!(b.tabs.borrow().len(), 2);
            assert_eq!(b.tabs.borrow().current_index(), Some(1));
            // Simulate a crash: no clean exit written.
        }
        {
            let engine = MockEngine::new();
            let mut b = Browser::new(engine, AppPaths::under(dir.path()), None).unwrap();
            b.start(&["x".into()]);
            assert!(b.restored_after_crash());
        }
    }

    #[test]
    fn permission_flow_denied_by_default_and_remembered() {
        let (engine, mut b, _dir) = browser();
        engine.initialize_for_tests();
        b.open_url("https://a.example/", false, true);
        spin_pump(&engine, &mut b);
        let wv = engine.webviews()[0].clone();
        let answers = Rc::new(RefCell::new(Vec::new()));
        let a = answers.clone();
        wv.push_event(WebViewEvent::Permission(PermissionRequest::new(
            PermissionKind::Geolocation,
            "https://a.example",
            move |ok| a.borrow_mut().push(ok),
        )));
        let events = spin_pump(&engine, &mut b);
        let prompt = events
            .into_iter()
            .find(|e| matches!(e, BrowserEvent::PermissionPrompt { .. }));
        assert!(prompt.is_some());
        drop(prompt); // UI dismissed it: denied
        assert_eq!(*answers.borrow(), vec![false]);

        b.remember_permission(
            "https://a.example",
            PermissionKind::Geolocation,
            true,
            false,
        );
        let a = answers.clone();
        wv.push_event(WebViewEvent::Permission(PermissionRequest::new(
            PermissionKind::Geolocation,
            "https://a.example",
            move |ok| a.borrow_mut().push(ok),
        )));
        let events = spin_pump(&engine, &mut b);
        assert!(
            !events
                .iter()
                .any(|e| matches!(e, BrowserEvent::PermissionPrompt { .. })),
            "answered from the store"
        );
        assert_eq!(*answers.borrow(), vec![false, true]);

        b.remember_permission("https://p.example", PermissionKind::Camera, true, true);
        assert_eq!(
            b.permissions
                .decision("https://p.example", PermissionKind::Camera),
            Decision::Ask
        );
    }

    #[test]
    fn downloads_flow_through_manager() {
        let (engine, mut b, dir) = browser();
        engine.initialize_for_tests();
        b.open_url("https://a.example/", false, true);
        spin_pump(&engine, &mut b);
        let wv = engine.webviews()[0].clone();
        wv.push_event(WebViewEvent::DownloadRequested(mock_download_request(
            &wv,
            5,
            "https://a.example/big.bin",
            "big.bin",
            "application/octet-stream",
            1000,
        )));
        let events = spin_pump(&engine, &mut b);
        assert!(events.iter().any(
            |e| matches!(e, BrowserEvent::DownloadStarted { file_name } if file_name == "big.bin")
        ));
        assert_eq!(b.downloads.len(), 1);
        assert!(b.downloads.items()[0]
            .path
            .starts_with(dir.path().join("downloads")));
        let events = spin_pump(&engine, &mut b);
        assert!(events
            .iter()
            .any(|e| matches!(e, BrowserEvent::Download(DownloadEvent::Finished(_)))));
        assert!(b.downloads.items()[0].ok);
    }

    #[test]
    fn cosmetic_filter_applies_per_host_and_pref() {
        let (engine, mut b, dir) = browser();
        std::fs::create_dir_all(dir.path().join("data/filters")).unwrap();
        std::fs::write(
            dir.path().join("data/filters/list.txt"),
            "##.ad\nexample.org##.promo\n",
        )
        .unwrap();
        assert_eq!(b.reload_cosmetic_rules(), 2);
        engine.initialize_for_tests();
        b.open_url("https://www.example.org/", false, true);
        spin_pump(&engine, &mut b);
        let wv = engine.webviews()[0].clone();
        let css = wv
            .state
            .borrow()
            .stylesheets
            .get(COSMETIC_STYLESHEET_ID)
            .cloned()
            .unwrap();
        assert!(css.contains(".ad") && css.contains(".promo"));
        b.prefs.cosmetic_filtering = false;
        b.apply_prefs();
        assert!(!wv
            .state
            .borrow()
            .stylesheets
            .contains_key(COSMETIC_STYLESHEET_ID));
    }

    #[test]
    fn open_url_respects_privacy_boundary() {
        let (_engine, mut b, _dir) = browser();
        b.open_url("https://a.example/", false, true);
        b.open_url("https://b.example/", false, false);
        assert_eq!(b.tabs.borrow().len(), 1, "same privacy: reuse current tab");
        b.open_url("https://p.example/", true, false);
        assert_eq!(b.tabs.borrow().len(), 2, "different privacy: new tab");
        assert!(b.tabs.borrow().current().unwrap().private);
        assert_eq!(
            b.resolve_input("jolla.com").as_deref(),
            Some("http://jolla.com/")
        );
        assert!(b
            .resolve_input("servo browser")
            .unwrap()
            .contains("duckduckgo"));
    }

    #[test]
    fn crash_flushes_session_and_reports() {
        let (engine, mut b, _dir) = browser();
        engine.push_crash("boom");
        let events = spin_pump(&engine, &mut b);
        assert!(events
            .iter()
            .any(|e| matches!(e, BrowserEvent::EngineCrashed { reason } if reason == "boom")));
        assert_eq!(b.engine_error(), Some("boom"));
    }
}
