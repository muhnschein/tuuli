# 0002 — QML load tests against Silica stubs

## Context
Silica, WebView and Share exist only on the device. The pages still need to be loaded
and driven on the host.

## Decision
`tests/silica-stubs/` provides `Sailfish.Silica`, `Sailfish.WebView`, `Sailfish.WebEngine`
and `Sailfish.Share` as QML stubs with the property and method names the real
components have, recording calls for assertions. They imitate no layout. One small C++
plugin supplies what QML cannot: the `EnterKey` attached property and the enum holders
(`Orientation`, `PageStatus`, `TruncationMode`).

## Consequences
Tests prove structure and wiring, not appearance; appearance is the device smoke test.
Using a Silica type the stubs lack fails the load test, which is the reminder to add it.
