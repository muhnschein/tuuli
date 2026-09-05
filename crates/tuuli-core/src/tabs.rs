// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tabs: state, ordering, the current tab, lazy engine webviews with a
//! live budget (spec 11), and session snapshots (spec 8.4).  Engine events
//! for a tab are routed here through the sink installed on its webview;
//! everything of interest to the UI is queued as a [`TabEvent`] and
//! drained by the Qt layer after each mutation and after each spin.

use std::cell::RefCell;
use std::rc::{Rc, Weak};
use std::time::Instant;

use crate::engine::*;
use crate::geometry::{Point, Rect, Size};
use crate::session::{Session, SessionTab};
use crate::useragent;

pub type TabId = u32;

pub struct Tab {
    pub id: TabId,
    pub private: bool,
    pub url: String,
    pub requested_url: String,
    /// `url` came from the engine, not from a request.
    pub committed: bool,
    pub title: String,
    pub loading: bool,
    pub can_go_back: bool,
    pub can_go_forward: bool,
    pub favicon: Option<RgbaImage>,
    pub favicon_revision: u32,
    pub thumbnail: Option<RgbaImage>,
    pub thumbnail_revision: u32,
    pub desktop_mode: bool,
    pub scroll: Point,
    pub pinch_zoom: f64,
    pub content_size: Size,
    pub last_active: Instant,
    pub webview: Option<Rc<dyn WebView>>,
    pending_find: Option<(String, bool)>,
}

impl Tab {
    fn new(id: TabId, private: bool) -> Self {
        Self {
            id,
            private,
            url: String::new(),
            requested_url: String::new(),
            committed: false,
            title: String::new(),
            loading: false,
            can_go_back: false,
            can_go_forward: false,
            favicon: None,
            favicon_revision: 0,
            thumbnail: None,
            thumbnail_revision: 0,
            desktop_mode: false,
            scroll: Point::default(),
            pinch_zoom: 1.0,
            content_size: Size::default(),
            last_active: Instant::now(),
            webview: None,
            pending_find: None,
        }
    }

    pub fn has_webview(&self) -> bool {
        self.webview.is_some()
    }

    /// Title, or the host, or the URL: what the chrome shows.
    pub fn display_title(&self) -> String {
        if !self.title.is_empty() {
            return self.title.clone();
        }
        let u = if self.url.is_empty() {
            &self.requested_url
        } else {
            &self.url
        };
        if u.is_empty() {
            return String::new();
        }
        url::Url::parse(u)
            .ok()
            .and_then(|p| p.host_str().map(|h| h.to_string()))
            .unwrap_or_else(|| u.clone())
    }

    pub fn effective_url(&self) -> &str {
        if self.url.is_empty() {
            &self.requested_url
        } else {
            &self.url
        }
    }

    pub fn host(&self) -> String {
        url::Url::parse(self.effective_url())
            .ok()
            .and_then(|u| u.host_str().map(|h| h.to_lowercase()))
            .unwrap_or_default()
    }

    /// Whether the content is scrolled to its top / bottom edge (pulley handoff).
    pub fn content_edges(&self, viewport_css_height: f64) -> (bool, bool) {
        let at_top = self.scroll.y <= 0.5;
        let at_bottom = self.content_size.height > 0.0
            && self.scroll.y + viewport_css_height >= self.content_size.height - 0.5;
        (at_top, at_bottom)
    }

    fn apply_desktop_mode(&self) {
        if let Some(wv) = &self.webview {
            if self.desktop_mode {
                wv.set_user_agent_override(Some(&useragent::desktop("", crate::VERSION)));
            } else {
                wv.set_user_agent_override(None);
            }
        }
    }

    pub fn reload(&self) {
        if let Some(wv) = &self.webview {
            wv.reload();
        }
    }
    pub fn stop(&self) {
        if let Some(wv) = &self.webview {
            wv.stop();
        }
    }
    pub fn go_back(&self) {
        if let Some(wv) = &self.webview {
            wv.go_back();
        }
    }
    pub fn go_forward(&self) {
        if let Some(wv) = &self.webview {
            wv.go_forward();
        }
    }
    pub fn find(&mut self, text: &str, case_sensitive: bool) {
        self.pending_find = Some((text.to_string(), case_sensitive));
        if let Some(wv) = &self.webview {
            wv.find(text, case_sensitive);
        }
    }
    pub fn find_next(&self, forward: bool) {
        if let Some(wv) = &self.webview {
            wv.find_next(forward);
        }
    }
    pub fn find_clear(&mut self) {
        self.pending_find = None;
        if let Some(wv) = &self.webview {
            wv.find_clear();
        }
    }
    pub fn set_user_stylesheet(&self, id: &str, css: &str) {
        if let Some(wv) = &self.webview {
            wv.add_user_stylesheet(id, css);
        }
    }
    pub fn remove_user_stylesheet(&self, id: &str) {
        if let Some(wv) = &self.webview {
            wv.remove_user_stylesheet(id);
        }
    }
}

