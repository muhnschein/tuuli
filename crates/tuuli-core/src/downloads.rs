// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Downloads (spec 7.1): the engine performs the transfer, we pick the
//! destination and mirror progress into Nemo Transfer Engine so the system
//! Transfers page shows it.  Downloads from private tabs are listed only
//! for the session and never registered with Transfer Engine (spec 7.3).

use std::path::{Path, PathBuf};

use crate::engine::DownloadRequest;
use crate::tabs::TabId;

/// Nemo Transfer Engine, as the Qt layer implements it over D-Bus.
pub trait TransferEngine {
    /// Returns the transfer id.
    fn create_download(
        &self,
        display_name: &str,
        path: &Path,
        mime: &str,
        expected_size: i64,
    ) -> Option<i32>;
    fn start(&self, transfer_id: i32);
    fn update_progress(&self, transfer_id: i32, progress: f64);
    fn finish(&self, transfer_id: i32, status: TransferStatus, reason: &str);
}

/// org.nemo.transferengine TransferStatus values.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum TransferStatus {
    Unknown = 0,
    NotStarted = 1,
    Started = 2,
    Canceled = 3,
    Finished = 4,
    Interrupted = 5,
}

#[derive(Clone, Debug, PartialEq)]
pub struct DownloadItem {
    pub id: u64,
    pub tab: TabId,
    /// The engine's id for this transfer.
    pub engine_id: u64,
    pub url: String,
    pub file_name: String,
    pub path: PathBuf,
    pub mime_type: String,
    pub received: i64,
    pub total: i64,
    pub finished: bool,
    pub ok: bool,
    pub error: String,
    pub private: bool,
    pub transfer_id: Option<i32>,
}

