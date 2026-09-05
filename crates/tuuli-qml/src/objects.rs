// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The `Browser` singleton and the objects it exposes to QML.  Thin
//! wrappers: state lives in [`tuuli_core`], these mirror it into Qt
//! properties and models and translate QML calls back.

#![allow(non_snake_case)]

use std::cell::RefCell;
use std::collections::HashMap;

use qmetaobject::prelude::*;
use qmetaobject::{QObjectBox, QPointer, QSingletonInit, USER_ROLE};
use qttypes::{QByteArray, QModelIndex, QRectF, QString, QVariant, QVariantList, QVariantMap};
use tuuli_core::bookmarks::Bookmark;
use tuuli_core::browser::BrowserEvent;
use tuuli_core::downloads::DownloadItem;
use tuuli_core::engine::{DialogRequest, InputType, PermissionKind, PermissionRequest};
use tuuli_core::geometry::Rect;
use tuuli_core::history::HistoryEntry;
use tuuli_core::ime::{ImeChanges, ImeRequest, InputMethodState};
use tuuli_core::permissions::Decision;
use tuuli_core::search;
use tuuli_core::tabs::{Tab, TabEvent, TabId};

use crate::core::{pump, register_browser, with_core, with_core_opt};
use crate::platform;

fn qs(s: &str) -> QString {
    QString::from(s)
}

// ---- Tab ---------------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct TabObject {
    base: qt_base_class!(trait QObject),
    pub(crate) tabId: qt_property!(i32; NOTIFY changed),
    url: qt_property!(QString; NOTIFY changed),
    title: qt_property!(QString; NOTIFY changed),
    displayTitle: qt_property!(QString; NOTIFY changed),
    loading: qt_property!(bool; NOTIFY loadingChanged),
    canGoBack: qt_property!(bool; NOTIFY changed),
    canGoForward: qt_property!(bool; NOTIFY changed),
    isPrivate: qt_property!(bool; NOTIFY changed),
    hasFavicon: qt_property!(bool; NOTIFY changed),
    faviconSource: qt_property!(QString; NOTIFY changed),
    hasThumbnail: qt_property!(bool; NOTIFY changed),
    thumbnailSource: qt_property!(QString; NOTIFY changed),
    desktopMode: qt_property!(bool; WRITE set_desktop_mode NOTIFY changed),
    hasWebView: qt_property!(bool; NOTIFY changed),
    scrollX: qt_property!(f64; NOTIFY viewportChanged),
    scrollY: qt_property!(f64; NOTIFY viewportChanged),
    pinchZoom: qt_property!(f64; NOTIFY viewportChanged),
    contentHeight: qt_property!(f64; NOTIFY viewportChanged),
    /// Anything above changed.
    changed: qt_signal!(),
    /// `loading` flipped: the chrome shows the toolbar on a new load.
    loadingChanged: qt_signal!(),
    /// Scroll, zoom or content size moved: the chrome hides the toolbar.
    viewportChanged: qt_signal!(),

    load: qt_method!(fn(&mut self, url: QString)),
    reload: qt_method!(fn(&mut self)),
    stop: qt_method!(fn(&mut self)),
    goBack: qt_method!(fn(&mut self)),
    goForward: qt_method!(fn(&mut self)),
    findInPage: qt_method!(fn(&mut self, text: QString, caseSensitive: bool)),
    findNext: qt_method!(fn(&mut self)),
    findPrevious: qt_method!(fn(&mut self)),
    clearFind: qt_method!(fn(&mut self)),

    favicon_revision: u32,
    thumbnail_revision: u32,
    /// Set by `sync_from`, consumed by `emit_changes`: which of the
    /// finer-grained signals the last sync earned.
    loading_dirty: bool,
    viewport_dirty: bool,
}

impl TabObject {
    /// `changed`, plus `loadingChanged` / `viewportChanged` when the last
    /// `sync_from` moved those.  Needs no `&mut self`: the flags are read
    /// here and reset on the next sync.
    fn emit_changes(&self) {
        self.changed();
        if self.loading_dirty {
            self.loadingChanged();
        }
        if self.viewport_dirty {
            self.viewportChanged();
        }
    }

    fn id(&self) -> TabId {
        self.tabId as TabId
    }

    /// Mirrors core state; publishes changed images to `image://tuuli/`.
    fn sync_from(&mut self, t: &Tab) {
        self.loading_dirty = self.loading != t.loading;
        self.viewport_dirty = self.scrollX != t.scroll.x
            || self.scrollY != t.scroll.y
            || self.pinchZoom != t.pinch_zoom
            || self.contentHeight != t.content_size.height;
        self.tabId = t.id as i32;
        self.url = qs(&t.url);
        self.title = qs(&t.title);
        self.displayTitle = qs(&t.display_title());
        self.loading = t.loading;
        self.canGoBack = t.can_go_back;
        self.canGoForward = t.can_go_forward;
        self.isPrivate = t.private;
        self.desktopMode = t.desktop_mode;
        self.hasWebView = t.has_webview();
        self.scrollX = t.scroll.x;
        self.scrollY = t.scroll.y;
        self.pinchZoom = t.pinch_zoom;
        self.contentHeight = t.content_size.height;
        self.hasFavicon = t.favicon.is_some();
        self.hasThumbnail = t.thumbnail.is_some();
        if t.favicon_revision != self.favicon_revision {
            self.favicon_revision = t.favicon_revision;
            platform::set_image(&format!("favicon/{}", t.id), t.favicon.clone());
        }
        if t.thumbnail_revision != self.thumbnail_revision {
            self.thumbnail_revision = t.thumbnail_revision;
            platform::set_image(&format!("thumbnail/{}", t.id), t.thumbnail.clone());
        }
        self.faviconSource = if t.favicon.is_some() {
            qs(&format!(
                "image://tuuli/favicon/{}/{}",
                t.id, t.favicon_revision
            ))
        } else {
            QString::default()
        };
        self.thumbnailSource = if t.thumbnail.is_some() {
            qs(&format!(
                "image://tuuli/thumbnail/{}/{}",
                t.id, t.thumbnail_revision
            ))
        } else {
            QString::default()
        };
    }

