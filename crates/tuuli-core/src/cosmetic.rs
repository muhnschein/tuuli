// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Cosmetic (element-hiding) filtering from an EasyList-derived rule set,
//! applied as a per-webview user stylesheet (spec 9.3, M3).
//!
//! This is deliberately NOT called ad blocking anywhere in the UI: network
//! requests are not intercepted.  Understood rule forms:
//!
//! ```text
//! ##selector                  generic hide
//! example.com##selector       hide on example.com and its subdomains
//! a.com,b.com##selector       several domains
//! ~a.com##selector            generic hide except on a.com
//! example.com#@#selector      exception: do not hide selector there
//! ```
//!
//! Network rules, comments, headers and scriptlet / extended-CSS rules are
//! ignored and counted.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::path::Path;

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Stats {
    pub generic_rules: usize,
    pub domain_rules: usize,
    pub exceptions: usize,
    pub ignored: usize,
}

#[derive(Clone, Debug)]
struct DomainRule {
    selector: String,
    exception: bool,
}

#[derive(Clone, Debug, Default)]
pub struct CosmeticFilter {
    generic: HashSet<String>,
    domain: HashMap<String, Vec<DomainRule>>,
    generic_excludes: HashMap<String, HashSet<String>>,
    stats: Stats,
}

const EXTENDED: &[&str] = &[
    ":has(",
    ":has-text(",
    ":contains(",
    ":matches-css",
    ":xpath(",
    ":-abp-",
    ":upward(",
    ":remove(",
    ":style(",
    ":if(",
    ":if-not(",
    ":nth-ancestor(",
    ":min-text-length(",
    ":watch-attr(",
    ":matches-path(",
    ":others(",
    ":matches-attr(",
    ":remove-attr(",
    ":remove-class(",
    ":matches-prop(",
];

impl CosmeticFilter {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn stats(&self) -> Stats {
        self.stats
    }
    pub fn is_empty(&self) -> bool {
        self.generic.is_empty() && self.domain.is_empty()
    }
    pub fn rule_count(&self) -> usize {
        self.stats.generic_rules + self.stats.domain_rules
    }

    pub fn clear(&mut self) {
        *self = Self::default();
    }

    pub fn add_rules(&mut self, list: &str) {
        for line in list.lines() {
            self.parse_line(line.trim());
        }
    }

    pub fn load_file(&mut self, path: &Path) -> bool {
        match std::fs::read_to_string(path) {
            Ok(text) => {
                self.add_rules(&text);
                true
            }
            Err(_) => false,
        }
    }

    /// Loads every `*.txt` in `dir`, sorted by name.  Returns the rule count.
    pub fn load_dir(&mut self, dir: &Path) -> usize {
        let mut files: Vec<_> = std::fs::read_dir(dir)
            .map(|rd| {
                rd.filter_map(Result::ok)
                    .map(|e| e.path())
                    .filter(|p| p.extension().map(|e| e == "txt").unwrap_or(false))
                    .collect()
            })
            .unwrap_or_default();
        files.sort();
        for f in files {
            self.load_file(&f);
        }
        self.rule_count()
    }

