# Manual device matrix

Run before every Chum release (spec 13) on the Jolla Phone (2026), 8 GB
SKU.  Community ports are unsupported; note results from them separately.

Record the build (`Browser.version`, engine tag), device software version
and date at the top of the run.

## Corpus

Load each page in `tools/corpus/pages.json`; note render correctness,
scroll and pinch smoothness, any hang or crash.  Capture screenshots to
compare with `tools/screenshots/compare.py`.

## Sailfish integrations

| Area | Check | Pass |
|---|---|---|
| Launch | icon, cold start, start view search field focused | |
| Session | kill the app; relaunch restores tabs, order, current tab, scroll | |
| Crash notice | `kill -SEGV`; relaunch shows "restored after unexpected exit" | |
| Edge gestures | left/right/top swipes reach lipstick from inside a page | |
| Toolbar | bottom-edge swipe reveals it; scrolling hides it; URL editing | |
| Pulley menus | top pulley at page top; bottom pulley at page bottom; nothing steals a mid-page scroll | |
| Long-press | link, image, selection, editable field, plain page; no movement needed; no accidental scroll | |
| Double-tap | zooms to element | |
| Pinch | zoom in/out, no dropped touch, no toolbar flicker | |
| VKB | text, url, email, number, tel, password, search inputs get the right layout; enter key label; caret stays visible above the keyboard; backspace, mid-text edits, autocorrect commits | |
| Clipboard | copy link / selection; paste into a field | |
| Share | page and link through the system share sheet | |
| Downloads | file appears in Transfers and in Downloads page; cancel; private tab download not in Transfers | |
| Tabs | overview thumbnails, swipe-to-close, long-press reorder, close all with remorse | |
| Private | dark treatment; no history; no session persistence; no permission persistence | |
| Permissions | prompt appears; deny by default; remember; forget in settings and page info | |
| Cover | title and favicon; new tab and reload actions | |
| Orientation | portrait, landscape both ways; content resizes; toolbar and keyboard still correct | |
| Ambience | chrome follows ambience colours | |
| Proxy | manual proxy in connman reaches the engine (page info shows it) | |
| Cosmetic filter | drop a list into `filters/`, reload, elements hidden; toggle off restores | |
| Audio | page audio ducks on a call (media role) | |
| Cover/sleep | minimise, wait, resume: page intact or restored | |
| Memory | eight corpus tabs; `Browser.prefs.maxLiveWebViews` eviction and re-creation | |
| About | threat-model text present and current | |

## Performance

With Performance logging on, run the corpus and interactions, pull
`<cache>/perf.log` and run `tools/perf/run-budgets.py` with the confirmed
panel refresh rate.  Budget failures block M3, not M2 (spec 11).