    fn set_desktop_mode(&mut self, on: bool) {
        let id = self.id();
        with_core(|b| b.tabs.borrow_mut().set_desktop_mode(id, on));
        pump();
    }
    fn load(&mut self, url: QString) {
        let id = self.id();
        with_core(|b| b.tabs.borrow_mut().load(id, &url.to_string()));
        pump();
    }
    fn reload(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.reload()));
        pump();
    }
    fn stop(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.stop()));
        pump();
    }
    fn goBack(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.go_back()));
        pump();
    }
    fn goForward(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.go_forward()));
        pump();
    }
    fn findInPage(&mut self, text: QString, caseSensitive: bool) {
        let id = self.id();
        with_core(|b| {
            b.tabs
                .borrow_mut()
                .by_id_mut(id)
                .map(|t| t.find(&text.to_string(), caseSensitive))
        });
    }
    fn findNext(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.find_next(true)));
    }
    fn findPrevious(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow().by_id(id).map(|t| t.find_next(false)));
    }
    fn clearFind(&mut self) {
        let id = self.id();
        with_core(|b| b.tabs.borrow_mut().by_id_mut(id).map(|t| t.find_clear()));
    }
}

// ---- Tab model -----------------------------------------------------------------------

const ROLE_TAB: i32 = USER_ROLE + 1;
const ROLE_URL: i32 = USER_ROLE + 2;
const ROLE_TITLE: i32 = USER_ROLE + 3;
const ROLE_PRIVATE: i32 = USER_ROLE + 4;
const ROLE_LOADING: i32 = USER_ROLE + 5;
const ROLE_FAVICON: i32 = USER_ROLE + 6;
const ROLE_THUMBNAIL: i32 = USER_ROLE + 7;
const ROLE_ACTIVE: i32 = USER_ROLE + 8;
const ROLE_TAB_ID: i32 = USER_ROLE + 9;

fn object_variant<T: QObject + Sized + 'static>(b: &QObjectBox<T>) -> QVariant {
    let pinned = b.pinned();
    pinned.get_or_create_cpp_object();
    let obj: &dyn QObject = pinned.borrow();
    unsafe { obj.as_qvariant() }
}

#[derive(QObject, Default)]
pub struct TabModel {
    base: qt_base_class!(trait QAbstractListModel),
    count: qt_property!(i32; NOTIFY countChanged),
    privateCount: qt_property!(i32; NOTIFY countChanged),
    countChanged: qt_signal!(),
    currentIndex: qt_property!(i32; WRITE set_current_index NOTIFY currentChanged),
    currentTab: qt_property!(QPointer<TabObject>; NOTIFY currentChanged),
    currentChanged: qt_signal!(),

    newTab: qt_method!(fn(&mut self, url: QString, isPrivate: bool, activate: bool)),
    closeTab: qt_method!(fn(&mut self, index: i32)),
    closeTabById: qt_method!(fn(&mut self, tabId: i32)),
    closeAll: qt_method!(fn(&mut self)),
    closeAllPrivate: qt_method!(fn(&mut self)),
    moveTab: qt_method!(fn(&mut self, from: i32, to: i32)),
    activate: qt_method!(fn(&mut self, index: i32)),
    tabAt: qt_method!(fn(&self, index: i32) -> QVariant),
    indexOfId: qt_method!(fn(&self, tabId: i32) -> i32),

    tabs: Vec<QObjectBox<TabObject>>,
}

impl QAbstractListModel for TabModel {
    fn row_count(&self) -> i32 {
        self.tabs.len() as i32
    }
    fn data(&self, index: QModelIndex, role: i32) -> QVariant {
        let row = index.row();
        if row < 0 || row as usize >= self.tabs.len() {
            return QVariant::default();
        }
        let b = &self.tabs[row as usize];
        if role == ROLE_TAB {
            return object_variant(b);
        }
        let pinned = b.pinned();
        let t = pinned.borrow();
        match role {
            ROLE_URL => t.url.clone().into(),
            ROLE_TITLE => t.displayTitle.clone().into(),
            ROLE_PRIVATE => t.isPrivate.into(),
            ROLE_LOADING => t.loading.into(),
            ROLE_FAVICON => t.faviconSource.clone().into(),
            ROLE_THUMBNAIL => t.thumbnailSource.clone().into(),
            ROLE_ACTIVE => (row == self.currentIndex).into(),
            ROLE_TAB_ID => t.tabId.into(),
            _ => QVariant::default(),
        }
    }
    fn role_names(&self) -> HashMap<i32, QByteArray> {
        [
            (ROLE_TAB, "tab"),
            (ROLE_URL, "url"),
            (ROLE_TITLE, "title"),
            (ROLE_PRIVATE, "isPrivate"),
            (ROLE_LOADING, "loading"),
            (ROLE_FAVICON, "favicon"),
            (ROLE_THUMBNAIL, "thumbnail"),
            (ROLE_ACTIVE, "active"),
            (ROLE_TAB_ID, "tabId"),
        ]
        .into_iter()
        .map(|(k, v)| (k, QByteArray::from(v)))
        .collect()
    }
}

impl TabModel {
    fn make_object(id: TabId) -> Option<QObjectBox<TabObject>> {
        let b = QObjectBox::new(TabObject::default());
        let found = with_core(|c| {
            c.tabs
                .borrow()
                .by_id(id)
                .map(|t| b.pinned().borrow_mut().sync_from(t))
                .is_some()
        });
        if !found {
            return None;
        }
        b.pinned().get_or_create_cpp_object();
        Some(b)
    }

    fn position(&self, id: TabId) -> Option<usize> {
        self.tabs
            .iter()
            .position(|b| b.pinned().borrow().id() == id)
    }

    fn update_counts(&mut self) {
        self.count = self.tabs.len() as i32;
        self.privateCount = self
            .tabs
            .iter()
            .filter(|b| b.pinned().borrow().isPrivate)
            .count() as i32;
        self.countChanged();
    }

    fn update_current(&mut self) {
        let idx = with_core(|c| c.tabs.borrow().current_index());
        self.currentIndex = idx.map(|i| i as i32).unwrap_or(-1);
        self.currentTab = match idx.and_then(|i| self.tabs.get(i)) {
            Some(b) => QPointer::from(b.pinned().borrow()),
            None => QPointer::default(),
        };
        self.currentChanged();
    }

    /// Full rebuild from the core (startup).
    pub(crate) fn rebuild(&mut self) {
        self.begin_reset_model();
        let ids: Vec<TabId> = with_core(|c| c.tabs.borrow().iter().map(|t| t.id).collect());
        self.tabs = ids.into_iter().filter_map(Self::make_object).collect();
        self.end_reset_model();
        self.update_counts();
        self.update_current();
    }

