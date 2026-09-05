// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Where Tuuli keeps its data.  Sailjail permits `~/.local/share/<Org>/<App>`
//! and siblings for the `OrganizationName`/`ApplicationName` declared in
//! the `.desktop` file (spec 9.1), so that is where everything goes.  Both
//! are the package name, as libsailfishapp itself sets them and as
//! `src/app/harbour-tuuli.desktop` declares; `ci/harbour-check.sh` (2.5)
//! fails a rename on one side only.

use std::path::{Path, PathBuf};

pub const ORGANIZATION: &str = "harbour-tuuli";
pub const APPLICATION: &str = "harbour-tuuli";

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct AppPaths {
    pub data_dir: PathBuf,
    pub cache_dir: PathBuf,
    pub config_dir: PathBuf,
    pub download_dir: PathBuf,
}

fn env_dir(var: &str, home_fallback: &str) -> PathBuf {
    match std::env::var_os(var).filter(|v| !v.is_empty()) {
        Some(v) => PathBuf::from(v),
        None => home().join(home_fallback),
    }
}

pub fn home() -> PathBuf {
    std::env::var_os("HOME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("/"))
}

impl AppPaths {
    /// XDG base directories with the sailjail organisation/application suffix.
    pub fn xdg() -> Self {
        let suffix = Path::new(ORGANIZATION).join(APPLICATION);
        Self {
            data_dir: env_dir("XDG_DATA_HOME", ".local/share").join(&suffix),
            cache_dir: env_dir("XDG_CACHE_HOME", ".cache").join(&suffix),
            config_dir: env_dir("XDG_CONFIG_HOME", ".config").join(&suffix),
            download_dir: home().join("Downloads"),
        }
    }

    pub fn under(root: &Path) -> Self {
        Self {
            data_dir: root.join("data"),
            cache_dir: root.join("cache"),
            config_dir: root.join("config"),
            download_dir: root.join("downloads"),
        }
    }

    pub fn create_all(&self) -> std::io::Result<()> {
        for d in [&self.data_dir, &self.cache_dir, &self.config_dir] {
            std::fs::create_dir_all(d)?;
        }
        Ok(())
    }

    pub fn session_file(&self) -> PathBuf {
        self.data_dir.join("session.json")
    }
    pub fn history_db(&self) -> PathBuf {
        self.data_dir.join("history.sqlite")
    }
    pub fn bookmarks_db(&self) -> PathBuf {
        self.data_dir.join("bookmarks.sqlite")
    }
    pub fn permissions_file(&self) -> PathBuf {
        self.data_dir.join("permissions.json")
    }
    pub fn prefs_file(&self) -> PathBuf {
        self.config_dir.join("prefs.json")
    }
    pub fn filters_dir(&self) -> PathBuf {
        self.data_dir.join("filters")
    }
    pub fn perf_log(&self) -> PathBuf {
        self.cache_dir.join("perf.log")
    }
    pub fn engine_data_dir(&self) -> PathBuf {
        self.data_dir.join("engine")
    }
    pub fn engine_cache_dir(&self) -> PathBuf {
        self.cache_dir.join("engine")
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn xdg_layout_uses_sailjail_suffix() {
        let p = AppPaths::xdg();
        assert!(p.data_dir.ends_with("harbour-tuuli/harbour-tuuli"));
        assert!(p.config_dir.ends_with("harbour-tuuli/harbour-tuuli"));
        assert!(p.cache_dir.ends_with("harbour-tuuli/harbour-tuuli"));
        assert!(p.session_file().ends_with("session.json"));
    }
}
