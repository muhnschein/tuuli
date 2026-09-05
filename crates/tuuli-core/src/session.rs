// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Session persistence (spec 8.4): tabs, scroll offsets and zoom, written
//! after a debounce, on every backgrounding and on quit.  With a
//! single-process engine (spec 4.1) this is the crash mitigation, so it is
//! written atomically (temp file + rename) and never skipped.  The
//! debounce timer lives in the Qt layer; this store only knows whether a
//! save is pending.

use std::fs;
use std::io;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

pub const FORMAT_VERSION: u32 = 1;
pub const DEBOUNCE_MS: u64 = 5000;

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct SessionTab {
    pub url: String,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub scroll_x: f64,
    #[serde(default)]
    pub scroll_y: f64,
    #[serde(default = "one")]
    pub zoom: f64,
    #[serde(default)]
    pub desktop_mode: bool,
}

fn one() -> f64 {
    1.0
}

#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Session {
    #[serde(default)]
    pub tabs: Vec<SessionTab>,
    #[serde(default)]
    pub current_index: Option<usize>,
    #[serde(default)]
    pub clean_exit: bool,
}

#[derive(Serialize, Deserialize)]
struct SessionFile {
    version: u32,
    #[serde(flatten)]
    session: Session,
}

pub struct SessionStore {
    path: PathBuf,
    pending: Option<Session>,
}

impl SessionStore {
    pub fn new(path: impl Into<PathBuf>) -> Self {
        Self {
            path: path.into(),
            pending: None,
        }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    /// Queue a snapshot; the Qt layer flushes after the debounce.
    pub fn schedule_save(&mut self, session: Session) {
        self.pending = Some(session);
    }

    pub fn has_pending(&self) -> bool {
        self.pending.is_some()
    }

    pub fn flush(&mut self) -> io::Result<()> {
        match self.pending.take() {
            Some(s) => self.save_now(&s),
            None => Ok(()),
        }
    }

    pub fn save_now(&mut self, session: &Session) -> io::Result<()> {
        self.pending = None;
        let file = SessionFile {
            version: FORMAT_VERSION,
            session: session.clone(),
        };
        let data = serde_json::to_vec(&file).map_err(io::Error::other)?;
        write_atomically(&self.path, &data)
    }

    pub fn exists(&self) -> bool {
        self.path.exists()
    }

    pub fn remove(&self) -> io::Result<()> {
        fs::remove_file(&self.path)
    }

    pub fn load(&self) -> io::Result<Session> {
        let data = fs::read(&self.path)?;
        parse_session(&data)
    }
}

pub fn write_atomically(path: &Path, data: &[u8]) -> io::Result<()> {
    if let Some(dir) = path.parent() {
        fs::create_dir_all(dir)?;
    }
    let tmp = path.with_extension("json.tmp");
    fs::write(&tmp, data)?;
    fs::rename(&tmp, path)
}

pub fn parse_session(data: &[u8]) -> io::Result<Session> {
    let file: SessionFile =
        serde_json::from_slice(data).map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))?;
    if file.version > FORMAT_VERSION {
        return Err(io::Error::new(
            io::ErrorKind::InvalidData,
            format!(
                "session format {} is newer than {}",
                file.version, FORMAT_VERSION
            ),
        ));
    }
    let mut s = file.session;
    s.tabs.retain(|t| !t.url.is_empty());
    for t in &mut s.tabs {
        if t.zoom.is_nan() || t.zoom <= 0.0 {
            t.zoom = 1.0;
        }
    }
    s.current_index = match s.current_index {
        _ if s.tabs.is_empty() => None,
        Some(i) if i < s.tabs.len() => Some(i),
        _ => Some(s.tabs.len() - 1),
    };
    if s.current_index.is_none() && !s.tabs.is_empty() {
        s.current_index = Some(0);
    }
    Ok(s)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> Session {
        Session {
            tabs: vec![
                SessionTab {
                    url: "https://example.org/a".into(),
                    title: "A".into(),
                    scroll_x: 0.0,
                    scroll_y: 120.5,
                    zoom: 1.5,
                    desktop_mode: false,
                },
                SessionTab {
                    url: "https://example.org/b".into(),
                    title: "B".into(),
                    scroll_x: 0.0,
                    scroll_y: 0.0,
                    zoom: 1.0,
                    desktop_mode: true,
                },
            ],
            current_index: Some(1),
            clean_exit: false,
        }
    }

    #[test]
    fn save_and_load_round_trip() {
        let dir = tempfile::tempdir().unwrap();
        let mut store = SessionStore::new(dir.path().join("nested/session.json"));
        assert!(!store.exists());
        store.save_now(&sample()).unwrap();
        assert!(store.exists());
        assert_eq!(store.load().unwrap(), sample());
        assert!(!dir.path().join("nested/session.json.tmp").exists());
    }

    #[test]
    fn pending_and_flush() {
        let dir = tempfile::tempdir().unwrap();
        let mut store = SessionStore::new(dir.path().join("session.json"));
        for i in 0..20 {
            let mut s = sample();
            s.current_index = Some(i % 2);
            store.schedule_save(s);
        }
        assert!(store.has_pending());
        assert!(!store.exists());
        store.flush().unwrap();
        assert!(!store.has_pending());
        assert_eq!(store.load().unwrap().current_index, Some(1));
        store.flush().unwrap();
    }

    #[test]
    fn clean_exit_flag() {
        let dir = tempfile::tempdir().unwrap();
        let mut store = SessionStore::new(dir.path().join("session.json"));
        let mut s = sample();
        s.clean_exit = true;
        store.save_now(&s).unwrap();
        assert!(store.load().unwrap().clean_exit);
        s.clean_exit = false;
        store.save_now(&s).unwrap();
        assert!(!store.load().unwrap().clean_exit);
    }

    #[test]
    fn corrupt_missing_and_newer_files_fail() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("session.json");
        fs::write(&path, b"{ not json").unwrap();
        assert!(SessionStore::new(&path).load().is_err());
        assert!(SessionStore::new("/nonexistent/dir/session.json")
            .load()
            .is_err());
        assert!(parse_session(br#"{"version": 99, "tabs": []}"#).is_err());
    }

    #[test]
    fn invalid_entries_skipped_and_index_clamped() {
        let s = parse_session(br#"{"version":1,"tabs":[{"title":"no url","url":""},{"url":"https://a.example/","zoom":-3}],"current_index":7}"#).unwrap();
        assert_eq!(s.tabs.len(), 1);
        assert_eq!(s.tabs[0].zoom, 1.0);
        assert_eq!(s.current_index, Some(0));
        let s = parse_session(br#"{"version":1,"tabs":[{"url":"https://a.example/"}]}"#).unwrap();
        assert_eq!(s.current_index, Some(0));
    }
}
