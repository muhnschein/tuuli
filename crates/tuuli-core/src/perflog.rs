// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! On-device timing samples for the spec 11 budgets, as JSON lines that
//! `tools/perf/run-budgets.py` evaluates.  Enabled by the "Performance
//! logging" developer toggle; otherwise every call is a no-op.

use std::collections::HashMap;
use std::fs::{File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};
use std::time::Instant;

use crate::tabs::TabId;

const CORPUS: &[(&str, &str)] = &[
    ("www.theguardian.com", "news-article"),
    ("app.tuta.com", "webmail"),
    ("forum.sailfishos.org", "forum-thread"),
    ("book.servo.org", "docs-site"),
    ("en.wikipedia.org", "wiki"),
    ("duckduckgo.com", "search-results"),
    ("github.com", "github-file"),
    ("fosstodon.org", "mastodon"),
    ("shop.jolla.com", "webshop"),
    ("excalidraw.com", "heavy-spa"),
];

/// Corpus id for a URL (tools/corpus/pages.json) or the host.
pub fn page_id_for(url: &str) -> String {
    let host = url::Url::parse(url)
        .ok()
        .and_then(|u| u.host_str().map(|h| h.to_lowercase()))
        .unwrap_or_default();
    CORPUS
        .iter()
        .find(|(h, _)| *h == host)
        .map(|(_, id)| id.to_string())
        .unwrap_or(host)
}

pub fn resident_set_mb() -> i64 {
    let Ok(statm) = std::fs::read_to_string("/proc/self/statm") else {
        return -1;
    };
    let pages: i64 = statm
        .split_whitespace()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(-1);
    if pages < 0 {
        return -1;
    }
    pages * 4096 / (1024 * 1024)
}

struct Nav {
    started: Instant,
    url: String,
    loaded: bool,
}

struct Interaction {
    kind: String,
    url: String,
    started: Instant,
    frames: u32,
    dropped: u32,
}

pub struct PerfLog {
    path: PathBuf,
    file: Option<File>,
    process_start: Instant,
    first_paint_logged: bool,
    navs: HashMap<TabId, Nav>,
    interaction: Option<Interaction>,
}

impl PerfLog {
    pub fn new(path: impl Into<PathBuf>) -> Self {
        Self {
            path: path.into(),
            file: None,
            process_start: Instant::now(),
            first_paint_logged: false,
            navs: HashMap::new(),
            interaction: None,
        }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }
    pub fn is_enabled(&self) -> bool {
        self.file.is_some()
    }

    pub fn set_enabled(&mut self, on: bool) {
        if on == self.is_enabled() {
            return;
        }
        if on {
            if let Some(dir) = self.path.parent() {
                let _ = std::fs::create_dir_all(dir);
            }
            self.file = OpenOptions::new()
                .append(true)
                .create(true)
                .open(&self.path)
                .ok();
        } else {
            self.file = None;
        }
    }

    fn write(&mut self, mut record: serde_json::Map<String, serde_json::Value>) {
        let Some(file) = self.file.as_mut() else {
            return;
        };
        record.insert(
            "t_ms".into(),
            (self.process_start.elapsed().as_millis() as u64).into(),
        );
        let line = serde_json::Value::Object(record).to_string();
        let _ = writeln!(file, "{line}");
        let _ = file.flush();
    }

    fn rec(kind: &str) -> serde_json::Map<String, serde_json::Value> {
        let mut m = serde_json::Map::new();
        m.insert("kind".into(), kind.into());
        m
    }

    pub fn mark_first_paint(&mut self, cold_start: bool) {
        if self.first_paint_logged {
            return;
        }
        self.first_paint_logged = true;
        let mut r = Self::rec("start");
        r.insert("cold".into(), cold_start.into());
        r.insert(
            "first_paint_ms".into(),
            (self.process_start.elapsed().as_millis() as u64).into(),
        );
        self.write(r);
    }