    pub(crate) fn apply(&mut self, ev: &TabEvent) {
        match ev {
            TabEvent::Inserted { row, id } => {
                let Some(obj) = Self::make_object(*id) else {
                    return;
                };
                let row = (*row).min(self.tabs.len());
                self.begin_insert_rows(row as i32, row as i32);
                self.tabs.insert(row, obj);
                self.end_insert_rows();
                self.update_counts();
            }
            TabEvent::Removed { id, .. } => {
                let Some(row) = self.position(*id) else {
                    return;
                };
                self.begin_remove_rows(row as i32, row as i32);
                self.tabs.remove(row);
                self.end_remove_rows();
                platform::remove_images_with_prefix(&format!("favicon/{id}"));
                platform::remove_images_with_prefix(&format!("thumbnail/{id}"));
                self.update_counts();
            }
            TabEvent::Moved { from, to } => {
                if *from >= self.tabs.len() || *to >= self.tabs.len() || from == to {
                    return;
                }
                let dest = if to > from { *to + 1 } else { *to };
                self.begin_move_rows(
                    QModelIndex::default(),
                    *from as i32,
                    *from as i32,
                    QModelIndex::default(),
                    dest as i32,
                );
                let obj = self.tabs.remove(*from);
                self.tabs.insert(*to, obj);
                self.end_move_rows();
            }
            TabEvent::Reset => {
                self.begin_reset_model();
                self.tabs.clear();
                self.end_reset_model();
                self.update_counts();
            }
            TabEvent::Changed { id, .. } => {
                let Some(row) = self.position(*id) else {
                    return;
                };
                with_core(|c| {
                    if let Some(t) = c.tabs.borrow().by_id(*id) {
                        self.tabs[row].pinned().borrow_mut().sync_from(t);
                    }
                });
                self.tabs[row].pinned().borrow().emit_changes();
                let idx = self.row_index(row as i32);
                self.data_changed(idx, idx);
            }
            TabEvent::CurrentChanged => {
                let old = self.currentIndex;
                self.update_current();
                for r in [old, self.currentIndex] {
                    if r >= 0 && (r as usize) < self.tabs.len() {
                        let idx = self.row_index(r);
                        self.data_changed(idx, idx);
                    }
                }
            }
            _ => {}
        }
    }

    fn set_current_index(&mut self, index: i32) {
        with_core(|c| {
            c.tabs.borrow_mut().set_current(if index < 0 {
                None
            } else {
                Some(index as usize)
            })
        });
        pump();
    }
    fn newTab(&mut self, url: QString, isPrivate: bool, activate: bool) {
        with_core(|c| {
            c.tabs
                .borrow_mut()
                .new_tab(&url.to_string(), isPrivate, activate);
        });
        pump();
    }
    fn closeTab(&mut self, index: i32) {
        if index >= 0 {
            with_core(|c| c.tabs.borrow_mut().close(index as usize));
            pump();
        }
    }
    fn closeTabById(&mut self, tabId: i32) {
        with_core(|c| c.tabs.borrow_mut().close_by_id(tabId as TabId));
        pump();
    }
    fn closeAll(&mut self) {
        with_core(|c| c.tabs.borrow_mut().close_all());
        pump();
    }
    fn closeAllPrivate(&mut self) {
        with_core(|c| c.tabs.borrow_mut().close_all_private());
        pump();
    }
    fn moveTab(&mut self, from: i32, to: i32) {
        if from >= 0 && to >= 0 {
            with_core(|c| c.tabs.borrow_mut().move_tab(from as usize, to as usize));
            pump();
        }
    }
    fn activate(&mut self, index: i32) {
        self.set_current_index(index);
    }
    fn tabAt(&self, index: i32) -> QVariant {
        if index < 0 || index as usize >= self.tabs.len() {
            return QVariant::default();
        }
        object_variant(&self.tabs[index as usize])
    }
    fn indexOfId(&self, tabId: i32) -> i32 {
        self.position(tabId as TabId)
            .map(|i| i as i32)
            .unwrap_or(-1)
    }
}

// ---- History -------------------------------------------------------------------------

const ROLE_H_URL: i32 = USER_ROLE + 1;
const ROLE_H_TITLE: i32 = USER_ROLE + 2;
const ROLE_H_VISITS: i32 = USER_ROLE + 3;
const ROLE_H_LAST: i32 = USER_ROLE + 4;

#[derive(QObject, Default)]
pub struct HistoryModel {
    base: qt_base_class!(trait QAbstractListModel),
    count: qt_property!(i32; NOTIFY countChanged),
    countChanged: qt_signal!(),
    filter: qt_property!(QString; WRITE set_filter NOTIFY filterChanged),
    filterChanged: qt_signal!(),
    limit: qt_property!(i32; WRITE set_limit NOTIFY limitChanged),
    limitChanged: qt_signal!(),
    refresh: qt_method!(fn(&mut self)),
    remove: qt_method!(fn(&mut self, url: QString) -> bool),
    clear: qt_method!(fn(&mut self) -> bool),
    rows: Vec<HistoryEntry>,
}

impl QAbstractListModel for HistoryModel {
    fn row_count(&self) -> i32 {
        self.rows.len() as i32
    }
    fn data(&self, index: QModelIndex, role: i32) -> QVariant {
        let Some(e) = self.rows.get(index.row().max(0) as usize) else {
            return QVariant::default();
        };
        match role {
            ROLE_H_URL => qs(&e.url).into(),
            ROLE_H_TITLE => qs(&e.display_title()).into(),
            ROLE_H_VISITS => (e.visits as i32).into(),
            ROLE_H_LAST => (e.last_visit_ms as f64).into(),
            _ => QVariant::default(),
        }
    }
    fn role_names(&self) -> HashMap<i32, QByteArray> {
        [
            (ROLE_H_URL, "url"),
            (ROLE_H_TITLE, "title"),
            (ROLE_H_VISITS, "visits"),
            (ROLE_H_LAST, "lastVisit"),
        ]
        .into_iter()
        .map(|(k, v)| (k, QByteArray::from(v)))
        .collect()
    }
}

