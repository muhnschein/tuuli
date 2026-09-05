// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! User-facing settings (spec 7.1 Settings view, 9.4 defaults), stored as
//! JSON in the sailjail-permitted config dir.

use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

use crate::search::{self, DEFAULT_ENGINE};
use crate::session::write_atomically;

/// Servo preference names Tuuli sets (spec 9.4).  Validated against the
/// pinned tag's `components/config/prefs.rs` at every rebase
/// (docs/UPSTREAM.md); kept in one table so a rename is one line here.
pub mod servo_prefs {
    pub const BLOCK_THIRD_PARTY_COOKIES: &str = "network_cookies_block_third_party";
    pub const SEND_DNT: &str = "network_http_dnt";
    pub const SEND_GPC: &str = "network_http_gpc";
    pub const REFERRER_POLICY: &str = "network_http_referrer_policy";
    pub const JS_ENABLED: &str = "js_enabled";
    pub const DOM_TOUCH_ENABLED: &str = "dom_touch_enabled";
    pub const MEDIA_GL_VIDEO: &str = "media_glvideo_enabled";
    pub const LAYOUT_THREADS: &str = "layout_threads";
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default)]
pub struct Preferences {
    pub search_engine: String,
    pub home_page: String,
    pub restore_session: bool,

    pub block_third_party_cookies: bool,
    pub send_do_not_track: bool,
    pub send_global_privacy_control: bool,
    pub referrer_policy: String,
    pub cosmetic_filtering: bool,

    pub javascript_enabled: bool,
    pub user_agent_override: String,
    pub download_directory: String,

    pub device_pixel_ratio_override: f64,
    pub show_frame_stats: bool,
    pub engine_logging: bool,
    pub perf_logging: bool,
    pub max_live_webviews: usize,
}

impl Default for Preferences {
    fn default() -> Self {
        Self {
            search_engine: DEFAULT_ENGINE.into(),
            home_page: String::new(),
            restore_session: true,
            block_third_party_cookies: true,
            send_do_not_track: true,
            send_global_privacy_control: true,
            referrer_policy: "strict-origin-when-cross-origin".into(),
            cosmetic_filtering: true,
            javascript_enabled: true,
            user_agent_override: String::new(),
            download_directory: String::new(),
            device_pixel_ratio_override: 0.0,
            show_frame_stats: false,
            engine_logging: false,
            perf_logging: false,
            max_live_webviews: 8,
        }
    }
}

impl Preferences {
    pub fn load(path: &Path) -> Self {
        let mut p: Preferences = std::fs::read(path)
            .ok()
            .and_then(|d| serde_json::from_slice(&d).ok())
            .unwrap_or_default();
        p.normalize();
        p
    }

    pub fn save(&self, path: &Path) -> std::io::Result<()> {
        let data = serde_json::to_vec_pretty(self).map_err(std::io::Error::other)?;
        write_atomically(path, &data)
    }

    pub fn normalize(&mut self) {
        if search::by_id(&self.search_engine).is_none() {
            self.search_engine = DEFAULT_ENGINE.into();
        }
        self.max_live_webviews = self.max_live_webviews.max(1);
        if self.device_pixel_ratio_override.is_nan() || self.device_pixel_ratio_override < 0.0 {
            self.device_pixel_ratio_override = 0.0;
        }
    }

    pub fn download_dir(&self, default: &Path) -> PathBuf {
        if self.download_directory.is_empty() {
            default.to_path_buf()
        } else {
            PathBuf::from(&self.download_directory)
        }
    }

    /// Engine preference pairs derived from the privacy and engine settings.
    pub fn engine_prefs(&self) -> Vec<(String, String)> {
        use servo_prefs::*;
        let b = |v: bool| {
            if v {
                "true".to_string()
            } else {
                "false".to_string()
            }
        };
        vec![
            (
                BLOCK_THIRD_PARTY_COOKIES.into(),
                b(self.block_third_party_cookies),
            ),
            (SEND_DNT.into(), b(self.send_do_not_track)),
            (SEND_GPC.into(), b(self.send_global_privacy_control)),
            (REFERRER_POLICY.into(), self.referrer_policy.clone()),
            (JS_ENABLED.into(), b(self.javascript_enabled)),
            (DOM_TOUCH_ENABLED.into(), b(true)),
            (MEDIA_GL_VIDEO.into(), b(true)),
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn defaults_match_spec() {
        let p = Preferences::default();
        assert!(
            p.block_third_party_cookies && p.send_do_not_track && p.send_global_privacy_control
        );
        assert_eq!(p.referrer_policy, "strict-origin-when-cross-origin");
        assert_eq!(p.search_engine, "duckduckgo");
        assert!(p.restore_session && p.javascript_enabled);
        assert_eq!(p.max_live_webviews, 8);
        assert_eq!(p.device_pixel_ratio_override, 0.0);
    }

    #[test]
    fn engine_prefs_reflect_settings() {
        let mut p = Preferences::default();
        let prefs = p.engine_prefs();
        assert!(prefs.contains(&(servo_prefs::BLOCK_THIRD_PARTY_COOKIES.into(), "true".into())));
        assert!(prefs.contains(&(
            servo_prefs::REFERRER_POLICY.into(),
            "strict-origin-when-cross-origin".into()
        )));
        p.block_third_party_cookies = false;
        p.javascript_enabled = false;
        let prefs = p.engine_prefs();
        assert!(prefs.contains(&(
            servo_prefs::BLOCK_THIRD_PARTY_COOKIES.into(),
            "false".into()
        )));
        assert!(prefs.contains(&(servo_prefs::JS_ENABLED.into(), "false".into())));
    }

    #[test]
    fn persists_and_normalizes() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("prefs.json");
        let p = Preferences {
            send_do_not_track: false,
            search_engine: "qwant".into(),
            max_live_webviews: 0,
            ..Preferences::default()
        };
        p.save(&path).unwrap();
        let again = Preferences::load(&path);
        assert!(!again.send_do_not_track);
        assert_eq!(again.search_engine, "qwant");
        assert_eq!(again.max_live_webviews, 1);
        std::fs::write(
            &path,
            br#"{"search_engine": "does-not-exist", "device_pixel_ratio_override": -1}"#,
        )
        .unwrap();
        let fixed = Preferences::load(&path);
        assert_eq!(fixed.search_engine, "duckduckgo");
        assert_eq!(fixed.device_pixel_ratio_override, 0.0);
        assert!(fixed.restore_session, "missing keys keep defaults");
        assert_eq!(
            Preferences::load(Path::new("/nonexistent/x.json")),
            Preferences::default()
        );
    }
}
