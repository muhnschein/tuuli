# 0005 — Favicons through a page script with `/favicon.ico` fallback

## Context
`WebView` exposes no favicon property (qtmozembed `qmozview_defined_wrapper.h`).
sailfish-browser receives icons through engine messages that are private to it.

## Decision
When a page finishes loading, the view runs `EngineMessages.faviconScript`
(`document.querySelector('link[rel~="icon"]')`) through the public `runJavaScript`
API and resolves the result against the page URL; without a declared icon, or on
error, `scheme://host/favicon.ico` is used. QML `Image` loads the URL; the RPM
requires `qt5-plugin-imageformat-ico`.

## Consequences
Icons are fetched by Qt, not by the engine, so a second request per page. Private tabs
never publish their icon. Verified on the device as part of the smoke test.