    pub fn navigation_started(&mut self, tab: TabId, url: &str) {
        if self.is_enabled() {
            self.navs.insert(
                tab,
                Nav {
                    started: Instant::now(),
                    url: url.to_string(),
                    loaded: false,
                },
            );
        }
    }

    pub fn load_finished(&mut self, tab: TabId) {
        if let Some(n) = self.navs.get_mut(&tab) {
            n.loaded = true;
        }
    }

    /// First frame after the load finished is the first contentful paint we
    /// can observe.
    pub fn frame_ready(&mut self, tab: TabId, open_tabs: usize) {
        let Some(n) = self.navs.get(&tab) else { return };
        if !n.loaded {
            return;
        }
        let n = self.navs.remove(&tab).expect("present");
        let mut r = Self::rec("load");
        r.insert("page".into(), page_id_for(&n.url).into());
        r.insert("url".into(), n.url.into());
        r.insert(
            "fcp_ms".into(),
            (n.started.elapsed().as_millis() as u64).into(),
        );
        r.insert("rss_mb".into(), resident_set_mb().into());
        r.insert("tabs".into(), (open_tabs as u64).into());
        self.write(r);
    }

    pub fn interaction_begin(&mut self, kind: &str, url: &str) {
        if self.is_enabled() {
            self.interaction = Some(Interaction {
                kind: kind.into(),
                url: url.into(),
                started: Instant::now(),
                frames: 0,
                dropped: 0,
            });
        }
    }

    pub fn interaction_frame(&mut self, frame_ms: f64, budget_ms: f64) {
        if let Some(i) = self.interaction.as_mut() {
            i.frames += 1;
            if budget_ms > 0.0 && frame_ms > budget_ms * 1.5 {
                i.dropped += (frame_ms / budget_ms) as u32 - 1;
            }
        }
    }

    pub fn interaction_end(&mut self) {
        let Some(i) = self.interaction.take() else {
            return;
        };
        if i.frames < 5 {
            return;
        }
        let mut r = Self::rec("frames");
        r.insert("page".into(), page_id_for(&i.url).into());
        r.insert("interaction".into(), i.kind.into());
        r.insert("frames".into(), i.frames.into());
        r.insert("dropped".into(), i.dropped.into());
        r.insert(
            "duration_ms".into(),
            (i.started.elapsed().as_millis() as u64).into(),
        );
        self.write(r);
    }

    pub fn sample_rss(&mut self, open_tabs: usize) {
        let mut r = Self::rec("rss");
        r.insert("tabs".into(), (open_tabs as u64).into());
        r.insert("rss_mb".into(), resident_set_mb().into());
        self.write(r);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn corpus_ids() {
        assert_eq!(page_id_for("https://en.wikipedia.org/wiki/X"), "wiki");
        assert_eq!(page_id_for("https://other.example/"), "other.example");
    }

    #[test]
    fn writes_records_when_enabled() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("sub/perf.log");
        let mut log = PerfLog::new(&path);
        log.mark_first_paint(true);
        assert!(!path.exists(), "disabled: nothing written");
        log.set_enabled(true);
        log.navigation_started(1, "https://book.servo.org/");
        log.load_finished(1);
        log.frame_ready(1, 1);
        log.interaction_begin("scroll", "https://book.servo.org/");
        for _ in 0..10 {
            log.interaction_frame(40.0, 16.7);
        }
        log.interaction_end();
        log.sample_rss(3);
        log.set_enabled(false);
        let text = std::fs::read_to_string(&path).unwrap();
        let lines: Vec<&str> = text.lines().collect();
        assert_eq!(lines.len(), 3);
        assert!(
            lines[0].contains("\"kind\":\"load\"") && lines[0].contains("\"page\":\"docs-site\"")
        );
        assert!(
            lines[1].contains("\"interaction\":\"scroll\"") && lines[1].contains("\"dropped\":10")
        );
        assert!(lines[2].contains("\"kind\":\"rss\""));
    }
}