impl DownloadItem {
    pub fn progress(&self) -> f64 {
        if self.total > 0 {
            self.received as f64 / self.total as f64
        } else if self.finished {
            1.0
        } else {
            0.0
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DownloadEvent {
    Inserted(usize),
    Changed(usize),
    Removed(usize),
    Started(u64),
    Finished(u64),
}

pub struct DownloadManager {
    directory: PathBuf,
    items: Vec<DownloadItem>,
    next_id: u64,
    transfers: Option<Box<dyn TransferEngine>>,
    events: Vec<DownloadEvent>,
}

pub fn sanitize_file_name(name: &str) -> String {
    let mut out: String = name
        .trim()
        .chars()
        .map(|c| {
            if c == '/' || c == '\\' || c == '\0' {
                '_'
            } else {
                c
            }
        })
        .collect();
    while out.starts_with('.') {
        out.remove(0);
    }
    if out.is_empty() {
        out = "download".into();
    }
    if out.chars().count() > 200 {
        out = out.chars().take(200).collect();
    }
    out
}

pub fn unique_path(directory: &Path, suggested_name: &str) -> PathBuf {
    let name = sanitize_file_name(suggested_name);
    let (base, suffix) = match name.find('.') {
        Some(i) if i > 0 => (name[..i].to_string(), Some(name[i + 1..].to_string())),
        _ => (name.clone(), None),
    };
    let mut candidate = directory.join(&name);
    let mut n = 1;
    while candidate.exists() {
        let mut alt = format!("{base}({n})");
        if let Some(s) = &suffix {
            alt.push('.');
            alt.push_str(s);
        }
        candidate = directory.join(alt);
        n += 1;
    }
    candidate
}

impl DownloadManager {
    pub fn new(directory: impl Into<PathBuf>, transfers: Option<Box<dyn TransferEngine>>) -> Self {
        Self {
            directory: directory.into(),
            items: Vec::new(),
            next_id: 1,
            transfers,
            events: Vec::new(),
        }
    }

    pub fn directory(&self) -> &Path {
        &self.directory
    }
    pub fn set_directory(&mut self, dir: impl Into<PathBuf>) {
        self.directory = dir.into();
    }
    pub fn items(&self) -> &[DownloadItem] {
        &self.items
    }
    pub fn len(&self) -> usize {
        self.items.len()
    }
    pub fn is_empty(&self) -> bool {
        self.items.is_empty()
    }
    pub fn active_count(&self) -> usize {
        self.items.iter().filter(|d| !d.finished).count()
    }
    pub fn take_events(&mut self) -> Vec<DownloadEvent> {
        std::mem::take(&mut self.events)
    }

    fn row_of(&self, id: u64) -> Option<usize> {
        self.items.iter().position(|d| d.id == id)
    }

    /// Accepts the request into the download directory.  Returns the item id.
    pub fn handle_request(
        &mut self,
        tab: TabId,
        mut request: DownloadRequest,
        private: bool,
    ) -> u64 {
        let _ = std::fs::create_dir_all(&self.directory);
        let mut name = request.suggested_name.clone();
        if name.is_empty() {
            name = url::Url::parse(&request.url)
                .ok()
                .and_then(|u| {
                    u.path_segments()
                        .and_then(|mut s| s.next_back().map(|s| s.to_string()))
                })
                .unwrap_or_default();
        }
        let path = unique_path(&self.directory, &name);
        let file_name = path
            .file_name()
            .map(|f| f.to_string_lossy().to_string())
            .unwrap_or_default();
        let id = self.next_id;
        self.next_id += 1;
        let mut item = DownloadItem {
            id,
            tab,
            engine_id: request.id,
            url: request.url.clone(),
            file_name: file_name.clone(),
            path: path.clone(),
            mime_type: request.mime_type.clone(),
            received: 0,
            total: request.total_bytes,
            finished: false,
            ok: false,
            error: String::new(),
            private,
            transfer_id: None,
        };
        if !private {
            if let Some(t) = &self.transfers {
                item.transfer_id =
                    t.create_download(&file_name, &path, &item.mime_type, item.total);
                if let Some(tid) = item.transfer_id {
                    t.start(tid);
                }
            }
        }
        let row = self.items.len();
        self.items.push(item);
        self.events.push(DownloadEvent::Inserted(row));
        self.events.push(DownloadEvent::Started(id));
        request.accept(path);
        id
    }

    pub fn progress(&mut self, tab: TabId, engine_id: u64, received: i64, total: i64) {
        let Some(row) = self
            .items
            .iter()
            .position(|d| d.tab == tab && d.engine_id == engine_id)
        else {
            return;
        };
        let d = &mut self.items[row];
        d.received = received;
        if total > 0 {
            d.total = total;
        }
        if let (Some(tid), Some(t)) = (d.transfer_id, &self.transfers) {
            if d.total > 0 {
                t.update_progress(tid, received as f64 / d.total as f64);
            }
        }
        self.events.push(DownloadEvent::Changed(row));
    }

    pub fn finished(&mut self, tab: TabId, engine_id: u64, ok: bool, error: &str) {
        let Some(row) = self
            .items
            .iter()
            .position(|d| d.tab == tab && d.engine_id == engine_id && !d.finished)
        else {
            return;
        };
        let d = &mut self.items[row];
        d.finished = true;
        d.ok = ok;
        d.error = error.to_string();
        if ok && d.total > 0 {
            d.received = d.total;
        }
        if let (Some(tid), Some(t)) = (d.transfer_id, &self.transfers) {
            t.finish(
                tid,
                if ok {
                    TransferStatus::Finished
                } else {
                    TransferStatus::Interrupted
                },
                error,
            );
        }
        let id = d.id;
        self.events.push(DownloadEvent::Changed(row));
        self.events.push(DownloadEvent::Finished(id));
    }

    /// Marks the item cancelled; returns `(tab, engine_id)` for the caller
    /// to cancel in the engine.
    pub fn cancel(&mut self, id: u64) -> Option<(TabId, u64)> {
        let row = self.row_of(id)?;
        let d = &mut self.items[row];
        if d.finished {
            return None;
        }
        d.finished = true;
        d.ok = false;
        d.error = "cancelled".into();
        if let (Some(tid), Some(t)) = (d.transfer_id, &self.transfers) {
            t.finish(tid, TransferStatus::Canceled, "");
        }
        let out = (d.tab, d.engine_id);
        self.events.push(DownloadEvent::Changed(row));
        Some(out)
    }

    pub fn remove(&mut self, id: u64) -> Option<(TabId, u64)> {
        let row = self.row_of(id)?;
        let to_cancel = if !self.items[row].finished {
            self.cancel(id)
        } else {
            None
        };
        self.items.remove(row);
        self.events.push(DownloadEvent::Removed(row));
        to_cancel
    }

    pub fn clear_finished(&mut self) {
        let ids: Vec<u64> = self
            .items
            .iter()
            .filter(|d| d.finished)
            .map(|d| d.id)
            .collect();
        for id in ids {
            self.remove(id);
        }
    }

    pub fn clear_private(&mut self) -> Vec<(TabId, u64)> {
        let ids: Vec<u64> = self
            .items
            .iter()
            .filter(|d| d.private)
            .map(|d| d.id)
            .collect();
        ids.into_iter().filter_map(|id| self.remove(id)).collect()
    }

    /// Transfer Engine asked us to cancel (D-Bus callback).
    pub fn cancel_transfer(&mut self, transfer_id: i32) -> Option<(TabId, u64)> {
        let id = self
            .items
            .iter()
            .find(|d| d.transfer_id == Some(transfer_id))
            .map(|d| d.id)?;
        self.cancel(id)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::rc::Rc;

    #[test]
    fn sanitizes_names() {
        assert_eq!(sanitize_file_name("../../etc/passwd"), "_.._etc_passwd");
        assert_eq!(sanitize_file_name("..hidden"), "hidden");
        assert_eq!(sanitize_file_name("   "), "download");
        assert_eq!(sanitize_file_name(&"a".repeat(300)).len(), 200);
    }

    #[test]
    fn unique_paths_avoid_collisions() {
        let dir = tempfile::tempdir().unwrap();
        let first = unique_path(dir.path(), "file.tar.gz");
        assert_eq!(first.file_name().unwrap(), "file.tar.gz");
        std::fs::write(&first, b"x").unwrap();
        let second = unique_path(dir.path(), "file.tar.gz");
        assert_eq!(second.file_name().unwrap(), "file(1).tar.gz");
    }

    #[derive(Default)]
    struct FakeTransfers {
        calls: RefCell<Vec<String>>,
    }
    impl TransferEngine for Rc<FakeTransfers> {
        fn create_download(
            &self,
            name: &str,
            _path: &Path,
            _mime: &str,
            _size: i64,
        ) -> Option<i32> {
            self.calls.borrow_mut().push(format!("create {name}"));
            Some(42)
        }
        fn start(&self, id: i32) {
            self.calls.borrow_mut().push(format!("start {id}"));
        }
        fn update_progress(&self, id: i32, p: f64) {
            self.calls
                .borrow_mut()
                .push(format!("progress {id} {p:.2}"));
        }
        fn finish(&self, id: i32, status: TransferStatus, _reason: &str) {
            self.calls
                .borrow_mut()
                .push(format!("finish {id} {status:?}"));
        }
    }

    #[test]
    fn handles_download_to_completion_with_transfer_engine() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Rc::new(FakeTransfers::default());
        let mut dm = DownloadManager::new(dir.path(), Some(Box::new(fake.clone())));
        let accepted = Rc::new(RefCell::new(None));
        let a = accepted.clone();
        let req = DownloadRequest::new(
            7,
            "https://a.example/big.bin",
            "big.bin",
            "application/octet-stream",
            1000,
            move |p| *a.borrow_mut() = p,
        );
        let id = dm.handle_request(3, req, false);
        assert_eq!(dm.len(), 1);
        assert_eq!(dm.active_count(), 1);
        assert_eq!(dm.items()[0].file_name, "big.bin");
        assert!(accepted.borrow().as_ref().unwrap().starts_with(dir.path()));
        dm.progress(3, 7, 500, 1000);
        assert_eq!(dm.items()[0].progress(), 0.5);
        dm.finished(3, 7, true, "");
        assert_eq!(dm.active_count(), 0);
        assert!(dm.items()[0].ok);
        assert_eq!(dm.items()[0].progress(), 1.0);
        let calls = fake.calls.borrow().clone();
        assert_eq!(
            calls,
            vec![
                "create big.bin",
                "start 42",
                "progress 42 0.50",
                "finish 42 Finished"
            ]
        );
        let events = dm.take_events();
        assert!(events.contains(&DownloadEvent::Finished(id)));
        dm.clear_finished();
        assert_eq!(dm.len(), 0);
    }

    #[test]
    fn private_downloads_skip_transfer_engine_and_clear() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Rc::new(FakeTransfers::default());
        let mut dm = DownloadManager::new(dir.path(), Some(Box::new(fake.clone())));
        let req = DownloadRequest::new(1, "https://a.example/x", "", "", -1, |_| {});
        let id = dm.handle_request(1, req, true);
        assert!(dm.items()[0].private);
        assert_eq!(dm.items()[0].file_name, "x");
        assert!(dm.items()[0].transfer_id.is_none());
        assert!(fake.calls.borrow().is_empty());
        let cancelled = dm.clear_private();
        assert_eq!(cancelled, vec![(1, 1)]);
        assert_eq!(dm.len(), 0);
        assert!(dm.cancel(id).is_none());
    }

    #[test]
    fn cancel_via_transfer_engine_callback() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Rc::new(FakeTransfers::default());
        let mut dm = DownloadManager::new(dir.path(), Some(Box::new(fake.clone())));
        dm.handle_request(
            2,
            DownloadRequest::new(9, "https://a.example/y", "y", "", 10, |_| {}),
            false,
        );
        assert_eq!(dm.cancel_transfer(42), Some((2, 9)));
        assert!(dm.items()[0].finished && !dm.items()[0].ok);
        assert!(fake.calls.borrow().last().unwrap().contains("Canceled"));
        assert_eq!(dm.cancel_transfer(42), None);
    }
}
