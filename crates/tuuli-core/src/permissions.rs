// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Per-origin permission decisions (spec 8.3): denied by default,
//! remembered per origin when the user asks, stored as JSON in the data
//! dir.  Private tabs consult stored decisions but never write them.

use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use crate::engine::{PermissionKind, PermissionRequest};
use crate::session::write_atomically;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Decision {
    Ask,
    Allow,
    Deny,
}

impl Decision {
    pub fn index(self) -> i32 {
        match self {
            Decision::Ask => 0,
            Decision::Allow => 1,
            Decision::Deny => 2,
        }
    }
    pub fn from_index(i: i32) -> Decision {
        match i {
            1 => Decision::Allow,
            2 => Decision::Deny,
            _ => Decision::Ask,
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PermissionEntry {
    pub origin: String,
    pub kind: PermissionKind,
    pub decision: Decision,
}

pub struct PermissionStore {
    path: Option<PathBuf>,
    decisions: BTreeMap<String, BTreeMap<u32, Decision>>,
}

impl PermissionStore {
    pub fn new(path: Option<PathBuf>) -> Self {
        let mut s = Self {
            path,
            decisions: BTreeMap::new(),
        };
        s.load();
        s
    }

    pub fn in_memory() -> Self {
        Self::new(None)
    }

    pub fn normalize_origin(origin: &str) -> String {
        match url::Url::parse(origin.trim()) {
            Ok(u) if u.host_str().is_some() => {
                let mut out = format!(
                    "{}://{}",
                    u.scheme().to_lowercase(),
                    u.host_str().unwrap_or("").to_lowercase()
                );
                if let Some(p) = u.port() {
                    out.push_str(&format!(":{p}"));
                }
                out
            }
            _ => origin.trim().to_lowercase(),
        }
    }

    pub fn decision(&self, origin: &str, kind: PermissionKind) -> Decision {
        self.decisions
            .get(&Self::normalize_origin(origin))
            .and_then(|k| k.get(&kind.index()))
            .copied()
            .unwrap_or(Decision::Ask)
    }

    pub fn set_decision(&mut self, origin: &str, kind: PermissionKind, decision: Decision) {
        let key = Self::normalize_origin(origin);
        if key.is_empty() {
            return;
        }
        match decision {
            Decision::Ask => {
                if let Some(kinds) = self.decisions.get_mut(&key) {
                    kinds.remove(&kind.index());
                    if kinds.is_empty() {
                        self.decisions.remove(&key);
                    }
                }
            }
            d => {
                self.decisions
                    .entry(key)
                    .or_default()
                    .insert(kind.index(), d);
            }
        }
        self.save();
    }

    pub fn clear_origin(&mut self, origin: &str) -> bool {
        let removed = self
            .decisions
            .remove(&Self::normalize_origin(origin))
            .is_some();
        if removed {
            self.save();
        }
        removed
    }

    pub fn clear_all(&mut self) {
        if !self.decisions.is_empty() {
            self.decisions.clear();
            self.save();
        }
    }

    pub fn count(&self) -> usize {
        self.decisions.values().map(|k| k.len()).sum()
    }

    /// Sorted by origin then kind, for the settings UI.
    pub fn entries(&self) -> Vec<PermissionEntry> {
        let mut out = Vec::new();
        for (origin, kinds) in &self.decisions {
            for (k, d) in kinds {
                if let Some(kind) = PermissionKind::from_index(*k) {
                    out.push(PermissionEntry {
                        origin: origin.clone(),
                        kind,
                        decision: *d,
                    });
                }
            }
        }
        out
    }

    /// Answer a request from the store if a decision exists.  Returns true
    /// when handled; otherwise the caller must prompt.
    pub fn answer_from_store(&self, request: &mut PermissionRequest) -> bool {
        match self.decision(&request.origin, request.kind) {
            Decision::Allow => {
                request.allow();
                true
            }
            Decision::Deny => {
                request.deny();
                true
            }
            Decision::Ask => false,
        }
    }

    fn load(&mut self) {
        let Some(path) = &self.path else { return };
        let Ok(data) = std::fs::read(path) else {
            return;
        };
        let Ok(root) = serde_json::from_slice::<serde_json::Map<String, serde_json::Value>>(&data)
        else {
            return;
        };
        for (origin, kinds) in root {
            let Some(kinds) = kinds.as_object() else {
                continue;
            };
            for (k, v) in kinds {
                let Ok(kind) = k.parse::<u32>() else { continue };
                let d = match v.as_str() {
                    Some("allow") => Decision::Allow,
                    Some("deny") => Decision::Deny,
                    _ => continue,
                };
                self.decisions
                    .entry(origin.clone())
                    .or_default()
                    .insert(kind, d);
            }
        }
    }

    pub fn save(&self) -> bool {
        let Some(path) = &self.path else { return false };
        let mut root = serde_json::Map::new();
        for (origin, kinds) in &self.decisions {
            let mut m = serde_json::Map::new();
            for (k, d) in kinds {
                m.insert(
                    k.to_string(),
                    serde_json::Value::String(if *d == Decision::Allow {
                        "allow".into()
                    } else {
                        "deny".into()
                    }),
                );
            }
            root.insert(origin.clone(), serde_json::Value::Object(m));
        }
        let data = serde_json::to_vec_pretty(&root).unwrap_or_default();
        write_atomically(Path::new(path), &data).is_ok()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::Cell;
    use std::rc::Rc;

    #[test]
    fn default_is_ask_and_origins_normalize() {
        let s = PermissionStore::in_memory();
        assert_eq!(
            s.decision("https://a.example", PermissionKind::Geolocation),
            Decision::Ask
        );
        assert_eq!(s.count(), 0);
        assert_eq!(
            PermissionStore::normalize_origin("HTTPS://A.Example/path?x"),
            "https://a.example"
        );
        assert_eq!(
            PermissionStore::normalize_origin("https://a.example:8443/"),
            "https://a.example:8443"
        );
        assert_eq!(PermissionStore::normalize_origin(" weird "), "weird");
    }

    #[test]
    fn stores_and_persists() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("p.json");
        {
            let mut s = PermissionStore::new(Some(path.clone()));
            s.set_decision(
                "https://a.example/x",
                PermissionKind::Geolocation,
                Decision::Allow,
            );
            s.set_decision("https://a.example", PermissionKind::Camera, Decision::Deny);
            assert_eq!(s.count(), 2);
        }
        let again = PermissionStore::new(Some(path));
        assert_eq!(
            again.decision("https://a.example", PermissionKind::Geolocation),
            Decision::Allow
        );
        assert_eq!(
            again.decision("https://a.example", PermissionKind::Camera),
            Decision::Deny
        );
        assert_eq!(
            again.decision("https://a.example", PermissionKind::Microphone),
            Decision::Ask
        );
        let entries = again.entries();
        assert_eq!(entries.len(), 2);
        assert_eq!(entries[0].kind, PermissionKind::Geolocation);
    }

    #[test]
    fn ask_removes_and_clear_works() {
        let mut s = PermissionStore::in_memory();
        s.set_decision(
            "https://a.example",
            PermissionKind::Geolocation,
            Decision::Allow,
        );
        s.set_decision(
            "https://b.example",
            PermissionKind::Geolocation,
            Decision::Allow,
        );
        s.set_decision(
            "https://a.example",
            PermissionKind::Geolocation,
            Decision::Ask,
        );
        assert_eq!(s.count(), 1);
        assert!(s.clear_origin("https://b.example"));
        assert!(!s.clear_origin("https://b.example"));
        s.set_decision(
            "https://c.example",
            PermissionKind::Notifications,
            Decision::Deny,
        );
        s.clear_all();
        assert_eq!(s.count(), 0);
    }

    #[test]
    fn answers_requests_from_store() {
        let mut s = PermissionStore::in_memory();
        s.set_decision(
            "https://a.example",
            PermissionKind::Geolocation,
            Decision::Allow,
        );
        s.set_decision(
            "https://d.example",
            PermissionKind::Geolocation,
            Decision::Deny,
        );
        let got = Rc::new(Cell::new(None));
        let g = got.clone();
        let mut r = PermissionRequest::new(
            PermissionKind::Geolocation,
            "https://a.example",
            move |ok| g.set(Some(ok)),
        );
        assert!(s.answer_from_store(&mut r));
        assert_eq!(got.get(), Some(true));
        let g = got.clone();
        let mut r = PermissionRequest::new(
            PermissionKind::Geolocation,
            "https://d.example",
            move |ok| g.set(Some(ok)),
        );
        assert!(s.answer_from_store(&mut r));
        assert_eq!(got.get(), Some(false));
        let g = got.clone();
        let mut r =
            PermissionRequest::new(PermissionKind::Camera, "https://a.example", move |ok| {
                g.set(Some(ok))
            });
        assert!(!s.answer_from_store(&mut r));
        drop(r);
        assert_eq!(got.get(), Some(false), "dismissed prompt denies");
    }
}