/// What changed, for the Qt model and the browser glue.  `row` is the
/// position at the time of the event.
pub enum TabEvent {
    Inserted {
        row: usize,
        id: TabId,
    },
    Removed {
        row: usize,
        id: TabId,
    },
    Moved {
        from: usize,
        to: usize,
    },
    Reset,
    /// Displayed state changed (url, title, loading, favicon, thumbnail...).
    Changed {
        row: usize,
        id: TabId,
    },
    CurrentChanged,
    /// Something the session snapshot depends on changed.
    SessionDirty,
    /// The engine committed a navigation (history, filters).
    Navigation {
        id: TabId,
        url: String,
        private: bool,
    },
    TitleCommitted {
        id: TabId,
        url: String,
        title: String,
        private: bool,
    },
    LoadFinished {
        id: TabId,
    },
    FrameReady {
        id: TabId,
    },
    /// A webview was (re)created for the tab (filters need re-applying).
    WebViewAttached {
        id: TabId,
    },
    ImeShow {
        id: TabId,
        input_type: InputType,
        text: String,
        multiline: bool,
        cursor_rect: Rect,
    },
    ImeHide {
        id: TabId,
    },
    ImeSelection {
        id: TabId,
        text: String,
        cursor: usize,
        anchor: usize,
    },
    Permission {
        id: TabId,
        private: bool,
        request: PermissionRequest,
    },
    Dialog {
        id: TabId,
        private: bool,
        request: DialogRequest,
    },
    ContextMenu {
        id: TabId,
        info: ContextMenuInfo,
    },
    DownloadRequested {
        id: TabId,
        private: bool,
        request: DownloadRequest,
    },
    DownloadProgress {
        id: TabId,
        download: u64,
        received: i64,
        total: i64,
    },
    DownloadFinished {
        id: TabId,
        download: u64,
        ok: bool,
        error: String,
    },
    MediaSession {
        id: TabId,
        info: MediaSessionInfo,
    },
    Notification {
        id: TabId,
        title: String,
        body: String,
    },
    Viewport {
        id: TabId,
    },
}

impl std::fmt::Debug for TabEvent {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            TabEvent::Inserted { row, id } => write!(f, "Inserted(row={row}, id={id})"),
            TabEvent::Removed { row, id } => write!(f, "Removed(row={row}, id={id})"),
            TabEvent::Moved { from, to } => write!(f, "Moved({from}->{to})"),
            TabEvent::Reset => write!(f, "Reset"),
            TabEvent::Changed { row, id } => write!(f, "Changed(row={row}, id={id})"),
            TabEvent::CurrentChanged => write!(f, "CurrentChanged"),
            TabEvent::SessionDirty => write!(f, "SessionDirty"),
            TabEvent::Navigation { id, url, .. } => write!(f, "Navigation({id}, {url})"),
            TabEvent::TitleCommitted { id, title, .. } => {
                write!(f, "TitleCommitted({id}, {title})")
            }
            TabEvent::LoadFinished { id } => write!(f, "LoadFinished({id})"),
            TabEvent::FrameReady { id } => write!(f, "FrameReady({id})"),
            TabEvent::WebViewAttached { id } => write!(f, "WebViewAttached({id})"),
            TabEvent::ImeShow { id, .. } => write!(f, "ImeShow({id})"),
            TabEvent::ImeHide { id } => write!(f, "ImeHide({id})"),
            TabEvent::ImeSelection { id, .. } => write!(f, "ImeSelection({id})"),
            TabEvent::Permission { id, .. } => write!(f, "Permission({id})"),
            TabEvent::Dialog { id, .. } => write!(f, "Dialog({id})"),
            TabEvent::ContextMenu { id, .. } => write!(f, "ContextMenu({id})"),
            TabEvent::DownloadRequested { id, .. } => write!(f, "DownloadRequested({id})"),
            TabEvent::DownloadProgress { id, .. } => write!(f, "DownloadProgress({id})"),
            TabEvent::DownloadFinished { id, .. } => write!(f, "DownloadFinished({id})"),
            TabEvent::MediaSession { id, .. } => write!(f, "MediaSession({id})"),
            TabEvent::Notification { id, .. } => write!(f, "Notification({id})"),
            TabEvent::Viewport { id } => write!(f, "Viewport({id})"),
        }
    }
}

pub struct TabList {
    engine: Rc<dyn Engine>,
    tabs: Vec<Tab>,
    current: Option<usize>,
    next_id: TabId,
    max_live: usize,
    viewport: (u32, u32),
    dpr: f64,
    events: Vec<TabEvent>,
    weak_self: Weak<RefCell<TabList>>,
}

pub type SharedTabList = Rc<RefCell<TabList>>;

impl TabList {
    pub fn new_shared(engine: Rc<dyn Engine>) -> SharedTabList {
        let list = Rc::new(RefCell::new(TabList {
            engine,
            tabs: Vec::new(),
            current: None,
            next_id: 1,
            max_live: 8,
            viewport: (1080, 2260),
            dpr: 2.0,
            events: Vec::new(),
            weak_self: Weak::new(),
        }));
        list.borrow_mut().weak_self = Rc::downgrade(&list);
        list
    }

    pub fn take_events(&mut self) -> Vec<TabEvent> {
        std::mem::take(&mut self.events)
    }
    pub fn has_events(&self) -> bool {
        !self.events.is_empty()
    }

