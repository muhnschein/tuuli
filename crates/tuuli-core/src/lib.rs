// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Tuuli core: everything below the Qt/QML layer and above the engine.
//!
//! This crate knows nothing about Qt.  The `tuuli-qml` crate wraps its
//! types in `QObject`s; the `tuuli-servo` crate implements the [`engine`]
//! traits over libservo.  Everything here is exercised by `cargo test`.
//!
//! Threading (spec §4.2, amended for Rust): libservo's types are
//! single-thread only, so the whole core, the engine and painting run on
//! the Qt GUI thread with the *basic* scene-graph render loop.  Engine
//! callbacks are only ever delivered from inside
//! [`engine::Engine::spin_event_loop`], never re-entrantly from a call the
//! core makes into the engine.

pub mod bookmarks;
pub mod browser;
pub mod cosmetic;
pub mod downloads;
pub mod engine;
pub mod geometry;
pub mod gesture;
pub mod history;
pub mod ime;
pub mod input;
pub mod mock;
pub mod paths;
pub mod perflog;
pub mod permissions;
pub mod prefs;
pub mod proxy;
pub mod search;
pub mod session;
pub mod tabs;
pub mod textdiff;
pub mod useragent;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");

pub use engine::{Engine, RenderingContext, WebView, WebViewEvent};
pub use geometry::{Point, Rect, Size};
