// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The one [`Browser`] core per process, the engine waker, and the
//! dispatcher that turns [`BrowserEvent`]s into Qt signals and model
//! updates.  All on the GUI thread; the thread-local enforces it.

use std::cell::RefCell;
use std::rc::Rc;
use std::sync::Arc;
use std::time::Duration;

use qmetaobject::{queued_callback, single_shot, QPointer};
use tuuli_core::browser::{Browser, BrowserEvent};
use tuuli_core::engine::Engine;
use tuuli_core::paths::AppPaths;
use tuuli_core::session::DEBOUNCE_MS;

use crate::objects::BrowserObject;
use crate::platform::DBusTransferEngine;
use crate::webview::WebViewItem;

thread_local! {
    static CORE: RefCell<Option<Rc<RefCell<Browser>>>> = const { RefCell::new(None) };
    static DISPATCH: RefCell<Dispatch> = RefCell::new(Dispatch::default());
    static SESSION_TIMER_ARMED: RefCell<bool> = const { RefCell::new(false) };
    static PUMPING: RefCell<bool> = const { RefCell::new(false) };
}

#[derive(Default)]
pub(crate) struct Dispatch {
    pub browser: Option<QPointer<BrowserObject>>,
    pub views: Vec<QPointer<WebViewItem>>,
}

/// Creates the core with `engine`, installs the waker and starts it.
pub fn install(engine: Rc<dyn Engine>, paths: AppPaths, args: Vec<String>) -> Result<(), String> {
    let transfers = DBusTransferEngine::new()
        .map(|t| Box::new(t) as Box<dyn tuuli_core::downloads::TransferEngine>);
    let browser = Browser::new(engine.clone(), paths, transfers)?;
    let core = Rc::new(RefCell::new(browser));
    CORE.with(|c| *c.borrow_mut() = Some(core.clone()));

    // Servo's waker may fire from any engine thread; it only posts to the
    // GUI thread, where we spin the engine and dispatch what came out.
    let spin = queued_callback(|()| {
        with_core_opt(|b| b.spin());
        pump();
    });
    engine.set_waker(Arc::new(move || spin(())));

    core.borrow_mut().start(&args);
    pump();
    Ok(())
}

pub fn with_core<R>(f: impl FnOnce(&mut Browser) -> R) -> R {
    CORE.with(|c| {
        let core = c
            .borrow()
            .clone()
            .expect("tuuli_qml::install() must run before the QML is loaded");
        let mut b = core.borrow_mut();
        f(&mut b)
    })
}

pub fn with_core_opt<R>(f: impl FnOnce(&mut Browser) -> R) -> Option<R> {
    CORE.with(|c| {
        let core = c.borrow().clone()?;
        let mut b = core.borrow_mut();
        Some(f(&mut b))
    })
}

pub(crate) fn register_browser(obj: QPointer<BrowserObject>) {
    DISPATCH.with(|d| d.borrow_mut().browser = Some(obj));
}

pub(crate) fn register_view(view: QPointer<WebViewItem>) {
    DISPATCH.with(|d| {
        let mut d = d.borrow_mut();
        d.views.retain(|v| !v.is_null());
        d.views.push(view);
    });
}

fn arm_session_timer() {
    let armed = SESSION_TIMER_ARMED.with(|a| std::mem::replace(&mut *a.borrow_mut(), true));
    if armed {
        return;
    }
    single_shot(Duration::from_millis(DEBOUNCE_MS), || {
        SESSION_TIMER_ARMED.with(|a| *a.borrow_mut() = false);
        with_core_opt(|b| {
            let _ = b.session.flush();
        });
    });
}

/// Applies the core's queued events to the Qt objects.  Call after every
/// call into the core from a QML handler, and after every spin.
///
/// Re-entrant calls (a QML handler reacting to a dispatched signal calls
/// back into the core and pumps) are no-ops; the outer pump loops until
/// the core is quiet.
pub fn pump() {
    let already = PUMPING.with(|p| std::mem::replace(&mut *p.borrow_mut(), true));
    if already {
        return;
    }
    struct Reset;
    impl Drop for Reset {
        fn drop(&mut self) {
            PUMPING.with(|p| *p.borrow_mut() = false);
        }
    }
    let _reset = Reset;

    for _ in 0..64 {
        let Some(events) = with_core_opt(|b| b.pump()) else {
            return;
        };
        if events.is_empty() {
            return;
        }
        let (browser, views) = DISPATCH.with(|d| {
            let d = d.borrow();
            (d.browser.clone(), d.views.clone())
        });
        for ev in events {
            if let BrowserEvent::SessionSaveRequested = ev {
                arm_session_timer();
                continue;
            }
            for v in &views {
                if let Some(view) = v.as_pinned() {
                    view.borrow_mut().on_browser_event(&ev);
                }
            }
            if let Some(b) = browser.as_ref().and_then(|b| b.as_pinned()) {
                b.borrow_mut().on_browser_event(ev);
            }
        }
        if !with_core_opt(|b| b.has_pending_events()).unwrap_or(false) {
            return;
        }
    }
}