    fn parse_line(&mut self, line: &str) {
        if line.is_empty() || line.starts_with('!') || line.starts_with('[') {
            return;
        }
        let (sep, len, exception) = match line.find("#@#") {
            Some(i) => (i, 3, true),
            None => match line.find("##") {
                Some(i) => (i, 2, false),
                None => {
                    self.stats.ignored += 1;
                    return;
                }
            },
        };
        // Scriptlet / extended syntax: #?# #$# #%#
        if !exception {
            if let Some(c) = line[sep + 1..].chars().next() {
                if c == '?' || c == '$' || c == '%' {
                    self.stats.ignored += 1;
                    return;
                }
            }
        }
        let domains = &line[..sep];
        let selector = line[sep + len..].trim();
        if selector.is_empty() || EXTENDED.iter().any(|e| selector.contains(e)) {
            self.stats.ignored += 1;
            return;
        }

        if domains.is_empty() {
            if exception {
                self.generic.remove(selector);
                self.stats.exceptions += 1;
            } else {
                self.generic.insert(selector.to_string());
                self.stats.generic_rules += 1;
            }
            return;
        }

        let mut includes = Vec::new();
        let mut excludes = Vec::new();
        for d in domains.split(',') {
            let d = d.trim().to_lowercase();
            if let Some(rest) = d.strip_prefix('~') {
                excludes.push(rest.to_string());
            } else if !d.is_empty() {
                includes.push(d);
            }
        }

        if includes.is_empty() && !excludes.is_empty() && !exception {
            self.generic.insert(selector.to_string());
            let ex = self
                .generic_excludes
                .entry(selector.to_string())
                .or_default();
            for d in excludes {
                ex.insert(d);
            }
            self.stats.generic_rules += 1;
            return;
        }

        for d in includes {
            self.domain.entry(d).or_default().push(DomainRule {
                selector: selector.to_string(),
                exception,
            });
            if exception {
                self.stats.exceptions += 1;
            } else {
                self.stats.domain_rules += 1;
            }
        }
        for d in excludes {
            self.domain.entry(d).or_default().push(DomainRule {
                selector: selector.to_string(),
                exception: true,
            });
        }
    }

    pub fn host_matches_domain(host: &str, domain: &str) -> bool {
        if host.is_empty() || domain.is_empty() {
            return false;
        }
        host == domain
            || host
                .strip_suffix(domain)
                .map(|p| p.ends_with('.'))
                .unwrap_or(false)
    }

    pub fn host_of(url: &str) -> String {
        url::Url::parse(url)
            .ok()
            .and_then(|u| u.host_str().map(|h| h.to_lowercase()))
            .unwrap_or_default()
    }

    /// Selectors that apply to `host`: generic minus exceptions plus
    /// domain-specific ones.  Sorted for determinism.
    pub fn selectors_for(&self, host: &str) -> Vec<String> {
        let host = host.to_lowercase();
        let mut result: BTreeSet<String> = BTreeSet::new();
        let mut exceptions: BTreeSet<String> = BTreeSet::new();

        let mut probe = host.as_str();
        loop {
            if let Some(rules) = self.domain.get(probe) {
                for r in rules {
                    if r.exception {
                        exceptions.insert(r.selector.clone());
                    } else {
                        result.insert(r.selector.clone());
                    }
                }
            }
            match probe.find('.') {
                Some(i) => probe = &probe[i + 1..],
                None => break,
            }
        }

        for sel in &self.generic {
            if let Some(ex) = self.generic_excludes.get(sel) {
                if ex.iter().any(|d| Self::host_matches_domain(&host, d)) {
                    continue;
                }
            }
            result.insert(sel.clone());
        }
        for sel in exceptions {
            result.remove(&sel);
        }
        result.into_iter().collect()
    }

    /// The stylesheet the engine gets for `host`.  Empty when nothing
    /// applies.  Selectors are grouped `per_rule` at a time so a single
    /// bad selector only invalidates a small group.
    pub fn stylesheet_for(&self, host: &str, per_rule: usize) -> String {
        let selectors = self.selectors_for(host);
        if selectors.is_empty() {
            return String::new();
        }
        let group = per_rule.max(1);
        let mut css = String::with_capacity(selectors.len() * 32);
        for chunk in selectors.chunks(group) {
            css.push_str(&chunk.join(",\n"));
            css.push_str(" { display: none !important; }\n");
        }
        css
    }

