// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Proxy configuration (spec 8.1).  The Qt layer reads the active connman
//! service's `Proxy` dict over D-Bus; the conversion lives here so it is
//! testable without a bus.

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct ProxyConfig {
    /// `host:port`; empty = direct.
    pub http: String,
    pub https: String,
    pub no_proxy: Vec<String>,
    pub pac_url: String,
}

impl ProxyConfig {
    pub fn is_direct(&self) -> bool {
        self.http.is_empty() && self.https.is_empty() && self.pac_url.is_empty()
    }

    pub fn strip_scheme(server: &str) -> String {
        let mut s = server.trim();
        if let Some(i) = s.find("://") {
            s = &s[i + 3..];
        }
        s.trim_end_matches('/').to_string()
    }

    /// From connman's `Proxy` property: `Method` (direct|manual|auto),
    /// `Servers`, `Excludes`, `URL`.
    pub fn from_connman(
        method: &str,
        servers: &[String],
        excludes: &[String],
        pac_url: &str,
    ) -> ProxyConfig {
        let mut cfg = ProxyConfig::default();
        match method.to_lowercase().as_str() {
            "manual" => {
                for raw in servers {
                    let server = Self::strip_scheme(raw);
                    if server.is_empty() {
                        continue;
                    }
                    if raw.trim().to_lowercase().starts_with("https://") {
                        if cfg.https.is_empty() {
                            cfg.https = server;
                        }
                    } else if cfg.http.is_empty() {
                        cfg.http = server;
                    }
                }
                if cfg.https.is_empty() {
                    cfg.https = cfg.http.clone();
                }
                if cfg.http.is_empty() {
                    cfg.http = cfg.https.clone();
                }
                cfg.no_proxy = excludes.to_vec();
            }
            "auto" => cfg.pac_url = pac_url.to_string(),
            _ => {}
        }
        cfg
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn direct_when_missing_or_direct() {
        assert!(ProxyConfig::from_connman("direct", &[], &[], "").is_direct());
        assert!(ProxyConfig::from_connman("", &[], &[], "").is_direct());
    }

    #[test]
    fn manual_with_excludes_and_https() {
        let c = ProxyConfig::from_connman(
            "manual",
            &["http://proxy.corp:3128/".into()],
            &["localhost".into(), "*.corp".into()],
            "",
        );
        assert!(!c.is_direct());
        assert_eq!(c.http, "proxy.corp:3128");
        assert_eq!(c.https, "proxy.corp:3128");
        assert_eq!(c.no_proxy, vec!["localhost", "*.corp"]);
        let c = ProxyConfig::from_connman(
            "manual",
            &["plain:8080".into(), "https://secure:8443".into()],
            &[],
            "",
        );
        assert_eq!(c.http, "plain:8080");
        assert_eq!(c.https, "secure:8443");
    }

    #[test]
    fn auto_and_strip() {
        let c = ProxyConfig::from_connman("auto", &[], &[], "http://wpad.corp/wpad.dat");
        assert!(!c.is_direct());
        assert_eq!(c.pac_url, "http://wpad.corp/wpad.dat");
        assert!(c.http.is_empty());
        assert_eq!(ProxyConfig::strip_scheme("socks5://h:1/"), "h:1");
        assert_eq!(ProxyConfig::strip_scheme(" h:1 "), "h:1");
    }
}