impl HistoryModel {
    pub(crate) fn refresh(&mut self) {
        let limit = if self.limit <= 0 {
            50
        } else {
            self.limit as usize
        };
        let filter = self.filter.to_string();
        let rows = with_core_opt(|b| b.history.search(&filter, limit)).unwrap_or_default();
        self.begin_reset_model();
        self.rows = rows;
        self.end_reset_model();
        self.count = self.rows.len() as i32;
        self.countChanged();
    }
    fn set_filter(&mut self, filter: QString) {
        if self.filter == filter {
            return;
        }
        self.filter = filter;
        self.filterChanged();
        self.refresh();
    }
    fn set_limit(&mut self, limit: i32) {
        let limit = limit.max(1);
        if self.limit == limit {
            return;
        }
        self.limit = limit;
        self.limitChanged();
        self.refresh();
    }
    fn remove(&mut self, url: QString) -> bool {
        let ok = with_core(|b| b.history.remove(&url.to_string()));
        self.refresh();
        ok
    }
    fn clear(&mut self) -> bool {
        let ok = with_core(|b| b.history.clear());
        self.refresh();
        ok
    }
}

// ---- Bookmarks ------------------------------------------------------------------------

const ROLE_B_ID: i32 = USER_ROLE + 1;
const ROLE_B_URL: i32 = USER_ROLE + 2;
const ROLE_B_TITLE: i32 = USER_ROLE + 3;
const ROLE_B_CREATED: i32 = USER_ROLE + 4;

#[derive(QObject, Default)]
pub struct BookmarkModel {
    base: qt_base_class!(trait QAbstractListModel),
    count: qt_property!(i32; NOTIFY countChanged),
    countChanged: qt_signal!(),
    changed: qt_signal!(),
    add: qt_method!(fn(&mut self, url: QString, title: QString) -> bool),
    remove: qt_method!(fn(&mut self, url: QString) -> bool),
    contains: qt_method!(fn(&self, url: QString) -> bool),
    rename: qt_method!(fn(&mut self, url: QString, title: QString) -> bool),
    move_: qt_method!(fn(&mut self, from: i32, to: i32) -> bool),
    refresh: qt_method!(fn(&mut self)),
    rows: Vec<Bookmark>,
}

impl QAbstractListModel for BookmarkModel {
    fn row_count(&self) -> i32 {
        self.rows.len() as i32
    }
    fn data(&self, index: QModelIndex, role: i32) -> QVariant {
        let Some(b) = self.rows.get(index.row().max(0) as usize) else {
            return QVariant::default();
        };
        match role {
            ROLE_B_ID => (b.id as i32).into(),
            ROLE_B_URL => qs(&b.url).into(),
            ROLE_B_TITLE => qs(&b.display_title()).into(),
            ROLE_B_CREATED => (b.created_ms as f64).into(),
            _ => QVariant::default(),
        }
    }
    fn role_names(&self) -> HashMap<i32, QByteArray> {
        [
            (ROLE_B_ID, "bookmarkId"),
            (ROLE_B_URL, "url"),
            (ROLE_B_TITLE, "title"),
            (ROLE_B_CREATED, "created"),
        ]
        .into_iter()
        .map(|(k, v)| (k, QByteArray::from(v)))
        .collect()
    }
}

impl BookmarkModel {
    pub(crate) fn refresh(&mut self) {
        let rows = with_core_opt(|b| b.bookmarks.all()).unwrap_or_default();
        self.begin_reset_model();
        self.rows = rows;
        self.end_reset_model();
        self.count = self.rows.len() as i32;
        self.countChanged();
    }
    fn add(&mut self, url: QString, title: QString) -> bool {
        let ok = with_core(|b| b.bookmarks.add(&url.to_string(), &title.to_string()));
        self.refresh();
        self.changed();
        ok
    }
    fn remove(&mut self, url: QString) -> bool {
        let ok = with_core(|b| b.bookmarks.remove(&url.to_string()));
        self.refresh();
        self.changed();
        ok
    }
    fn contains(&self, url: QString) -> bool {
        let url = url.to_string();
        self.rows.iter().any(|b| b.url == url)
    }
    fn rename(&mut self, url: QString, title: QString) -> bool {
        let ok = with_core(|b| b.bookmarks.rename(&url.to_string(), &title.to_string()));
        self.refresh();
        self.changed();
        ok
    }
    fn move_(&mut self, from: i32, to: i32) -> bool {
        if from < 0 || to < 0 {
            return false;
        }
        let ok = with_core(|b| b.bookmarks.move_bookmark(from as usize, to as usize));
        self.refresh();
        self.changed();
        ok
    }
}

// ---- Downloads -------------------------------------------------------------------------

const ROLE_D_ID: i32 = USER_ROLE + 1;
const ROLE_D_URL: i32 = USER_ROLE + 2;
const ROLE_D_FILE: i32 = USER_ROLE + 3;
const ROLE_D_PATH: i32 = USER_ROLE + 4;
const ROLE_D_MIME: i32 = USER_ROLE + 5;
const ROLE_D_RECEIVED: i32 = USER_ROLE + 6;
const ROLE_D_TOTAL: i32 = USER_ROLE + 7;
const ROLE_D_PROGRESS: i32 = USER_ROLE + 8;
const ROLE_D_FINISHED: i32 = USER_ROLE + 9;
const ROLE_D_OK: i32 = USER_ROLE + 10;
const ROLE_D_ERROR: i32 = USER_ROLE + 11;
const ROLE_D_PRIVATE: i32 = USER_ROLE + 12;

#[derive(QObject, Default)]
pub struct DownloadModel {
    base: qt_base_class!(trait QAbstractListModel),
    count: qt_property!(i32; NOTIFY countChanged),
    activeCount: qt_property!(i32; NOTIFY countChanged),
    countChanged: qt_signal!(),
    directory: qt_property!(QString; NOTIFY directoryChanged),
    directoryChanged: qt_signal!(),
    cancel: qt_method!(fn(&mut self, id: i32)),
    remove: qt_method!(fn(&mut self, id: i32)),
    clearFinished: qt_method!(fn(&mut self)),
    rows: Vec<DownloadItem>,
}

