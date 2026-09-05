`qmetaobject_rust.hpp` is a verbatim copy of the header shipped inside the
`qmetaobject` crate (version 0.2.10, MIT licence, Olivier Goffart).  Our
`cpp!` blocks subclass `RustObject<QQuickFramebufferObject>` exactly the
way the crate subclasses `RustObject<QQuickItem>`, so the two must agree;
`build.rs` prefers the copy inside the resolved crate when it can find it
and falls back to this one (vendored builds).  Bump both together.
