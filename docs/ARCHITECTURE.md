# Architecture

## Module boundaries

| Layer | Location | Owns | Knows about |
|---|---|---|---|
| Engine | platform `Sailfish.WebView` | rendering, navigation history, cookies, dialogs, pickers, downloads | nothing of ours |
| UI | `qml/` | pages, components, cover | the `harbour.tuuli` singletons |
| Core | `src/` | tabs, history, bookmarks, settings, engine-facing strings | SQLite, QSettings |

`Sailfish.WebView` is imported in `qml/pages/BrowserPage.qml` only; a device without
the engine package fails to open that page, not the application. `Sailfish.WebEngine`
is imported there and in `SettingsPage.qml` (data clearing). `tests/tst_qmlstatic.cpp`
enforces both.

The core is one process-wide `Tuuli::Core` (`src/Core.h`) that owns:

- `Storage` — the single SQLite file and its schema.
- `TabModel` + `TabPersistence` — open tabs, the active tab, private flag.
- `HistoryModel` — visited pages, search, pruning.
- `BookmarkModel` — bookmarks and "is the active page bookmarked".
- `Settings` — home page, search engine, desktop mode, address-bar heuristics.
- `EngineMessages` — the only place engine-specific strings live.

`registerQmlTypes()` exposes each as a QML singleton under `harbour.tuuli 1.0`.

## Data flow

1. The engine reports `url`, `title` and load state on a `WebView`.
2. `BrowserPage` forwards them to `TabModel.updateUrl/updateTitle/updateFavicon`.
3. `TabModel` updates its row, persists non-private tabs, and emits `visited`,
   `titleUpdated`, `faviconUpdated` for non-private tabs only.
4. `Core` wires those signals to `HistoryModel` and `BookmarkModel`. Private tabs
   therefore never reach history or disk; the engine's `privateMode` keeps cookies out.
5. `TabModel.activeTabDataChanged` feeds the address bar, the cover and
   `BookmarkModel.activeUrl`.

Views: one `WebView` per tab that has been shown this session, created lazily by a
`Loader` (see `DECISIONS/0003-one-webview-per-tab.md`). Restored tabs cost nothing
until activated. Favicons come from a page script with `/favicon.ico` as fallback
(`DECISIONS/0005-favicons.md`).

Typed text goes through `Settings.urlForInput`: a URL with a known scheme is used as
is, a host-like token gets `https://` (`http://` for localhost and IP addresses),
anything else becomes a search with the selected engine.

## Storage

Location: `QStandardPaths::AppDataLocation` (Sailjail: `~/.local/share/<org>/<app>`),
file `tuuli.sqlite`. Settings: `AppConfigLocation/tuuli.conf` (INI). Nothing else is
written. Schema version is `PRAGMA user_version` (`Storage::SchemaVersion`, currently 1);
a newer database than the build refuses to open rather than corrupt.

```
tab              tab_id PK, position, url, title, favicon
browser_history  id PK, url UNIQUE, title, visited_count, date (ms since epoch)
bookmark         id PK, url, title, favicon, position, created (s since epoch)
setting          name PK, value          -- activeTabId
```

History is capped at 2000 rows (pruned on open) and the model shows the newest 500.
Queries run on the UI thread; sizes are bounded, so no worker thread
(`DECISIONS/0004-sqlite-storage.md`).