impl QAbstractListModel for DownloadModel {
    fn row_count(&self) -> i32 {
        self.rows.len() as i32
    }
    fn data(&self, index: QModelIndex, role: i32) -> QVariant {
        let Some(d) = self.rows.get(index.row().max(0) as usize) else {
            return QVariant::default();
        };
        match role {
            ROLE_D_ID => (d.id as i32).into(),
            ROLE_D_URL => qs(&d.url).into(),
            ROLE_D_FILE => qs(&d.file_name).into(),
            ROLE_D_PATH => qs(&d.path.to_string_lossy()).into(),
            ROLE_D_MIME => qs(&d.mime_type).into(),
            ROLE_D_RECEIVED => (d.received as f64).into(),
            ROLE_D_TOTAL => (d.total as f64).into(),
            ROLE_D_PROGRESS => d.progress().into(),
            ROLE_D_FINISHED => d.finished.into(),
            ROLE_D_OK => d.ok.into(),
            ROLE_D_ERROR => qs(&d.error).into(),
            ROLE_D_PRIVATE => d.private.into(),
            _ => QVariant::default(),
        }
    }
    fn role_names(&self) -> HashMap<i32, QByteArray> {
        [
            (ROLE_D_ID, "downloadId"),
            (ROLE_D_URL, "url"),
            (ROLE_D_FILE, "fileName"),
            (ROLE_D_PATH, "path"),
            (ROLE_D_MIME, "mimeType"),
            (ROLE_D_RECEIVED, "received"),
            (ROLE_D_TOTAL, "total"),
            (ROLE_D_PROGRESS, "progress"),
            (ROLE_D_FINISHED, "finished"),
            (ROLE_D_OK, "ok"),
            (ROLE_D_ERROR, "error"),
            (ROLE_D_PRIVATE, "isPrivate"),
        ]
        .into_iter()
        .map(|(k, v)| (k, QByteArray::from(v)))
        .collect()
    }
}

impl DownloadModel {
    pub(crate) fn refresh(&mut self) {
        let (rows, dir) = with_core_opt(|b| {
            (
                b.downloads.items().to_vec(),
                b.downloads.directory().to_string_lossy().to_string(),
            )
        })
        .unwrap_or_default();
        self.begin_reset_model();
        self.rows = rows;
        self.end_reset_model();
        self.count = self.rows.len() as i32;
        self.activeCount = self.rows.iter().filter(|d| !d.finished).count() as i32;
        self.countChanged();
        let dir = qs(&dir);
        if self.directory != dir {
            self.directory = dir;
            self.directoryChanged();
        }
    }
    fn cancel(&mut self, id: i32) {
        with_core(|b| b.cancel_download(id as u64));
        pump();
        self.refresh();
    }
    fn remove(&mut self, id: i32) {
        with_core(|b| b.remove_download(id as u64));
        pump();
        self.refresh();
    }
    fn clearFinished(&mut self) {
        with_core(|b| b.downloads.clear_finished());
        pump();
        self.refresh();
    }
}

// ---- Permissions -------------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct PermissionsObject {
    base: qt_base_class!(trait QObject),
    count: qt_property!(i32; NOTIFY changed),
    changed: qt_signal!(),
    entries: qt_method!(fn(&self) -> QVariantList),
    decisionFor: qt_method!(fn(&self, origin: QString, kind: i32) -> i32),
    setDecision: qt_method!(fn(&mut self, origin: QString, kind: i32, decision: i32)),
    clearOrigin: qt_method!(fn(&mut self, origin: QString)),
    clearAll: qt_method!(fn(&mut self)),
}

impl PermissionsObject {
    pub(crate) fn refresh(&mut self) {
        self.count = with_core_opt(|b| b.permissions.count()).unwrap_or(0) as i32;
        self.changed();
    }
    fn entries(&self) -> QVariantList {
        with_core_opt(|b| {
            b.permissions
                .entries()
                .into_iter()
                .map(|e| {
                    let mut m = QVariantMap::default();
                    m.insert(qs("origin"), qs(&e.origin).into());
                    m.insert(qs("kind"), (e.kind.index() as i32).into());
                    m.insert(qs("kindName"), qs(e.kind.name()).into());
                    m.insert(qs("decision"), e.decision.index().into());
                    QVariant::from(m)
                })
                .collect::<QVariantList>()
        })
        .unwrap_or_default()
    }
    fn decisionFor(&self, origin: QString, kind: i32) -> i32 {
        let Some(kind) = PermissionKind::from_index(kind.max(0) as u32) else {
            return 0;
        };
        with_core_opt(|b| b.permissions.decision(&origin.to_string(), kind).index()).unwrap_or(0)
    }
    fn setDecision(&mut self, origin: QString, kind: i32, decision: i32) {
        let Some(kind) = PermissionKind::from_index(kind.max(0) as u32) else {
            return;
        };
        with_core(|b| {
            b.permissions
                .set_decision(&origin.to_string(), kind, Decision::from_index(decision))
        });
        self.refresh();
    }
    fn clearOrigin(&mut self, origin: QString) {
        with_core(|b| b.permissions.clear_origin(&origin.to_string()));
        self.refresh();
    }
    fn clearAll(&mut self) {
        with_core(|b| b.permissions.clear_all());
        self.refresh();
    }
}

// ---- Preferences ---------------------------------------------------------------------------

macro_rules! pref_setter {
    ($fn_name:ident, $field:ident, $ty:ty, $conv:expr) => {
        fn $fn_name(&mut self, v: $ty) {
            let value = $conv(v);
            // Only on an actual change: the chrome binds `checked: pref`
            // and writes `pref = checked` back, and notifying on a no-op
            // write would re-enter that binding.
            let changed = with_core(|b| {
                if b.prefs.$field == value {
                    false
                } else {
                    b.prefs.$field = value;
                    b.apply_prefs();
                    true
                }
            });
            if changed {
                self.sync();
                pump();
            }
        }
    };
}

#[derive(QObject, Default)]
pub struct PrefsObject {
    base: qt_base_class!(trait QObject),
    searchEngine: qt_property!(QString; WRITE set_search_engine NOTIFY changed),
    homePage: qt_property!(QString; WRITE set_home_page NOTIFY changed),
    restoreSession: qt_property!(bool; WRITE set_restore_session NOTIFY changed),
    blockThirdPartyCookies: qt_property!(bool; WRITE set_block_third_party_cookies NOTIFY changed),
    sendDoNotTrack: qt_property!(bool; WRITE set_send_dnt NOTIFY changed),
    sendGlobalPrivacyControl: qt_property!(bool; WRITE set_send_gpc NOTIFY changed),
    referrerPolicy: qt_property!(QString; WRITE set_referrer_policy NOTIFY changed),
    cosmeticFiltering: qt_property!(bool; WRITE set_cosmetic_filtering NOTIFY changed),
    javascriptEnabled: qt_property!(bool; WRITE set_javascript_enabled NOTIFY changed),
    userAgentOverride: qt_property!(QString; WRITE set_user_agent_override NOTIFY changed),
    downloadDirectory: qt_property!(QString; WRITE set_download_directory NOTIFY changed),
    devicePixelRatioOverride: qt_property!(f64; WRITE set_dpr_override NOTIFY changed),
    showFrameStats: qt_property!(bool; WRITE set_show_frame_stats NOTIFY changed),
    engineLogging: qt_property!(bool; WRITE set_engine_logging NOTIFY changed),
    perfLogging: qt_property!(bool; WRITE set_perf_logging NOTIFY changed),
    maxLiveWebViews: qt_property!(i32; WRITE set_max_live_webviews NOTIFY changed),
    changed: qt_signal!(),
}

