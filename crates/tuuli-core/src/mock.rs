// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! In-process fake engine.  Used by the tests and by builds without the
//! `servo` feature so the Silica chrome can be iterated on a host, in the
//! emulator, or on a device without a libservo build.  It "loads" pages by
//! echoing the URL back as the title, paints nothing (the item draws a
//! placeholder) and records every input it receives.  Like the real
//! engine it delivers events only from inside [`Engine::spin_event_loop`].

use std::cell::{Cell, RefCell};
use std::collections::HashMap;
use std::path::PathBuf;
use std::rc::{Rc, Weak};

use crate::engine::*;
use crate::geometry::{Point, Rect};
use crate::input::TouchPhase;
use crate::proxy::ProxyConfig;

#[derive(Clone, Debug, PartialEq)]
pub struct RecordedTouch {
    pub phase: TouchPhase,
    pub id: i32,
    pub css: Point,
}

#[derive(Default)]
pub struct MockWebViewState {
    pub url: String,
    pub history: Vec<String>,
    pub history_index: Option<usize>,
    pub size: (u32, u32),
    pub viewport: Rect,
    pub dpr: f64,
    pub pinch_zoom: f64,
    pub page_zoom: f64,
    pub scroll: Point,
    pub visible: bool,
    pub focused: bool,
    pub closed: bool,
    pub touches: Vec<RecordedTouch>,
    pub keys: Vec<(bool, String)>,
    pub compositions: Vec<String>,
    pub actions: Vec<EditingAction>,
    pub scripts: Vec<String>,
    pub stylesheets: HashMap<String, String>,
    pub user_agent: Option<String>,
    pub find_text: String,
    pub find_next_count: u32,
    pub ime_dismiss_count: u32,
    pub paint_count: u32,
    pub load_count: u32,
    pub reload_count: u32,
    pub context_menu_requested_at: Option<Point>,
    pub cancelled_downloads: Vec<u64>,
}

pub struct MockWebView {
    engine: Weak<MockEngine>,
    private: bool,
    sink: RefCell<Option<EventSink>>,
    pub state: RefCell<MockWebViewState>,
    queue: RefCell<Vec<WebViewEvent>>,
}

impl MockWebView {
    fn queue(&self, ev: WebViewEvent) {
        self.queue.borrow_mut().push(ev);
        if let Some(e) = self.engine.upgrade() {
            e.wake();
        }
    }

    /// Test helper: push an event as if the engine produced it.  Delivered
    /// on the next spin.
    pub fn push_event(&self, ev: WebViewEvent) {
        self.queue(ev);
    }

    fn deliver(&self) {
        let events: Vec<WebViewEvent> = std::mem::take(&mut *self.queue.borrow_mut());
        if events.is_empty() {
            return;
        }
        let sink = self.sink.borrow().clone();
        if let Some(sink) = sink {
            for ev in events {
                sink(ev);
            }
        }
    }

    fn complete_load(&self) {
        let (url, back, forward) = {
            let s = self.state.borrow();
            let idx = s.history_index.unwrap_or(0);
            (s.url.clone(), idx > 0, idx + 1 < s.history.len())
        };
        let title = url::Url::parse(&url)
            .ok()
            .and_then(|u| u.host_str().map(|h| h.to_string()))
            .unwrap_or(url);
        self.queue(WebViewEvent::TitleChanged(title));
        self.queue(WebViewEvent::History {
            can_go_back: back,
            can_go_forward: forward,
        });
        self.queue(WebViewEvent::LoadStatus(LoadStatus::Complete));
        self.queue(WebViewEvent::FrameReady);
    }
}

