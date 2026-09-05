// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Built-in search engines (spec 9.4: non-tracking default, user-changeable,
//! no revenue arrangement of any kind) and the URL-or-search resolver used
//! by the address field.

use percent_encoding::{utf8_percent_encode, NON_ALPHANUMERIC};
use regex::Regex;
use std::sync::OnceLock;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SearchEngine {
    pub id: &'static str,
    pub name: &'static str,
    /// Contains `{searchTerms}`.
    pub search_url: &'static str,
    pub home_url: &'static str,
}

pub const ENGINES: &[SearchEngine] = &[
    SearchEngine {
        id: "duckduckgo",
        name: "DuckDuckGo",
        search_url: "https://duckduckgo.com/?q={searchTerms}",
        home_url: "https://duckduckgo.com/",
    },
    SearchEngine {
        id: "startpage",
        name: "Startpage",
        search_url: "https://www.startpage.com/do/search?q={searchTerms}",
        home_url: "https://www.startpage.com/",
    },
    SearchEngine {
        id: "qwant",
        name: "Qwant",
        search_url: "https://www.qwant.com/?q={searchTerms}",
        home_url: "https://www.qwant.com/",
    },
    SearchEngine {
        id: "mojeek",
        name: "Mojeek",
        search_url: "https://www.mojeek.com/search?q={searchTerms}",
        home_url: "https://www.mojeek.com/",
    },
    SearchEngine {
        id: "brave",
        name: "Brave Search",
        search_url: "https://search.brave.com/search?q={searchTerms}",
        home_url: "https://search.brave.com/",
    },
    SearchEngine {
        id: "wikipedia",
        name: "Wikipedia",
        search_url: "https://en.wikipedia.org/w/index.php?search={searchTerms}",
        home_url: "https://en.wikipedia.org/",
    },
];

pub const DEFAULT_ENGINE: &str = "duckduckgo";

pub fn by_id(id: &str) -> Option<&'static SearchEngine> {
    ENGINES.iter().find(|e| e.id == id)
}

pub fn search_url(engine_id: &str, terms: &str) -> String {
    let e = by_id(engine_id)
        .or_else(|| by_id(DEFAULT_ENGINE))
        .expect("default engine exists");
    let encoded = utf8_percent_encode(terms, NON_ALPHANUMERIC).to_string();
    e.search_url.replace("{searchTerms}", &encoded)
}

fn regexes() -> &'static (Regex, Regex, Regex, Regex, Regex) {
    static R: OnceLock<(Regex, Regex, Regex, Regex, Regex)> = OnceLock::new();
    R.get_or_init(|| {
        (
            Regex::new(r"^[a-zA-Z][a-zA-Z0-9+.-]*://").unwrap(),
            Regex::new(r"(?i)^(about|file|data|blob):").unwrap(),
            Regex::new(r"^[a-zA-Z0-9.-]+:[0-9]{1,5}(/.*)?$").unwrap(),
            Regex::new(r"^[a-zA-Z][a-zA-Z0-9+.-]*:").unwrap(),
            Regex::new(r"^([a-zA-Z0-9]([a-zA-Z0-9-]*[a-zA-Z0-9])?\.)+[a-zA-Z]{2,63}\.?$").unwrap(),
        )
    })
}

pub fn looks_like_url(raw: &str) -> bool {
    let input = raw.trim();
    if input.is_empty() {
        return false;
    }
    let (with_authority, known_opaque, host_port, other_scheme, domain) = regexes();
    // An explicit scheme wins, spaces or not ("https://x.org/a b").
    if with_authority.is_match(input) || known_opaque.is_match(input) {
        return true;
    }
    if host_port.is_match(input) {
        return true;
    }
    // Anything else with a colon prefix ("what:ever", "javascript:...") is a search.
    if other_scheme.is_match(input) {
        return false;
    }
    if input.contains(' ') {
        return false;
    }
    let host = input.split('/').next().unwrap_or("");
    if host.eq_ignore_ascii_case("localhost") {
        return true;
    }
    if host.parse::<std::net::IpAddr>().is_ok() {
        return true;
    }
    domain.is_match(host)
}

/// Turns address-field input into something to load: a URL when it looks
/// like one, a search otherwise.  Empty input yields `None`.
pub fn resolve(raw: &str, engine_id: &str) -> Option<String> {
    let input = raw.trim();
    if input.is_empty() {
        return None;
    }
    if looks_like_url(input) {
        if let Some(u) = from_user_input(input) {
            return Some(u);
        }
    }
    Some(search_url(engine_id, input))
}

/// The bits of `QUrl::fromUserInput` we need: add `http://` when there is
/// no scheme, percent-encode spaces.
pub fn from_user_input(input: &str) -> Option<String> {
    let (with_authority, known_opaque, ..) = regexes();
    let candidate = if with_authority.is_match(input) || known_opaque.is_match(input) {
        input.to_string()
    } else {
        format!("http://{input}")
    };
    let candidate = candidate.replace(' ', "%20");
    url::Url::parse(&candidate).ok().map(|u| u.to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_is_non_tracking() {
        assert_eq!(DEFAULT_ENGINE, "duckduckgo");
        assert!(by_id(DEFAULT_ENGINE).is_some());
        assert!(by_id("google").is_none());
        assert!(ENGINES.len() >= 3);
    }

    #[test]
    fn search_url_encodes_terms() {
        let u = search_url("duckduckgo", "sailfish os & servo");
        assert!(u.starts_with("https://duckduckgo.com/?q=sailfish%20os%20%26%20servo"));
        assert!(search_url("nope", "x").starts_with("https://duckduckgo.com/"));
    }

    #[test]
    fn looks_like_url_cases() {
        for (input, expected) in [
            ("https://jolla.com", true),
            ("jolla.com", true),
            ("jolla.com/phone", true),
            ("docs.servo.org", true),
            ("localhost", true),
            ("localhost:8080/x", true),
            ("192.168.1.1", true),
            ("192.168.1.1:3000", true),
            ("about:blank", true),
            ("jolla phone review", false),
            ("servo", false),
            ("what is 2+2?", false),
            ("jolla.com is nice", false),
            ("", false),
            ("example.org.", true),
            ("foo.123", false),
            ("https://x.org/a b", true),
            ("what:ever", false),
            ("javascript:alert(1)", false),
            ("file:///home/user/x.html", true),
        ] {
            assert_eq!(looks_like_url(input), expected, "{input:?}");
        }
    }

    #[test]
    fn resolve_adds_scheme_or_searches() {
        assert_eq!(
            resolve("jolla.com", "duckduckgo").as_deref(),
            Some("http://jolla.com/")
        );
        assert_eq!(
            resolve("  https://x.org/a b  ", "duckduckgo").as_deref(),
            Some("https://x.org/a%20b")
        );
        assert!(resolve("jolla phone", "qwant")
            .unwrap()
            .starts_with("https://www.qwant.com/?q=jolla%20phone"));
        assert_eq!(resolve("", "duckduckgo"), None);
    }
}