impl PrefsObject {
    pub(crate) fn sync(&mut self) {
        with_core_opt(|b| {
            let p = &b.prefs;
            self.searchEngine = qs(&p.search_engine);
            self.homePage = qs(&p.home_page);
            self.restoreSession = p.restore_session;
            self.blockThirdPartyCookies = p.block_third_party_cookies;
            self.sendDoNotTrack = p.send_do_not_track;
            self.sendGlobalPrivacyControl = p.send_global_privacy_control;
            self.referrerPolicy = qs(&p.referrer_policy);
            self.cosmeticFiltering = p.cosmetic_filtering;
            self.javascriptEnabled = p.javascript_enabled;
            self.userAgentOverride = qs(&p.user_agent_override);
            self.downloadDirectory = qs(&p.download_dir(&b.paths.download_dir).to_string_lossy());
            self.devicePixelRatioOverride = p.device_pixel_ratio_override;
            self.showFrameStats = p.show_frame_stats;
            self.engineLogging = p.engine_logging;
            self.perfLogging = p.perf_logging;
            self.maxLiveWebViews = p.max_live_webviews as i32;
        });
        self.changed();
    }
    pref_setter!(set_search_engine, search_engine, QString, |v: QString| v
        .to_string());
    pref_setter!(set_home_page, home_page, QString, |v: QString| v
        .to_string());
    pref_setter!(set_restore_session, restore_session, bool, |v| v);
    pref_setter!(
        set_block_third_party_cookies,
        block_third_party_cookies,
        bool,
        |v| v
    );
    pref_setter!(set_send_dnt, send_do_not_track, bool, |v| v);
    pref_setter!(set_send_gpc, send_global_privacy_control, bool, |v| v);
    pref_setter!(
        set_referrer_policy,
        referrer_policy,
        QString,
        |v: QString| v.to_string()
    );
    pref_setter!(set_cosmetic_filtering, cosmetic_filtering, bool, |v| v);
    pref_setter!(set_javascript_enabled, javascript_enabled, bool, |v| v);
    pref_setter!(
        set_user_agent_override,
        user_agent_override,
        QString,
        |v: QString| v.to_string()
    );
    pref_setter!(
        set_download_directory,
        download_directory,
        QString,
        |v: QString| v.to_string()
    );
    pref_setter!(
        set_dpr_override,
        device_pixel_ratio_override,
        f64,
        |v: f64| v.max(0.0)
    );
    pref_setter!(set_show_frame_stats, show_frame_stats, bool, |v| v);
    pref_setter!(set_engine_logging, engine_logging, bool, |v| v);
    pref_setter!(set_perf_logging, perf_logging, bool, |v| v);
    pref_setter!(
        set_max_live_webviews,
        max_live_webviews,
        i32,
        |v: i32| v.max(1) as usize
    );
}

// ---- Clipboard -----------------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct ClipboardObject {
    base: qt_base_class!(trait QObject),
    hasText: qt_property!(bool; READ has_text NOTIFY changed),
    changed: qt_signal!(),
    text: qt_method!(fn(&self) -> QString),
    setText: qt_method!(fn(&mut self, text: QString)),
}

impl ClipboardObject {
    fn has_text(&self) -> bool {
        !platform::clipboard_text().is_empty()
    }
    fn text(&self) -> QString {
        qs(&platform::clipboard_text())
    }
    fn setText(&mut self, text: QString) {
        platform::set_clipboard_text(&text.to_string());
        self.changed();
    }
}

// ---- Input method proxy ----------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct InputMethodProxyObject {
    base: qt_base_class!(trait QObject),
    active: qt_property!(bool; NOTIFY activeChanged),
    activeChanged: qt_signal!(),
    text: qt_property!(QString; NOTIFY textChanged),
    textChanged: qt_signal!(),
    cursorPosition: qt_property!(i32; NOTIFY selectionChanged),
    anchorPosition: qt_property!(i32; NOTIFY selectionChanged),
    selectionChanged: qt_signal!(),
    inputMethodHints: qt_property!(i32; NOTIFY inputTypeChanged),
    enterKeyType: qt_property!(i32; NOTIFY inputTypeChanged),
    passwordMode: qt_property!(bool; NOTIFY inputTypeChanged),
    multiline: qt_property!(bool; NOTIFY inputTypeChanged),
    inputType: qt_property!(i32; NOTIFY inputTypeChanged),
    inputTypeChanged: qt_signal!(),
    cursorRect: qt_property!(QRectF; NOTIFY cursorRectChanged),
    cursorRectChanged: qt_signal!(),

    textEdited: qt_method!(fn(&mut self, text: QString)),
    sendKey: qt_method!(fn(&mut self, key: i32, text: QString, modifiers: i32)),
    dismiss: qt_method!(fn(&mut self)),
    submit: qt_method!(fn(&mut self)),

    pub(crate) state: InputMethodState,
    tab: Option<TabId>,
}

impl InputMethodProxyObject {
    pub(crate) fn set_tab(&mut self, tab: Option<TabId>) {
        self.tab = tab;
    }

    fn mirror(&mut self) {
        self.active = self.state.active;
        self.text = qs(&self.state.text);
        self.cursorPosition = self.state.cursor as i32;
        self.anchorPosition = self.state.anchor as i32;
        self.inputMethodHints = self.state.hints() as i32;
        self.enterKeyType = self.state.enter_key_type() as i32;
        self.passwordMode = self.state.password_mode();
        self.multiline = self.state.multiline;
        self.inputType = self.state.input_type.index();
        let r = self.state.cursor_rect;
        self.cursorRect = QRectF {
            x: r.x,
            y: r.y,
            width: r.width,
            height: r.height,
        };
    }

    pub(crate) fn emit_changes(&self, ch: ImeChanges) {
        if ch.input_type {
            self.inputTypeChanged();
        }
        if ch.text {
            self.textChanged();
        }
        if ch.selection {
            self.selectionChanged();
        }
        if ch.cursor_rect {
            self.cursorRectChanged();
        }
        if ch.active {
            self.activeChanged();
        }
    }

