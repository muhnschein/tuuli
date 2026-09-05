# Silica stubs

Stand-ins for the `Sailfish.Silica` and `Sailfish.Share` QML modules, so
that `crates/tuuli-browser/tests/qml_loads.rs` can instantiate every file
of the chrome on a host with no Silica installed.  Adapted from
Postivene's `tests/silica-stubs` (same author) and extended with the types
Tuuli uses.

They imitate no layout and no behaviour: a page that loads here is a page
whose types, imports and property names resolve, nothing more.  A property
the real Silica lacks is still only found on a device, so keep each stub
to the properties Silica actually has.

`EnterKey` cannot be stubbed: it is an attached property, which needs a
registered C++ type, so the test strips `EnterKey.*` lines from its copy of
the QML before loading.
