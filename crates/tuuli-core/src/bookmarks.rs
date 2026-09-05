// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Bookmarks in SQLite, user-ordered.

use std::path::Path;
use std::time::{SystemTime, UNIX_EPOCH};

use rusqlite::{params, Connection};

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Bookmark {
    pub id: i64,
    pub url: String,
    pub title: String,
    pub created_ms: i64,
    pub position: i64,
}

impl Bookmark {
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

pub struct BookmarkStore {
    conn: Connection,
}

impl BookmarkStore {
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
            "CREATE TABLE IF NOT EXISTS bookmarks (
                id INTEGER PRIMARY KEY,
                url TEXT NOT NULL UNIQUE,
                title TEXT,
                created INTEGER NOT NULL,
                position INTEGER NOT NULL DEFAULT 0);",
        )?;
        Ok(Self { conn })
    }

    pub fn add(&self, url: &str, title: &str) -> bool {
        if url.is_empty() || url::Url::parse(url).is_err() {
            return false;
        }
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as i64)
            .unwrap_or(0);
        matches!(
            self.conn.execute(
                "INSERT OR IGNORE INTO bookmarks(url, title, created, position)
                 VALUES(?1, ?2, ?3, (SELECT COALESCE(MAX(position), 0) + 1 FROM bookmarks))",
                params![url, title, now],
            ),
            Ok(n) if n > 0
        )
    }

    pub fn remove(&self, url: &str) -> bool {
        matches!(self.conn.execute("DELETE FROM bookmarks WHERE url = ?1", params![url]), Ok(n) if n > 0)
    }

    pub fn contains(&self, url: &str) -> bool {
        self.conn
            .query_row(
                "SELECT COUNT(*) FROM bookmarks WHERE url = ?1",
                params![url],
                |r| r.get::<_, i64>(0),
            )
            .unwrap_or(0)
            > 0
    }

    pub fn rename(&self, url: &str, title: &str) -> bool {
        matches!(self.conn.execute("UPDATE bookmarks SET title = ?1 WHERE url = ?2", params![title, url]), Ok(n) if n > 0)
    }

    pub fn all(&self) -> Vec<Bookmark> {
        self.conn
            .prepare("SELECT id, url, title, created, position FROM bookmarks ORDER BY position ASC, id ASC")
            .and_then(|mut s| {
                s.query_map([], |row| {
                    Ok(Bookmark {
                        id: row.get(0)?,
                        url: row.get(1)?,
                        title: row.get::<_, Option<String>>(2)?.unwrap_or_default(),
                        created_ms: row.get(3)?,
                        position: row.get(4)?,
                    })
                })
                .map(|r| r.filter_map(Result::ok).collect())
            })
            .unwrap_or_default()
    }

    pub fn count(&self) -> usize {
        self.conn
            .query_row("SELECT COUNT(*) FROM bookmarks", [], |r| r.get::<_, i64>(0))
            .unwrap_or(0) as usize
    }

    pub fn move_bookmark(&mut self, from: usize, to: usize) -> bool {
        let mut rows = self.all();
        if from >= rows.len() || to >= rows.len() || from == to {
            return false;
        }
        let moved = rows.remove(from);
        rows.insert(to, moved);
        let Ok(tx) = self.conn.transaction() else {
            return false;
        };
        for (i, b) in rows.iter().enumerate() {
            if tx
                .execute(
                    "UPDATE bookmarks SET position = ?1 WHERE id = ?2",
                    params![i as i64 + 1, b.id],
                )
                .is_err()
            {
                return false;
            }
        }
        tx.commit().is_ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_remove_contains_rename() {
        let b = BookmarkStore::open_in_memory().unwrap();
        assert!(b.add("https://a.example/", "A"));
        assert!(!b.add("https://a.example/", "dup"));
        assert!(!b.add("", "empty"));
        assert!(!b.add("not a url", "x"));
        assert!(b.contains("https://a.example/"));
        assert_eq!(b.count(), 1);
        assert!(b.rename("https://a.example/", "Renamed"));
        assert_eq!(b.all()[0].title, "Renamed");
        assert!(b.remove("https://a.example/"));
        assert!(!b.remove("https://a.example/"));
        assert_eq!(b.count(), 0);
    }

    #[test]
    fn order_and_move() {
        let mut b = BookmarkStore::open_in_memory().unwrap();
        b.add("https://1.example/", "1");
        b.add("https://2.example/", "2");
        b.add("https://3.example/", "3");
        assert_eq!(b.all()[0].title, "1");
        assert!(b.move_bookmark(0, 2));
        let all = b.all();
        assert_eq!(all[0].title, "2");
        assert_eq!(all[2].title, "1");
        assert!(!b.move_bookmark(0, 9));
        assert_eq!(all[2].display_title(), "1");
    }

    #[test]
    fn persists_to_disk() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("bm.sqlite");
        BookmarkStore::open(&path)
            .unwrap()
            .add("https://a.example/", "A");
        let again = BookmarkStore::open(&path).unwrap();
        assert_eq!(again.count(), 1);
        assert!(again.contains("https://a.example/"));
    }
}