    pub(crate) fn show_from_engine(
        &mut self,
        t: InputType,
        text: &str,
        multiline: bool,
        rect: Rect,
    ) -> ImeChanges {
        let ch = self.state.show_from_engine(t, text, multiline, rect);
        self.mirror();
        ch
    }
    pub(crate) fn hide_from_engine(&mut self) -> ImeChanges {
        let ch = self.state.hide_from_engine();
        self.mirror();
        ch
    }
    pub(crate) fn selection_from_engine(
        &mut self,
        text: &str,
        cursor: usize,
        anchor: Option<usize>,
    ) -> ImeChanges {
        let ch = self.state.selection_from_engine(text, cursor, anchor);
        self.mirror();
        ch
    }

    fn flush_requests(&mut self) {
        let requests = self.state.take_requests();
        let Some(tab) = self.tab else { return };
        let wv =
            with_core_opt(|b| b.tabs.borrow().by_id(tab).and_then(|t| t.webview.clone())).flatten();
        let Some(wv) = wv else { return };
        for r in requests {
            match r {
                ImeRequest::Key {
                    down,
                    key,
                    modifiers,
                } => wv.key(down, &key, modifiers),
                ImeRequest::Commit(text) => {
                    wv.ime_composition(tuuli_core::engine::CompositionState::End, &text)
                }
                ImeRequest::Dismiss => wv.ime_dismissed(),
            }
        }
    }

    fn textEdited(&mut self, text: QString) {
        let ch = self.state.text_edited(&text.to_string());
        self.mirror();
        self.flush_requests();
        self.emit_changes(ch);
    }
    fn sendKey(&mut self, key: i32, text: QString, modifiers: i32) {
        self.state
            .send_key(key, &text.to_string(), modifiers.max(0) as u32);
        self.flush_requests();
    }
    fn dismiss(&mut self) {
        let ch = self.state.dismiss();
        self.mirror();
        self.flush_requests();
        self.emit_changes(ch);
    }
    fn submit(&mut self) {
        self.state.submit();
        self.flush_requests();
    }
}

// ---- Request wrappers -------------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct PermissionRequestObject {
    base: qt_base_class!(trait QObject),
    origin: qt_property!(QString; CONST),
    kind: qt_property!(i32; CONST),
    kindName: qt_property!(QString; CONST),
    answered: qt_property!(bool; NOTIFY answeredChanged),
    answeredChanged: qt_signal!(),
    allow: qt_method!(fn(&mut self)),
    deny: qt_method!(fn(&mut self)),
    request: Option<PermissionRequest>,
}

impl PermissionRequestObject {
    fn wrap(request: PermissionRequest) -> QObjectBox<Self> {
        let b = QObjectBox::new(Self {
            origin: qs(&request.origin),
            kind: request.kind.index() as i32,
            kindName: qs(request.kind.name()),
            request: Some(request),
            ..Default::default()
        });
        b.pinned().get_or_create_cpp_object();
        b
    }
    fn allow(&mut self) {
        if let Some(mut r) = self.request.take() {
            r.allow();
            self.answered = true;
            self.answeredChanged();
            pump();
        }
    }
    fn deny(&mut self) {
        if let Some(mut r) = self.request.take() {
            r.deny();
            self.answered = true;
            self.answeredChanged();
            pump();
        }
    }
}

#[derive(QObject, Default)]
pub struct DialogRequestObject {
    base: qt_base_class!(trait QObject),
    kind: qt_property!(i32; CONST),
    message: qt_property!(QString; CONST),
    defaultValue: qt_property!(QString; CONST),
    answered: qt_property!(bool; NOTIFY answeredChanged),
    answeredChanged: qt_signal!(),
    accept: qt_method!(fn(&mut self, value: QString)),
    dismiss: qt_method!(fn(&mut self)),
    request: Option<DialogRequest>,
}

impl DialogRequestObject {
    fn wrap(request: DialogRequest) -> QObjectBox<Self> {
        let b = QObjectBox::new(Self {
            kind: request.kind as i32,
            message: qs(&request.message),
            defaultValue: qs(&request.default_value),
            request: Some(request),
            ..Default::default()
        });
        b.pinned().get_or_create_cpp_object();
        b
    }
    fn accept(&mut self, value: QString) {
        if let Some(mut r) = self.request.take() {
            r.accept(value.to_string());
            self.answered = true;
            self.answeredChanged();
            pump();
        }
    }
    fn dismiss(&mut self) {
        if let Some(mut r) = self.request.take() {
            r.dismiss();
            self.answered = true;
            self.answeredChanged();
            pump();
        }
    }
}

// ---- Browser singleton --------------------------------------------------------------------------

#[derive(QObject, Default)]
pub struct BrowserObject {
    base: qt_base_class!(trait QObject),
    tabs: qt_property!(RefCell<TabModel>; CONST),
    history: qt_property!(RefCell<HistoryModel>; CONST),
    bookmarks: qt_property!(RefCell<BookmarkModel>; CONST),
    downloads: qt_property!(RefCell<DownloadModel>; CONST),
    permissions: qt_property!(RefCell<PermissionsObject>; CONST),
    prefs: qt_property!(RefCell<PrefsObject>; CONST),
    clipboard: qt_property!(RefCell<ClipboardObject>; CONST),
    engineName: qt_property!(QString; CONST),
    engineVersion: qt_property!(QString; CONST),
    version: qt_property!(QString; CONST),
    dataDirectory: qt_property!(QString; CONST),
    searchEngines: qt_property!(QVariantList; CONST),
    restoredAfterCrash: qt_property!(bool; NOTIFY changed),
    engineError: qt_property!(QString; NOTIFY changed),
    cosmeticRuleCount: qt_property!(i32; NOTIFY changed),
    proxyActive: qt_property!(bool; NOTIFY changed),
    changed: qt_signal!(),

    resolveInput: qt_method!(fn(&self, input: QString) -> QString),
    openUrl: qt_method!(fn(&mut self, url: QString, isPrivate: bool, inNewTab: bool)),
    openInput: qt_method!(fn(&mut self, input: QString, isPrivate: bool, inNewTab: bool)),
    saveSessionNow: qt_method!(fn(&mut self)),
    searchEngineName: qt_method!(fn(&self, id: QString) -> QString),
    clearBrowsingData: qt_method!(
        fn(&mut self, history: bool, cookies: bool, cache: bool, storage: bool, permissions: bool)
    ),
    rememberPermission:
        qt_method!(fn(&mut self, origin: QString, kind: i32, allow: bool, isPrivate: bool)),
    reloadCosmeticRules: qt_method!(fn(&mut self)),
    notify: qt_method!(fn(&mut self, text: QString)),
    share: qt_method!(fn(&mut self, url: QString, title: QString)),

