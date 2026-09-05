// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Browsing history in SQLite.  Private tabs never write here (spec 7.3);
//! the caller passes the privacy flag and we refuse.

use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use rusqlite::{params, Connection};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct HistoryEntry {
    pub url: String,
    pub title: String,
    pub visits: i64,
    pub last_visit_ms: i64,
}

impl HistoryEntry {
    /// Title, or the host when the page had none.
    pub fn display_title(&self) -> String {
        if !self.title.is_empty() {
            return self.title.clone();
        }
        url::Url::parse(&self.url)
            .ok()
            .and_then(|u| u.host_str().map(|h| h.to_string()))
            .unwrap_or_else(|| self.url.clone())
    }
}

pub struct HistoryStore {
    conn: Connection,
}

fn now_ms() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

fn is_recordable(url: &str) -> bool {
    url.starts_with("http://") || url.starts_with("https://")
}

impl HistoryStore {
    pub fn open(path: &Path) -> rusqlite::Result<Self> {
        if let Some(dir) = path.parent() {
            let _ = std::fs::create_dir_all(dir);
        }
        Self::init(Connection::open(path)?)
    }

    pub fn open_in_memory() -> rusqlite::Result<Self> {
        Self::init(Connection::open_in_memory()?)
    }

    fn init(conn: Connection) -> rusqlite::Result<Self> {
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS history (
                id INTEGER PRIMARY KEY,
                url TEXT NOT NULL UNIQUE,
                title TEXT,
                visits INTEGER NOT NULL DEFAULT 0,
                last_visit INTEGER NOT NULL DEFAULT 0);
             CREATE INDEX IF NOT EXISTS history_last_visit ON history(last_visit DESC);",
        )?;
        Ok(Self { conn })
    }

    pub fn add_visit(&self, url: &str, title: &str, private: bool) -> bool {
        if private || !is_recordable(url) {
            return false;
        }
        let now = now_ms();
        let updated = if title.is_empty() {
            self.conn.execute(
                "UPDATE history SET visits = visits + 1, last_visit = ?1 WHERE url = ?2",
                params![now, url],
            )
        } else {
            self.conn.execute(
                "UPDATE history SET visits = visits + 1, last_visit = ?1, title = ?2 WHERE url = ?3",
                params![now, title, url],
            )
        };
        match updated {
            Ok(0) => self
                .conn
                .execute(
                    "INSERT INTO history(url, title, visits, last_visit) VALUES(?1, ?2, 1, ?3)",
                    params![url, title, now],
                )
                .is_ok(),
            Ok(_) => true,
            Err(_) => false,
        }
    }

    pub fn update_title(&self, url: &str, title: &str, private: bool) -> bool {
        if private || title.is_empty() {
            return false;
        }
        matches!(self.conn.execute("UPDATE history SET title = ?1 WHERE url = ?2", params![title, url]), Ok(n) if n > 0)
    }

    pub fn remove(&self, url: &str) -> bool {
        self.conn
            .execute("DELETE FROM history WHERE url = ?1", params![url])
            .is_ok()
    }

    pub fn clear(&self) -> bool {
        self.conn.execute("DELETE FROM history", []).is_ok()
    }

    pub fn search(&self, text: &str, limit: usize) -> Vec<HistoryEntry> {
        let map = |row: &rusqlite::Row| -> rusqlite::Result<HistoryEntry> {
            Ok(HistoryEntry {
                url: row.get(0)?,
                title: row.get::<_, Option<String>>(1)?.unwrap_or_default(),
                visits: row.get(2)?,
                last_visit_ms: row.get(3)?,
            })
        };
        let result = if text.is_empty() {
            self.conn
                .prepare("SELECT url, title, visits, last_visit FROM history ORDER BY last_visit DESC LIMIT ?1")
                .and_then(|mut s| s.query_map(params![limit as i64], map).map(|r| r.filter_map(Result::ok).collect()))
        } else {
            let pat = format!("%{text}%");
            self.conn
                .prepare(
                    "SELECT url, title, visits, last_visit FROM history WHERE url LIKE ?1 OR title LIKE ?1
                     ORDER BY visits DESC, last_visit DESC LIMIT ?2",
                )
                .and_then(|mut s| s.query_map(params![pat, limit as i64], map).map(|r| r.filter_map(Result::ok).collect()))
        };
        result.unwrap_or_default()
    }

    pub fn total_count(&self) -> i64 {
        self.conn
            .query_row("SELECT COUNT(*) FROM history", [], |r| r.get(0))
            .unwrap_or(0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn adds_and_counts_visits() {
        let h = HistoryStore::open_in_memory().unwrap();
        assert!(h.add_visit("https://a.example/", "A", false));
        assert!(h.add_visit("https://a.example/", "", false));
        assert!(h.add_visit("https://b.example/", "B", false));
        assert_eq!(h.total_count(), 2);
        let all = h.search("", 10);
        let a = all.iter().find(|e| e.url == "https://a.example/").unwrap();
        assert_eq!(a.visits, 2);
        assert_eq!(a.title, "A");
    }

    #[test]
    fn private_and_non_http_never_recorded() {
        let h = HistoryStore::open_in_memory().unwrap();
        assert!(!h.add_visit("https://secret.example/", "S", true));
        assert!(!h.update_title("https://secret.example/", "S", true));
        assert!(!h.add_visit("about:blank", "", false));
        assert!(!h.add_visit("file:///etc/passwd", "", false));
        assert!(h.add_visit("http://a.example/", "", false));
        assert_eq!(h.total_count(), 1);
    }

    #[test]
    fn search_update_remove_clear() {
        let h = HistoryStore::open_in_memory().unwrap();
        h.add_visit("https://news.example/story", "Big news", false);
        h.add_visit("https://docs.example/", "", false);
        assert_eq!(h.search("news", 10).len(), 1);
        assert_eq!(h.search("", 1).len(), 1);
        let docs = h.search("docs", 10);
        assert_eq!(
            docs[0].display_title(),
            "docs.example",
            "untitled entries show their host"
        );
        assert!(h.update_title("https://docs.example/", "Docs", false));
        assert!(!h.update_title("https://missing.example/", "x", false));
        assert_eq!(h.search("Docs", 10)[0].title, "Docs");
        assert!(h.remove("https://docs.example/"));
        assert_eq!(h.total_count(), 1);
        assert!(h.clear());
        assert_eq!(h.total_count(), 0);
    }

    #[test]
    fn persists_to_disk() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("sub/history.sqlite");
        HistoryStore::open(&path)
            .unwrap()
            .add_visit("https://a.example/", "A", false);
        assert_eq!(HistoryStore::open(&path).unwrap().total_count(), 1);
    }
}