    pub fn len(&self) -> usize {
        self.tabs.len()
    }
    pub fn is_empty(&self) -> bool {
        self.tabs.is_empty()
    }
    pub fn private_count(&self) -> usize {
        self.tabs.iter().filter(|t| t.private).count()
    }
    pub fn current_index(&self) -> Option<usize> {
        self.current
    }
    pub fn current(&self) -> Option<&Tab> {
        self.current.and_then(|i| self.tabs.get(i))
    }
    pub fn current_mut(&mut self) -> Option<&mut Tab> {
        match self.current {
            Some(i) => self.tabs.get_mut(i),
            None => None,
        }
    }
    pub fn current_id(&self) -> Option<TabId> {
        self.current().map(|t| t.id)
    }
    pub fn get(&self, index: usize) -> Option<&Tab> {
        self.tabs.get(index)
    }
    pub fn get_mut(&mut self, index: usize) -> Option<&mut Tab> {
        self.tabs.get_mut(index)
    }
    pub fn by_id(&self, id: TabId) -> Option<&Tab> {
        self.tabs.iter().find(|t| t.id == id)
    }
    pub fn by_id_mut(&mut self, id: TabId) -> Option<&mut Tab> {
        self.tabs.iter_mut().find(|t| t.id == id)
    }
    pub fn index_of(&self, id: TabId) -> Option<usize> {
        self.tabs.iter().position(|t| t.id == id)
    }
    pub fn iter(&self) -> impl Iterator<Item = &Tab> {
        self.tabs.iter()
    }
    pub fn max_live_webviews(&self) -> usize {
        self.max_live
    }
    pub fn set_max_live_webviews(&mut self, n: usize) {
        self.max_live = n.max(1);
        let keep = self.current_id();
        self.trim_live(keep);
    }
    pub fn live_webview_count(&self) -> usize {
        self.tabs.iter().filter(|t| t.webview.is_some()).count()
    }

    /// Viewport geometry every new webview is created with; the view item
    /// keeps this current.
    pub fn set_viewport_geometry(&mut self, size: (u32, u32), dpr: f64) {
        if size.0 > 0 && size.1 > 0 {
            self.viewport = size;
        }
        if dpr > 0.0 {
            self.dpr = dpr;
        }
    }
    pub fn viewport(&self) -> (u32, u32) {
        self.viewport
    }
    pub fn dpr(&self) -> f64 {
        self.dpr
    }

    fn changed(&mut self, id: TabId) {
        if let Some(row) = self.index_of(id) {
            self.events.push(TabEvent::Changed { row, id });
        }
    }

    // ---- creating / closing -------------------------------------------

    pub fn new_tab(&mut self, url: &str, private: bool, activate: bool) -> TabId {
        let id = self.next_id;
        self.next_id += 1;
        let mut tab = Tab::new(id, private);
        if !url.is_empty() {
            tab.requested_url = url.to_string();
            tab.url = url.to_string();
        }
        let row = self.tabs.len();
        self.tabs.push(tab);
        self.events.push(TabEvent::Inserted { row, id });
        if activate {
            self.set_current(Some(row));
        } else {
            self.events.push(TabEvent::SessionDirty);
        }
        id
    }

    /// Adopt a webview the engine created itself (window.open).
    pub fn adopt_webview(&mut self, webview: Rc<dyn WebView>) -> TabId {
        let id = self.new_tab("", webview.is_private(), true);
        webview.set_client(self.sink_for(id));
        if let Some(tab) = self.by_id_mut(id) {
            tab.webview = Some(webview);
            tab.apply_desktop_mode();
        }
        self.events.push(TabEvent::WebViewAttached { id });
        id
    }

    pub fn close(&mut self, index: usize) {
        if index >= self.tabs.len() {
            return;
        }
        let mut tab = self.tabs.remove(index);
        let id = tab.id;
        if let Some(wv) = tab.webview.take() {
            wv.close();
        }
        self.events.push(TabEvent::Removed { row: index, id });

        let new_current = match self.current {
            None => None,
            Some(c) if self.tabs.is_empty() => {
                let _ = c;
                None
            }
            Some(c) if index < c => Some(c - 1),
            Some(c) if index == c => Some(index.min(self.tabs.len() - 1)),
            Some(c) => Some(c),
        };
        match (self.current, new_current) {
            (Some(c), Some(n)) if index < c && n == c - 1 => {
                // Same tab, shifted row.
                self.current = Some(n);
                self.events.push(TabEvent::CurrentChanged);
                self.events.push(TabEvent::SessionDirty);
            }
            (Some(c), Some(n)) if index == c => {
                self.current = None;
                let _ = n;
                self.set_current(Some(index.min(self.tabs.len() - 1)));
            }
            (_, None) => {
                self.current = None;
                self.events.push(TabEvent::CurrentChanged);
                self.events.push(TabEvent::SessionDirty);
            }
            _ => self.events.push(TabEvent::SessionDirty),
        }
    }

    pub fn close_by_id(&mut self, id: TabId) {
        if let Some(i) = self.index_of(id) {
            self.close(i);
        }
    }

    pub fn close_all(&mut self) {
        if self.tabs.is_empty() {
            return;
        }
        let old = std::mem::take(&mut self.tabs);
        self.current = None;
        for mut t in old {
            if let Some(wv) = t.webview.take() {
                wv.close();
            }
            self.events.push(TabEvent::Removed { row: 0, id: t.id });
        }
        self.events.push(TabEvent::Reset);
        self.events.push(TabEvent::CurrentChanged);
        self.events.push(TabEvent::SessionDirty);
    }

    pub fn close_all_private(&mut self) {
        let ids: Vec<TabId> = self
            .tabs
            .iter()
            .filter(|t| t.private)
            .map(|t| t.id)
            .collect();
        for id in ids {
            self.close_by_id(id);
        }
    }