impl WebView for MockWebView {
    fn is_private(&self) -> bool {
        self.private
    }
    fn set_client(&self, sink: EventSink) {
        *self.sink.borrow_mut() = Some(sink);
    }
    fn load(&self, url: &str) {
        {
            let mut s = self.state.borrow_mut();
            s.load_count += 1;
            s.url = url.to_string();
            if let Some(i) = s.history_index {
                s.history.truncate(i + 1);
            }
            s.history.push(url.to_string());
            s.history_index = Some(s.history.len() - 1);
        }
        self.queue(WebViewEvent::LoadStatus(LoadStatus::Started));
        self.queue(WebViewEvent::UrlChanged(url.to_string()));
        self.complete_load();
    }
    fn reload(&self) {
        self.state.borrow_mut().reload_count += 1;
        self.queue(WebViewEvent::LoadStatus(LoadStatus::Started));
        self.complete_load();
    }
    fn stop(&self) {
        self.queue(WebViewEvent::LoadStatus(LoadStatus::Complete));
    }
    fn go_back(&self) {
        let url = {
            let mut s = self.state.borrow_mut();
            match s.history_index {
                Some(i) if i > 0 => {
                    s.history_index = Some(i - 1);
                    s.url = s.history[i - 1].clone();
                    Some((s.url.clone(), i - 1 > 0, true))
                }
                _ => None,
            }
        };
        if let Some((url, back, forward)) = url {
            self.queue(WebViewEvent::UrlChanged(url));
            self.queue(WebViewEvent::History {
                can_go_back: back,
                can_go_forward: forward,
            });
        }
    }
    fn go_forward(&self) {
        let url = {
            let mut s = self.state.borrow_mut();
            match s.history_index {
                Some(i) if i + 1 < s.history.len() => {
                    s.history_index = Some(i + 1);
                    s.url = s.history[i + 1].clone();
                    Some((s.url.clone(), true, i + 2 < s.history.len()))
                }
                _ => None,
            }
        };
        if let Some((url, back, forward)) = url {
            self.queue(WebViewEvent::UrlChanged(url));
            self.queue(WebViewEvent::History {
                can_go_back: back,
                can_go_forward: forward,
            });
        }
    }
    fn set_visible(&self, visible: bool) {
        self.state.borrow_mut().visible = visible;
    }
    fn set_focused(&self, focused: bool) {
        self.state.borrow_mut().focused = focused;
    }
    fn set_size(&self, width: u32, height: u32) {
        self.state.borrow_mut().size = (width, height);
    }
    fn set_viewport_rect(&self, rect: Rect) {
        self.state.borrow_mut().viewport = rect;
    }
    fn set_device_pixel_ratio(&self, dpr: f64) {
        self.state.borrow_mut().dpr = dpr;
    }
    fn set_pinch_zoom(&self, zoom: f64) {
        self.state.borrow_mut().pinch_zoom = zoom;
    }
    fn set_page_zoom(&self, zoom: f64) {
        self.state.borrow_mut().page_zoom = zoom;
    }
    fn scroll_to(&self, css: Point) {
        self.state.borrow_mut().scroll = css;
    }
    fn touch(&self, phase: TouchPhase, id: i32, css: Point) {
        self.state
            .borrow_mut()
            .touches
            .push(RecordedTouch { phase, id, css });
    }
    fn key(&self, down: bool, key: &str, _modifiers: u32) {
        self.state.borrow_mut().keys.push((down, key.to_string()));
    }
    fn ime_composition(&self, _state: CompositionState, text: &str) {
        self.state.borrow_mut().compositions.push(text.to_string());
    }
    fn ime_dismissed(&self) {
        self.state.borrow_mut().ime_dismiss_count += 1;
    }
    fn editing_action(&self, action: EditingAction) {
        self.state.borrow_mut().actions.push(action);
    }
    fn request_context_menu(&self, css: Point) {
        self.state.borrow_mut().context_menu_requested_at = Some(css);
        self.queue(WebViewEvent::ContextMenu(ContextMenuInfo {
            css,
            ..Default::default()
        }));
    }
    fn find(&self, text: &str, _case_sensitive: bool) {
        self.state.borrow_mut().find_text = text.to_string();
    }
    fn find_next(&self, _forward: bool) {
        self.state.borrow_mut().find_next_count += 1;
    }
    fn find_clear(&self) {
        self.state.borrow_mut().find_text.clear();
    }
    fn add_user_stylesheet(&self, id: &str, css: &str) {
        self.state
            .borrow_mut()
            .stylesheets
            .insert(id.to_string(), css.to_string());
    }
    fn remove_user_stylesheet(&self, id: &str) {
        self.state.borrow_mut().stylesheets.remove(id);
    }
    fn set_user_agent_override(&self, ua: Option<&str>) {
        self.state.borrow_mut().user_agent = ua.map(|s| s.to_string());
    }
    fn evaluate_javascript(&self, script: &str) {
        self.state.borrow_mut().scripts.push(script.to_string());
    }
    fn capture(&self) -> Option<RgbaImage> {
        let (w, h) = self.state.borrow().size;
        Some(RgbaImage::solid(
            (w / 8).max(1),
            (h / 8).max(1),
            [0x2b, 0x2b, 0x2b, 0xff],
        ))
    }
    fn cancel_download(&self, id: u64) {
        self.state.borrow_mut().cancelled_downloads.push(id);
        self.queue(WebViewEvent::DownloadFinished {
            id,
            ok: false,
            error: "cancelled".into(),
        });
    }
    fn paint(&self) -> bool {
        self.state.borrow_mut().paint_count += 1;
        false
    }
    fn close(&self) {
        self.state.borrow_mut().closed = true;
        *self.sink.borrow_mut() = None;
    }
}

