# 0006 — Engine-facing strings live in C++

## Context
Clearing cookies and cache goes through `WebEngine.notifyObservers` with topics the
embedding understands (`clear-private-data`, payloads `cookies-and-site-data`, `cache`).
The scope forbids engine quirks in QML.

## Decision
`src/engine/EngineMessages` holds every engine-facing string, each with a comment naming
its upstream source, and is exposed as a QML singleton. QML passes the values to the
platform API without knowing them.

## Consequences
A change in the engine touches one file and one unit test. Linking `libsailfishwebengine`
from C++ was rejected because it would need another host stub for no gain.