    pub fn move_tab(&mut self, from: usize, to: usize) {
        let n = self.tabs.len();
        if from >= n || to >= n || from == to {
            return;
        }
        let tab = self.tabs.remove(from);
        self.tabs.insert(to, tab);
        if let Some(c) = self.current {
            self.current = Some(if c == from {
                to
            } else if from < c && to >= c {
                c - 1
            } else if from > c && to <= c {
                c + 1
            } else {
                c
            });
        }
        self.events.push(TabEvent::Moved { from, to });
        self.events.push(TabEvent::CurrentChanged);
        self.events.push(TabEvent::SessionDirty);
    }

    // ---- current ---------------------------------------------------------

    pub fn set_current(&mut self, index: Option<usize>) {
        if let Some(i) = index {
            if i >= self.tabs.len() {
                return;
            }
        }
        if index == self.current {
            return;
        }
        let old = self.current;
        self.current = index;
        if let Some(prev) = old.and_then(|i| self.tabs.get(i)) {
            if let Some(wv) = &prev.webview {
                wv.set_focused(false);
                wv.set_visible(false);
            }
        }
        if let Some(i) = index {
            let id = self.tabs[i].id;
            self.tabs[i].last_active = Instant::now();
            self.ensure_webview(id);
            if let Some(wv) = self.tabs[i].webview.as_ref() {
                wv.set_visible(true);
                wv.set_focused(true);
            }
        }
        if let Some(o) = old {
            if o < self.tabs.len() {
                let id = self.tabs[o].id;
                self.events.push(TabEvent::Changed { row: o, id });
            }
        }
        if let Some(i) = index {
            let id = self.tabs[i].id;
            self.events.push(TabEvent::Changed { row: i, id });
        }
        self.events.push(TabEvent::CurrentChanged);
        self.events.push(TabEvent::SessionDirty);
    }

    pub fn activate_id(&mut self, id: TabId) {
        if let Some(i) = self.index_of(id) {
            self.set_current(Some(i));
        }
    }

    // ---- webviews ---------------------------------------------------------

    fn sink_for(&self, id: TabId) -> EventSink {
        let weak = self.weak_self.clone();
        Rc::new(move |ev| {
            if let Some(list) = weak.upgrade() {
                list.borrow_mut().handle_webview_event(id, ev);
            }
        })
    }

    /// Materialise a webview for `id` if it has none, dropping least
    /// recently used ones over the budget.  Needs an initialised engine.
    pub fn ensure_webview(&mut self, id: TabId) -> bool {
        if !self.engine.is_initialized() {
            return false;
        }
        let Some(idx) = self.index_of(id) else {
            return false;
        };
        if self.tabs[idx].webview.is_some() {
            return true;
        }
        self.trim_live(Some(id));
        let private = self.tabs[idx].private;
        let sink = self.sink_for(id);
        let Some(wv) = self
            .engine
            .create_webview(sink, private, self.dpr, self.viewport)
        else {
            return false;
        };
        let tab = &mut self.tabs[idx];
        tab.webview = Some(wv.clone());
        tab.apply_desktop_mode();
        let target = if tab.requested_url.is_empty() {
            tab.url.clone()
        } else {
            tab.requested_url.clone()
        };
        if !target.is_empty() {
            wv.load(&target);
        }
        if let Some((text, cs)) = tab.pending_find.clone() {
            wv.find(&text, cs);
        }
        self.events.push(TabEvent::WebViewAttached { id });
        self.changed(id);
        true
    }

    /// Drop the engine webview but keep the tab's state (context loss,
    /// memory budget).  Re-materialised on activation.
    pub fn detach_webview(&mut self, id: TabId) {
        let Some(tab) = self.by_id_mut(id) else {
            return;
        };
        if let Some(wv) = tab.webview.take() {
            wv.close();
        }
        tab.requested_url = tab.url.clone();
        tab.committed = false;
        if tab.loading {
            tab.loading = false;
        }
        self.changed(id);
    }

    pub fn detach_all_webviews(&mut self) {
        let ids: Vec<TabId> = self
            .tabs
            .iter()
            .filter(|t| t.webview.is_some())
            .map(|t| t.id)
            .collect();
        for id in ids {
            self.detach_webview(id);
        }
    }

    fn trim_live(&mut self, keep: Option<TabId>) {
        let live = self.live_webview_count();
        let keep_needs_one = keep
            .and_then(|k| self.by_id(k))
            .map(|t| t.webview.is_none())
            .unwrap_or(false);
        let budget = self.max_live.max(1) - usize::from(keep_needs_one);
        if live <= budget {
            return;
        }
        let current = self.current_id();
        let mut candidates: Vec<(Instant, TabId)> = self
            .tabs
            .iter()
            .filter(|t| t.webview.is_some() && Some(t.id) != keep && Some(t.id) != current)
            .map(|t| (t.last_active, t.id))
            .collect();
        candidates.sort();
        let mut live = live;
        for (_, id) in candidates {
            if live <= budget {
                break;
            }
            self.detach_webview(id);
            live -= 1;
        }
    }

    /// Engine (re)initialised: make sure the current tab has a webview.
    pub fn on_engine_initialized(&mut self) {
        if let Some(id) = self.current_id() {
            self.ensure_webview(id);
            if let Some(wv) = self.by_id(id).and_then(|t| t.webview.clone()) {
                wv.set_visible(true);
                wv.set_focused(true);
            }
        }
    }

