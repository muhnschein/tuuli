# 0003 — One WebView per tab, created on first activation

## Context
The engine keeps navigation history, scroll position and form state per view. A single
view with URL swapping would lose all of that on every tab switch.

## Decision
`BrowserPage` keeps a `Repeater` over `TabModel` whose delegate is a `Loader`; the
`WebView` is created the first time its tab becomes active and kept afterwards. Only the
active view is `active` (painting) and visible. Restored tabs stay unloaded until tapped.

## Consequences
Memory grows with the number of tabs viewed in a session. Phase 1 has no cap; if the
smoke test shows pressure, the next step is unloading the least recently viewed views,
which needs no model change because the Loader already handles absence.