    #[allow(dead_code)]
    fn sorted_domains(&self) -> BTreeMap<&String, usize> {
        self.domain.iter().map(|(k, v)| (k, v.len())).collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const LIST: &str = "[Adblock Plus 2.0]
! Title: test list
||ads.example^$third-party
##.ad-banner
##div[id^=\"google_ads\"]
example.com##.sidebar-promo
example.com,other.net###promo
example.com#@#.ad-banner
~news.example.org##.newsletter
example.com#?#.foo:has(> .bar)
example.com##.x:has-text(buy)
example.com#$#body { overflow: auto !important }
";

    fn filter() -> CosmeticFilter {
        let mut f = CosmeticFilter::new();
        f.add_rules(LIST);
        f
    }

    #[test]
    fn parses_supported_rules_only() {
        let s = filter().stats();
        assert_eq!(s.generic_rules, 3);
        assert_eq!(s.domain_rules, 3);
        assert_eq!(s.exceptions, 1);
        assert_eq!(s.ignored, 4);
        assert!(!filter().is_empty());
        assert_eq!(filter().rule_count(), 6);
    }

    #[test]
    fn generic_domain_exception_and_negation() {
        let f = filter();
        let sel = f.selectors_for("random.site");
        assert!(sel.contains(&".ad-banner".to_string()));
        assert!(sel.contains(&"div[id^=\"google_ads\"]".to_string()));
        assert!(sel.contains(&".newsletter".to_string()));
        assert!(!sel.contains(&".sidebar-promo".to_string()));

        assert!(f
            .selectors_for("example.com")
            .contains(&".sidebar-promo".to_string()));
        assert!(f
            .selectors_for("www.example.com")
            .contains(&".sidebar-promo".to_string()));
        assert!(f
            .selectors_for("WWW.EXAMPLE.COM")
            .contains(&"#promo".to_string()));
        assert!(f.selectors_for("other.net").contains(&"#promo".to_string()));
        assert!(!f
            .selectors_for("notexample.com")
            .contains(&".sidebar-promo".to_string()));

        assert!(!f
            .selectors_for("example.com")
            .contains(&".ad-banner".to_string()));
        assert!(!f
            .selectors_for("sub.example.com")
            .contains(&".ad-banner".to_string()));
        assert!(f
            .selectors_for("other.net")
            .contains(&".ad-banner".to_string()));

        assert!(!f
            .selectors_for("news.example.org")
            .contains(&".newsletter".to_string()));
        assert!(!f
            .selectors_for("m.news.example.org")
            .contains(&".newsletter".to_string()));
        assert!(f
            .selectors_for("example.org")
            .contains(&".newsletter".to_string()));
    }

    #[test]
    fn stylesheet_is_grouped_and_deterministic() {
        let mut f = CosmeticFilter::new();
        let list: String = (0..120).map(|i| format!("##.r{i}\n")).collect();
        f.add_rules(&list);
        let css = f.stylesheet_for("a.example", 50);
        assert_eq!(css.matches("display: none !important").count(), 3);
        assert_eq!(css, f.stylesheet_for("a.example", 50));
        assert!(f.stylesheet_for("a.example", 0).contains(".r0"));
        assert!(CosmeticFilter::new()
            .stylesheet_for("a.example", 50)
            .is_empty());
    }

    #[test]
    fn host_helpers_and_files() {
        assert!(CosmeticFilter::host_matches_domain("a.b.c", "b.c"));
        assert!(CosmeticFilter::host_matches_domain("b.c", "b.c"));
        assert!(!CosmeticFilter::host_matches_domain("ab.c", "b.c"));
        assert!(!CosmeticFilter::host_matches_domain("", "b.c"));
        assert_eq!(
            CosmeticFilter::host_of("https://WWW.Example.com/x"),
            "www.example.com"
        );

        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("list.txt"), LIST).unwrap();
        std::fs::write(dir.path().join("ignored.md"), "##.nope").unwrap();
        let mut f = CosmeticFilter::new();
        assert_eq!(f.load_dir(dir.path()), 6);
        assert!(!f.load_file(&dir.path().join("missing.txt")));
        f.clear();
        assert!(f.is_empty());
    }
}