    /// Engine GL state gone (spec 5.2): views are gone; state stays.
    pub fn on_render_context_lost(&mut self) {
        self.detach_all_webviews();
    }

    // ---- user actions -----------------------------------------------------

    pub fn load(&mut self, id: TabId, url: &str) {
        if url.is_empty() {
            return;
        }
        let Some(tab) = self.by_id_mut(id) else {
            return;
        };
        tab.requested_url = url.to_string();
        tab.committed = false;
        if let Some(wv) = &tab.webview {
            wv.load(url);
        } else {
            tab.url = url.to_string();
            tab.title.clear();
            self.changed(id);
        }
    }

    pub fn set_desktop_mode(&mut self, id: TabId, on: bool) {
        let Some(tab) = self.by_id_mut(id) else {
            return;
        };
        if tab.desktop_mode == on {
            return;
        }
        tab.desktop_mode = on;
        tab.apply_desktop_mode();
        tab.reload();
        self.changed(id);
        self.events.push(TabEvent::SessionDirty);
    }

    pub fn set_thumbnail(&mut self, id: TabId, image: RgbaImage) {
        let Some(tab) = self.by_id_mut(id) else {
            return;
        };
        tab.thumbnail = Some(image);
        tab.thumbnail_revision += 1;
        self.changed(id);
    }

    pub fn capture_thumbnail(&mut self, id: TabId) {
        let img = self
            .by_id(id)
            .and_then(|t| t.webview.as_ref())
            .and_then(|wv| wv.capture());
        if let Some(img) = img {
            self.set_thumbnail(id, img);
        }
    }

    // ---- engine events -----------------------------------------------------

    fn handle_webview_event(&mut self, id: TabId, ev: WebViewEvent) {
        let Some(row) = self.index_of(id) else { return };
        let private = self.tabs[row].private;
        match ev {
            WebViewEvent::UrlChanged(url) => {
                let tab = &mut self.tabs[row];
                let changed_for_ui = tab.url != url;
                let navigation = changed_for_ui || !tab.committed;
                tab.url = url.clone();
                tab.committed = true;
                tab.requested_url.clear();
                if changed_for_ui {
                    self.events.push(TabEvent::Changed { row, id });
                    self.events.push(TabEvent::SessionDirty);
                }
                if navigation {
                    self.events.push(TabEvent::Navigation { id, url, private });
                }
            }
            WebViewEvent::TitleChanged(title) => {
                let tab = &mut self.tabs[row];
                if tab.title != title {
                    tab.title = title.clone();
                    let url = tab.url.clone();
                    self.events.push(TabEvent::Changed { row, id });
                    self.events.push(TabEvent::SessionDirty);
                    self.events.push(TabEvent::TitleCommitted {
                        id,
                        url,
                        title,
                        private,
                    });
                }
            }
            WebViewEvent::LoadStatus(status) => {
                let loading = status != LoadStatus::Complete;
                let tab = &mut self.tabs[row];
                if tab.loading != loading {
                    tab.loading = loading;
                    self.events.push(TabEvent::Changed { row, id });
                }
                if status == LoadStatus::Complete {
                    // Restored viewport is applied once the page has content.
                    if let Some(wv) = &tab.webview {
                        if tab.pinch_zoom != 1.0 {
                            wv.set_pinch_zoom(tab.pinch_zoom);
                        }
                        if !tab.scroll.is_zero() {
                            wv.scroll_to(tab.scroll);
                        }
                    }
                    self.events.push(TabEvent::LoadFinished { id });
                }
            }
            WebViewEvent::Favicon(img) => {
                let tab = &mut self.tabs[row];
                tab.favicon = Some(img);
                tab.favicon_revision += 1;
                self.events.push(TabEvent::Changed { row, id });
            }
            WebViewEvent::History {
                can_go_back,
                can_go_forward,
            } => {
                let tab = &mut self.tabs[row];
                if tab.can_go_back != can_go_back || tab.can_go_forward != can_go_forward {
                    tab.can_go_back = can_go_back;
                    tab.can_go_forward = can_go_forward;
                    self.events.push(TabEvent::Changed { row, id });
                }
            }
            WebViewEvent::FrameReady => self.events.push(TabEvent::FrameReady { id }),
            WebViewEvent::Viewport {
                scroll,
                zoom,
                content,
            } => {
                let tab = &mut self.tabs[row];
                tab.scroll = scroll;
                tab.pinch_zoom = zoom;
                tab.content_size = content;
                self.events.push(TabEvent::Viewport { id });
                self.events.push(TabEvent::SessionDirty);
            }
            WebViewEvent::ImeShow {
                input_type,
                text,
                multiline,
                cursor_rect,
            } => self.events.push(TabEvent::ImeShow {
                id,
                input_type,
                text,
                multiline,
                cursor_rect,
            }),
            WebViewEvent::ImeHide => self.events.push(TabEvent::ImeHide { id }),
            WebViewEvent::ImeSelection {
                text,
                cursor,
                anchor,
            } => self.events.push(TabEvent::ImeSelection {
                id,
                text,
                cursor,
                anchor,
            }),
            WebViewEvent::Permission(request) => self.events.push(TabEvent::Permission {
                id,
                private,
                request,
            }),
            WebViewEvent::Dialog(request) => self.events.push(TabEvent::Dialog {
                id,
                private,
                request,
            }),
            WebViewEvent::ContextMenu(info) => self.events.push(TabEvent::ContextMenu { id, info }),
            WebViewEvent::DownloadRequested(request) => {
                self.events.push(TabEvent::DownloadRequested {
                    id,
                    private,
                    request,
                })
            }
            WebViewEvent::DownloadProgress {
                id: download,
                received,
                total,
            } => self.events.push(TabEvent::DownloadProgress {
                id,
                download,
                received,
                total,
            }),
            WebViewEvent::DownloadFinished {
                id: download,
                ok,
                error,
            } => self.events.push(TabEvent::DownloadFinished {
                id,
                download,
                ok,
                error,
            }),
            WebViewEvent::MediaSession(info) => {
                self.events.push(TabEvent::MediaSession { id, info })
            }
            WebViewEvent::Notification { title, body, .. } => {
                self.events.push(TabEvent::Notification { id, title, body })
            }
            WebViewEvent::NewWebViewRequested { url } => {
                self.new_tab(url.as_deref().unwrap_or(""), private, true);
            }
            WebViewEvent::Closed => {
                self.close_by_id(id);
            }
        }
    }