/// Simulates a download the engine would perform: accepting it completes
/// it on the following spins.
pub fn mock_download_request(
    wv: &Rc<MockWebView>,
    id: u64,
    url: &str,
    name: &str,
    mime: &str,
    total: i64,
) -> DownloadRequest {
    let weak = Rc::downgrade(wv);
    DownloadRequest::new(id, url, name, mime, total, move |dest: Option<PathBuf>| {
        if let (Some(wv), Some(_)) = (weak.upgrade(), dest) {
            let half = if total > 0 { total / 2 } else { 512 };
            let all = if total > 0 { total } else { 1024 };
            wv.push_event(WebViewEvent::DownloadProgress {
                id,
                received: half,
                total,
            });
            wv.push_event(WebViewEvent::DownloadProgress {
                id,
                received: all,
                total,
            });
            wv.push_event(WebViewEvent::DownloadFinished {
                id,
                ok: true,
                error: String::new(),
            });
        }
    })
}

#[derive(Default)]
pub struct MockEngineState {
    pub prefs: HashMap<String, String>,
    pub cleared: Vec<(Option<String>, u32)>,
    pub proxy_updates: u32,
    pub spin_count: u32,
    pub created: u32,
    pub init_failure: Option<String>,
}

pub struct MockEngine {
    config: RefCell<EngineConfig>,
    initialized: Cell<bool>,
    webviews: RefCell<Vec<Weak<MockWebView>>>,
    engine_queue: RefCell<Vec<EngineEvent>>,
    sink: RefCell<Option<EngineEventSink>>,
    waker: RefCell<Option<Waker>>,
    weak_self: RefCell<Weak<MockEngine>>,
    pub state: RefCell<MockEngineState>,
}

impl MockEngine {
    pub fn new() -> Rc<MockEngine> {
        let e = Rc::new(MockEngine {
            config: RefCell::new(EngineConfig::default()),
            initialized: Cell::new(false),
            webviews: RefCell::new(Vec::new()),
            engine_queue: RefCell::new(Vec::new()),
            sink: RefCell::new(None),
            waker: RefCell::new(None),
            weak_self: RefCell::new(Weak::new()),
            state: RefCell::new(MockEngineState::default()),
        });
        *e.weak_self.borrow_mut() = Rc::downgrade(&e);
        e
    }

    fn wake(&self) {
        if let Some(w) = self.waker.borrow().as_ref() {
            w();
        }
    }

    /// Live mock webviews, in creation order.
    pub fn webviews(&self) -> Vec<Rc<MockWebView>> {
        self.webviews
            .borrow()
            .iter()
            .filter_map(|w| w.upgrade())
            .collect()
    }

    /// Tests: initialise without a rendering context.
    pub fn initialize_for_tests(&self) {
        if !self.initialized.get() {
            self.initialized.set(true);
            self.engine_queue
                .borrow_mut()
                .push(EngineEvent::Initialized);
        }
    }

    /// Tests: simulate an engine-created (window.open) webview.
    pub fn push_auxiliary_webview(&self, private: bool) -> Rc<MockWebView> {
        let wv = self.make_webview(None, private, 1.0, (100, 100));
        self.engine_queue
            .borrow_mut()
            .push(EngineEvent::AuxiliaryWebView(wv.clone()));
        wv
    }

    pub fn push_crash(&self, reason: &str) {
        self.engine_queue.borrow_mut().push(EngineEvent::Crashed {
            reason: reason.into(),
            backtrace: String::new(),
        });
    }

    fn make_webview(
        &self,
        sink: Option<EventSink>,
        private: bool,
        dpr: f64,
        size: (u32, u32),
    ) -> Rc<MockWebView> {
        self.state.borrow_mut().created += 1;
        let wv = Rc::new(MockWebView {
            engine: self.weak_self.borrow().clone(),
            private,
            sink: RefCell::new(sink),
            state: RefCell::new(MockWebViewState {
                size,
                viewport: Rect::new(0.0, 0.0, size.0 as f64, size.1 as f64),
                dpr,
                pinch_zoom: 1.0,
                page_zoom: 1.0,
                ..Default::default()
            }),
            queue: RefCell::new(Vec::new()),
        });
        self.webviews.borrow_mut().push(Rc::downgrade(&wv));
        wv
    }
}