    permissionPrompt: qt_signal!(request: QVariant, isPrivate: bool),
    dialogPrompt: qt_signal!(request: QVariant, isPrivate: bool),
    notificationRequested: qt_signal!(title: QString, body: QString),
    downloadStarted: qt_signal!(fileName: QString),
    engineCrashed: qt_signal!(reason: QString),
    shareRequested: qt_signal!(url: QString, title: QString),

    permission_requests: Vec<QObjectBox<PermissionRequestObject>>,
    dialog_requests: Vec<QObjectBox<DialogRequestObject>>,
}

impl QSingletonInit for BrowserObject {
    fn init(&mut self) {
        register_browser(QPointer::from(&*self));
        with_core_opt(|b| {
            self.engineName = qs(b.engine.name());
            self.engineVersion = qs(&b.engine.version());
            self.version = qs(b.version());
            self.dataDirectory = qs(&b.paths.data_dir.to_string_lossy());
            self.restoredAfterCrash = b.restored_after_crash();
            self.engineError = qs(b.engine_error().unwrap_or(""));
            self.cosmeticRuleCount = b.filter.rule_count() as i32;
            self.proxyActive = !b.proxy().is_direct();
        });
        self.searchEngines = search::ENGINES
            .iter()
            .map(|e| {
                let mut m = QVariantMap::default();
                m.insert(qs("id"), qs(e.id).into());
                m.insert(qs("name"), qs(e.name).into());
                m.insert(qs("homeUrl"), qs(e.home_url).into());
                QVariant::from(m)
            })
            .collect();
        self.tabs.borrow_mut().rebuild();
        self.history.borrow_mut().refresh();
        self.bookmarks.borrow_mut().refresh();
        self.downloads.borrow_mut().refresh();
        self.permissions.borrow_mut().refresh();
        self.prefs.borrow_mut().sync();
        self.changed();
        pump();
    }
}

impl BrowserObject {
    pub(crate) fn on_browser_event(&mut self, ev: BrowserEvent) {
        self.permission_requests
            .retain(|r| !r.pinned().borrow().answered);
        self.dialog_requests
            .retain(|r| !r.pinned().borrow().answered);
        match ev {
            BrowserEvent::Tab(t) => self.tabs.borrow_mut().apply(&t),
            BrowserEvent::Download(_) => self.downloads.borrow_mut().refresh(),
            BrowserEvent::PermissionPrompt {
                private, request, ..
            } => {
                let obj = PermissionRequestObject::wrap(request);
                let v = object_variant(&obj);
                self.permission_requests.push(obj);
                self.permissionPrompt(v, private);
            }
            BrowserEvent::DialogPrompt {
                private, request, ..
            } => {
                let obj = DialogRequestObject::wrap(request);
                let v = object_variant(&obj);
                self.dialog_requests.push(obj);
                self.dialogPrompt(v, private);
            }
            BrowserEvent::Notification { title, body } => {
                self.notificationRequested(qs(&title), qs(&body))
            }
            BrowserEvent::DownloadStarted { file_name } => {
                self.downloads.borrow_mut().refresh();
                self.downloadStarted(qs(&file_name));
            }
            BrowserEvent::EngineCrashed { reason } => {
                self.engineError = qs(&reason);
                self.changed();
                self.engineCrashed(qs(&reason));
            }
            BrowserEvent::HistoryChanged => self.history.borrow_mut().refresh(),
            BrowserEvent::BookmarksChanged => self.bookmarks.borrow_mut().refresh(),
            BrowserEvent::PermissionsChanged => self.permissions.borrow_mut().refresh(),
            BrowserEvent::ProxyChanged => {
                self.proxyActive = with_core_opt(|b| !b.proxy().is_direct()).unwrap_or(false);
                self.changed();
            }
            BrowserEvent::EngineInitialized
            | BrowserEvent::RenderContextLost
            | BrowserEvent::SessionSaveRequested => {}
            BrowserEvent::ContextMenu { .. }
            | BrowserEvent::ImeShow { .. }
            | BrowserEvent::ImeHide { .. }
            | BrowserEvent::ImeSelection { .. }
            | BrowserEvent::FrameReady { .. }
            | BrowserEvent::MediaSession { .. } => {}
        }
    }

    fn resolveInput(&self, input: QString) -> QString {
        qs(&with_core(|b| b.resolve_input(&input.to_string())).unwrap_or_default())
    }
    fn openUrl(&mut self, url: QString, isPrivate: bool, inNewTab: bool) {
        with_core(|b| b.open_url(&url.to_string(), isPrivate, inNewTab));
        pump();
    }
    fn openInput(&mut self, input: QString, isPrivate: bool, inNewTab: bool) {
        with_core(|b| b.open_input(&input.to_string(), isPrivate, inNewTab));
        pump();
    }
    fn saveSessionNow(&mut self) {
        with_core(|b| b.save_session_now());
    }
    fn searchEngineName(&self, id: QString) -> QString {
        qs(search::by_id(&id.to_string()).map(|e| e.name).unwrap_or(""))
    }
    fn clearBrowsingData(
        &mut self,
        history: bool,
        cookies: bool,
        cache: bool,
        storage: bool,
        permissions: bool,
    ) {
        with_core(|b| b.clear_browsing_data(history, cookies, cache, storage, permissions));
        pump();
    }
    fn rememberPermission(&mut self, origin: QString, kind: i32, allow: bool, isPrivate: bool) {
        if let Some(kind) = PermissionKind::from_index(kind.max(0) as u32) {
            with_core(|b| b.remember_permission(&origin.to_string(), kind, allow, isPrivate));
            pump();
        }
    }
    fn reloadCosmeticRules(&mut self) {
        self.cosmeticRuleCount = with_core(|b| b.reload_cosmetic_rules()) as i32;
        self.changed();
        pump();
    }
    fn notify(&mut self, text: QString) {
        self.notificationRequested(QString::default(), text);
    }
    fn share(&mut self, url: QString, title: QString) {
        self.shareRequested(url, title);
    }
}