    // ---- session ------------------------------------------------------------

    /// Private tabs are excluded (spec 7.3).
    pub fn snapshot(&self) -> Session {
        let mut s = Session::default();
        let mut current = None;
        for (i, t) in self.tabs.iter().enumerate() {
            if t.private {
                continue;
            }
            let url = t.effective_url();
            if url.is_empty() {
                continue;
            }
            if Some(i) == self.current {
                current = Some(s.tabs.len());
            }
            s.tabs.push(SessionTab {
                url: url.to_string(),
                title: t.title.clone(),
                scroll_x: t.scroll.x,
                scroll_y: t.scroll.y,
                zoom: t.pinch_zoom,
                desktop_mode: t.desktop_mode,
            });
        }
        s.current_index = current.or(if s.tabs.is_empty() { None } else { Some(0) });
        s
    }

    pub fn restore(&mut self, session: &Session) {
        for st in &session.tabs {
            let id = self.new_tab(&st.url, false, false);
            if let Some(tab) = self.by_id_mut(id) {
                tab.title = st.title.clone();
                tab.scroll = Point::new(st.scroll_x, st.scroll_y);
                tab.pinch_zoom = if st.zoom > 0.0 { st.zoom } else { 1.0 };
                tab.desktop_mode = st.desktop_mode;
            }
        }
        if !session.tabs.is_empty() {
            let idx = session.current_index.unwrap_or(0).min(self.tabs.len() - 1);
            self.set_current(Some(idx));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock::MockEngine;

    fn setup() -> (Rc<MockEngine>, SharedTabList) {
        let engine = MockEngine::new();
        engine.initialize_for_tests();
        let tabs = TabList::new_shared(engine.clone());
        tabs.borrow_mut().set_viewport_geometry((1080, 2260), 2.5);
        (engine, tabs)
    }

    fn spin(engine: &Rc<MockEngine>) {
        engine.spin_event_loop();
    }

    #[test]
    fn new_tab_activates_and_creates_webview() {
        let (engine, tabs) = setup();
        let id = tabs
            .borrow_mut()
            .new_tab("https://example.org/", false, true);
        {
            let t = tabs.borrow();
            assert_eq!(t.len(), 1);
            assert_eq!(t.current_index(), Some(0));
            assert!(t.by_id(id).unwrap().has_webview());
        }
        let wv = engine.webviews()[0].clone();
        {
            let s = wv.state.borrow();
            assert_eq!(s.size, (1080, 2260));
            assert_eq!(s.dpr, 2.5);
            assert!(s.visible && s.focused);
            assert_eq!(s.url, "https://example.org/");
        }
        spin(&engine);
        let t = tabs.borrow();
        let tab = t.by_id(id).unwrap();
        assert_eq!(tab.title, "example.org");
        assert!(!tab.loading);
        assert!(tab.committed);
        assert_eq!(tab.display_title(), "example.org");
    }

    #[test]
    fn tabs_are_lazy_without_engine() {
        let engine = MockEngine::new();
        let tabs = TabList::new_shared(engine.clone());
        let id = tabs
            .borrow_mut()
            .new_tab("https://example.org/", false, true);
        assert!(!tabs.borrow().by_id(id).unwrap().has_webview());
        assert_eq!(tabs.borrow().by_id(id).unwrap().url, "https://example.org/");
        engine.initialize_for_tests();
        tabs.borrow_mut().on_engine_initialized();
        assert!(tabs.borrow().by_id(id).unwrap().has_webview());
    }

    #[test]
    fn switching_tabs_toggles_visibility() {
        let (engine, tabs) = setup();
        tabs.borrow_mut().new_tab("https://a.example/", false, true);
        tabs.borrow_mut().new_tab("https://b.example/", false, true);
        let views = engine.webviews();
        assert!(!views[0].state.borrow().visible);
        assert!(views[1].state.borrow().visible);
        tabs.borrow_mut().set_current(Some(0));
        assert!(views[0].state.borrow().visible);
        assert!(!views[1].state.borrow().visible);
    }

    #[test]
    fn close_picks_neighbour_and_keeps_current_when_earlier_closed() {
        let (_engine, tabs) = setup();
        let a = tabs.borrow_mut().new_tab("https://a.example/", false, true);
        let _b = tabs.borrow_mut().new_tab("https://b.example/", false, true);
        let c = tabs.borrow_mut().new_tab("https://c.example/", false, true);
        tabs.borrow_mut().set_current(Some(1));
        tabs.borrow_mut().close(1);
        assert_eq!(tabs.borrow().len(), 2);
        assert_eq!(tabs.borrow().current_id(), Some(c));
        tabs.borrow_mut().close(1);
        assert_eq!(tabs.borrow().current_id(), Some(a));
        tabs.borrow_mut().close(0);
        assert_eq!(tabs.borrow().len(), 0);
        assert_eq!(tabs.borrow().current_index(), None);

        let a = tabs.borrow_mut().new_tab("https://a.example/", false, true);
        let b = tabs.borrow_mut().new_tab("https://b.example/", false, true);
        tabs.borrow_mut().close(0);
        assert_ne!(tabs.borrow().current_id(), Some(a));
        assert_eq!(tabs.borrow().current_id(), Some(b));
        assert_eq!(tabs.borrow().current_index(), Some(0));
    }

    #[test]
    fn move_tab_tracks_current() {
        let (_engine, tabs) = setup();
        let a = tabs.borrow_mut().new_tab("https://a.example/", false, true);
        let b = tabs.borrow_mut().new_tab("https://b.example/", false, true);
        let c = tabs.borrow_mut().new_tab("https://c.example/", false, true);
        tabs.borrow_mut().set_current(Some(0));
        tabs.borrow_mut().move_tab(0, 2);
        let t = tabs.borrow();
        assert_eq!(t.get(0).unwrap().id, b);
        assert_eq!(t.get(1).unwrap().id, c);
        assert_eq!(t.get(2).unwrap().id, a);
        assert_eq!(t.current_index(), Some(2));
        drop(t);
        tabs.borrow_mut().move_tab(2, 0);
        assert_eq!(tabs.borrow().current_index(), Some(0));
    }

    #[test]
    fn private_tabs_are_excluded_from_snapshot() {
        let (engine, tabs) = setup();
        tabs.borrow_mut().new_tab("https://a.example/", false, true);
        let p = tabs
            .borrow_mut()
            .new_tab("https://secret.example/", true, true);
        tabs.borrow_mut().new_tab("https://c.example/", false, true);
        assert!(tabs.borrow().by_id(p).unwrap().private);
        assert!(engine.webviews()[1].is_private());
        assert_eq!(tabs.borrow().private_count(), 1);
        tabs.borrow_mut().set_current(Some(2));
        let s = tabs.borrow().snapshot();
        assert_eq!(s.tabs.len(), 2);
        assert_eq!(s.tabs[0].url, "https://a.example/");
        assert_eq!(s.tabs[1].url, "https://c.example/");
        assert_eq!(s.current_index, Some(1));
        tabs.borrow_mut().set_current(Some(1));
        assert_eq!(tabs.borrow().snapshot().current_index, Some(0));
        tabs.borrow_mut().close_all_private();
        assert_eq!(tabs.borrow().len(), 2);
        assert_eq!(tabs.borrow().private_count(), 0);
    }

    #[test]
    fn restore_recreates_tabs_and_state() {
        let engine = MockEngine::new();
        let tabs = TabList::new_shared(engine.clone());
        let session = Session {
            tabs: vec![
                SessionTab {
                    url: "https://a.example/".into(),
                    title: "A".into(),
                    scroll_x: 0.0,
                    scroll_y: 300.0,
                    zoom: 2.0,
                    desktop_mode: false,
                },
                SessionTab {
                    url: "https://b.example/".into(),
                    title: String::new(),
                    scroll_x: 0.0,
                    scroll_y: 0.0,
                    zoom: 1.0,
                    desktop_mode: true,
                },
            ],
            current_index: Some(1),
            clean_exit: false,
        };
        tabs.borrow_mut().restore(&session);
        {
            let t = tabs.borrow();
            assert_eq!(t.len(), 2);
            assert_eq!(t.current_index(), Some(1));
            assert_eq!(t.get(0).unwrap().title, "A");
            assert_eq!(t.get(0).unwrap().scroll, Point::new(0.0, 300.0));
            assert!(t.get(1).unwrap().desktop_mode);
            assert!(!t.get(0).unwrap().has_webview());
        }
        engine.initialize_for_tests();
        tabs.borrow_mut().on_engine_initialized();
        assert!(tabs.borrow().get(1).unwrap().has_webview());
        assert!(!tabs.borrow().get(0).unwrap().has_webview());
        assert_eq!(engine.webviews().len(), 1);
        assert!(
            engine.webviews()[0].state.borrow().user_agent.is_some(),
            "desktop UA applied"
        );

        tabs.borrow_mut().set_current(Some(0));
        assert!(tabs.borrow().get(0).unwrap().has_webview());
        spin(&engine);
        let wv = engine.webviews().last().unwrap().clone();
        assert_eq!(wv.state.borrow().pinch_zoom, 2.0);
        assert_eq!(wv.state.borrow().scroll, Point::new(0.0, 300.0));
    }

    #[test]
    fn live_webview_budget_drops_least_recently_used() {
        let (_engine, tabs) = setup();
        tabs.borrow_mut().set_max_live_webviews(2);
        let a = tabs.borrow_mut().new_tab("https://a.example/", false, true);
        std::thread::sleep(std::time::Duration::from_millis(2));
        let b = tabs.borrow_mut().new_tab("https://b.example/", false, true);
        std::thread::sleep(std::time::Duration::from_millis(2));
        let c = tabs.borrow_mut().new_tab("https://c.example/", false, true);
        {
            let t = tabs.borrow();
            assert_eq!(t.live_webview_count(), 2);
            assert!(!t.by_id(a).unwrap().has_webview());
            assert!(t.by_id(b).unwrap().has_webview());
            assert!(t.by_id(c).unwrap().has_webview());
            assert_eq!(t.by_id(a).unwrap().url, "https://a.example/");
        }
        tabs.borrow_mut().set_current(Some(0));
        let t = tabs.borrow();
        assert!(t.by_id(a).unwrap().has_webview());
        assert!(!t.by_id(b).unwrap().has_webview());
        assert_eq!(t.live_webview_count(), 2);
    }

    #[test]
    fn render_context_loss_detaches_and_recreates() {
        let (engine, tabs) = setup();
        let a = tabs.borrow_mut().new_tab("https://a.example/", false, true);
        spin(&engine);
        assert_eq!(tabs.borrow().by_id(a).unwrap().title, "a.example");
        engine.shutdown();
        tabs.borrow_mut().on_render_context_lost();
        {
            let t = tabs.borrow();
            let tab = t.by_id(a).unwrap();
            assert!(!tab.has_webview());
            assert_eq!(tab.url, "https://a.example/");
            assert_eq!(tab.title, "a.example");
        }
        engine.initialize_for_tests();
        tabs.borrow_mut().on_engine_initialized();
        assert!(tabs.borrow().by_id(a).unwrap().has_webview());
        assert_eq!(
            engine.webviews().last().unwrap().state.borrow().url,
            "https://a.example/"
        );
    }

    #[test]
    fn engine_events_update_state_and_queue_events() {
        let (engine, tabs) = setup();
        let id = tabs
            .borrow_mut()
            .new_tab("https://a.example/path", false, true);
        assert!(
            !tabs.borrow().by_id(id).unwrap().loading,
            "nothing until the engine spins"
        );
        spin(&engine);
        assert!(
            !tabs.borrow().by_id(id).unwrap().loading,
            "mock completes within one spin"
        );
        let events = tabs.borrow_mut().take_events();
        assert!(events.iter().any(
            |e| matches!(e, TabEvent::Navigation { url, .. } if url == "https://a.example/path")
        ));
        assert!(events
            .iter()
            .any(|e| matches!(e, TabEvent::TitleCommitted { title, .. } if title == "a.example")));
        assert!(events
            .iter()
            .any(|e| matches!(e, TabEvent::LoadFinished { .. })));
        assert!(events
            .iter()
            .any(|e| matches!(e, TabEvent::FrameReady { .. })));

        let wv = engine.webviews()[0].clone();
        wv.push_event(WebViewEvent::LoadStatus(LoadStatus::Started));
        spin(&engine);
        assert!(tabs.borrow().by_id(id).unwrap().loading);
        wv.push_event(WebViewEvent::LoadStatus(LoadStatus::Complete));
        wv.push_event(WebViewEvent::History {
            can_go_back: true,
            can_go_forward: false,
        });
        wv.push_event(WebViewEvent::Favicon(RgbaImage::solid(
            16,
            16,
            [255, 0, 0, 255],
        )));
        wv.push_event(WebViewEvent::Viewport {
            scroll: Point::new(0.0, 50.0),
            zoom: 1.5,
            content: Size::new(540.0, 4000.0),
        });
        spin(&engine);
        let mut t = tabs.borrow_mut();
        let events = t.take_events();
        assert!(events
            .iter()
            .any(|e| matches!(e, TabEvent::Viewport { .. })));
        let tab = t.by_id(id).unwrap();
        assert!(!tab.loading);
        assert!(tab.can_go_back && !tab.can_go_forward);
        assert!(tab.favicon.is_some());
        assert_eq!(tab.favicon_revision, 1);
        assert_eq!(tab.content_edges(730.0), (false, false));
        assert_eq!(tab.pinch_zoom, 1.5);
    }

    #[test]
    fn requested_url_counts_as_navigation_once_committed() {
        let (engine, tabs) = setup();
        tabs.borrow_mut().new_tab("https://a.example/", false, true);
        tabs.borrow_mut().take_events();
        spin(&engine);
        let events = tabs.borrow_mut().take_events();
        let navs = events
            .iter()
            .filter(|e| matches!(e, TabEvent::Navigation { .. }))
            .count();
        assert_eq!(
            navs, 1,
            "the engine's first report of the requested URL is a navigation"
        );
    }

    #[test]
    fn new_webview_request_and_adoption() {
        let (engine, tabs) = setup();
        tabs.borrow_mut().new_tab("https://a.example/", true, true);
        spin(&engine);
        engine.webviews()[0].push_event(WebViewEvent::NewWebViewRequested {
            url: Some("https://popup.example/".into()),
        });
        spin(&engine);
        {
            let t = tabs.borrow();
            assert_eq!(t.len(), 2);
            assert!(t.current().unwrap().private, "inherits privacy");
            assert_eq!(t.current().unwrap().url, "https://popup.example/");
        }
        let aux = engine.push_auxiliary_webview(false);
        let id = tabs.borrow_mut().adopt_webview(aux.clone());
        aux.push_event(WebViewEvent::TitleChanged("adopted".into()));
        spin(&engine);
        assert_eq!(tabs.borrow().by_id(id).unwrap().title, "adopted");
    }
}