impl Engine for MockEngine {
    fn name(&self) -> &'static str {
        "mock"
    }
    fn version(&self) -> String {
        "0.0.0-mock".into()
    }
    fn configure(&self, config: EngineConfig) {
        *self.config.borrow_mut() = config;
    }
    fn config(&self) -> EngineConfig {
        self.config.borrow().clone()
    }
    fn initialize(&self, _ctx: Rc<dyn RenderingContext>) -> Result<(), String> {
        if let Some(err) = self.state.borrow().init_failure.clone() {
            return Err(err);
        }
        if !self.initialized.get() {
            self.initialized.set(true);
            self.engine_queue
                .borrow_mut()
                .push(EngineEvent::Initialized);
            self.wake();
        }
        Ok(())
    }
    fn is_initialized(&self) -> bool {
        self.initialized.get()
    }
    fn shutdown(&self) {
        if self.initialized.replace(false) {
            for wv in self.webviews() {
                wv.close();
            }
            self.engine_queue.borrow_mut().push(EngineEvent::ShutDown);
            self.wake();
        }
    }
    fn create_webview(
        &self,
        sink: EventSink,
        private: bool,
        dpr: f64,
        size: (u32, u32),
    ) -> Option<Rc<dyn WebView>> {
        if !self.initialized.get() {
            return None;
        }
        let wv = self.make_webview(Some(sink), private, dpr, size);
        Some(wv)
    }
    fn spin_event_loop(&self) {
        self.state.borrow_mut().spin_count += 1;
        // Engine events first (Initialized before any webview event).
        let events: Vec<EngineEvent> = std::mem::take(&mut *self.engine_queue.borrow_mut());
        let sink = self.sink.borrow().clone();
        for ev in events {
            if let Some(sink) = &sink {
                sink(ev);
            }
        }
        // Drop dead views, deliver the rest.  Delivery may create new views
        // or queue more events; loop until quiescent.
        for _ in 0..16 {
            self.webviews.borrow_mut().retain(|w| w.upgrade().is_some());
            let views = self.webviews();
            let pending: usize = views.iter().map(|v| v.queue.borrow().len()).sum();
            if pending == 0 {
                break;
            }
            for wv in views {
                wv.deliver();
            }
        }
    }
    fn set_pref(&self, name: &str, value: &str) {
        self.state
            .borrow_mut()
            .prefs
            .insert(name.into(), value.into());
    }
    fn set_proxy(&self, proxy: &ProxyConfig) {
        self.config.borrow_mut().proxy = proxy.clone();
        self.state.borrow_mut().proxy_updates += 1;
    }
    fn clear_site_data(&self, origin: Option<&str>, kinds: u32) {
        self.state
            .borrow_mut()
            .cleared
            .push((origin.map(|s| s.to_string()), kinds));
    }
    fn set_event_sink(&self, sink: EngineEventSink) {
        *self.sink.borrow_mut() = Some(sink);
    }
    fn set_waker(&self, waker: Waker) {
        *self.waker.borrow_mut() = Some(waker);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn events_are_delivered_only_on_spin() {
        let engine = MockEngine::new();
        engine.initialize_for_tests();
        let got: Rc<RefCell<Vec<String>>> = Rc::new(RefCell::new(Vec::new()));
        let g = got.clone();
        let sink: EventSink = Rc::new(move |ev| g.borrow_mut().push(format!("{ev:?}")));
        let wv = engine.create_webview(sink, false, 2.0, (100, 200)).unwrap();
        wv.load("https://example.org/x");
        assert!(got.borrow().is_empty(), "nothing before spin");
        engine.spin_event_loop();
        let events = got.borrow();
        assert!(events[0].contains("Started"));
        assert!(events[1].contains("UrlChanged"));
        assert!(events
            .iter()
            .any(|e| e.contains("TitleChanged(\"example.org\")")));
        assert!(events.last().unwrap().contains("FrameReady"));
    }

    #[test]
    fn history_navigation() {
        let engine = MockEngine::new();
        engine.initialize_for_tests();
        let sink: EventSink = Rc::new(|_| {});
        let wv = engine.create_webview(sink, false, 1.0, (1, 1)).unwrap();
        wv.load("https://a/");
        wv.load("https://b/");
        wv.go_back();
        let mock = engine.webviews()[0].clone();
        assert_eq!(mock.state.borrow().url, "https://a/");
        wv.go_forward();
        assert_eq!(mock.state.borrow().url, "https://b/");
        assert!(engine
            .create_webview(Rc::new(|_| {}), true, 1.0, (1, 1))
            .unwrap()
            .is_private());
    }
}
