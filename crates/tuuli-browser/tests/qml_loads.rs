// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Every QML file of the chrome instantiates against the Silica stubs in
//! `tests/silica-stubs`.  What this catches: a type used without importing
//! its directory, a typo in a type or property name, a file that is not
//! where a `Qt.resolvedUrl` says, syntax the QML parser rejects.  What it
//! cannot: layout, behaviour, and properties the stubs have but the real
//! Silica lacks.  The first version of the root file used `BrowserPage`
//! without `import "pages"`, and the only place that showed was a white
//! screen on a phone.

use std::path::{Path, PathBuf};
use std::rc::Rc;

use qmetaobject::QmlEngine;
use tuuli_core::mock::MockEngine;
use tuuli_core::paths::AppPaths;

fn workspace_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .canonicalize()
        .unwrap()
}

/// `EnterKey` is an attached property: it needs a registered C++ type,
/// which a QML stub cannot provide.  Drop `EnterKey.*` lines from the
/// copy the test loads, with the continuation lines and handler bodies
/// that belong to them (anything indented deeper, and the braces they
/// open).
fn strip_enter_key(source: &str) -> String {
    let lines: Vec<&str> = source.lines().collect();
    let mut out = Vec::with_capacity(lines.len());
    let mut i = 0;
    while i < lines.len() {
        let line = lines[i];
        let trimmed = line.trim_start();
        if !trimmed.starts_with("EnterKey.") {
            out.push(line);
            i += 1;
            continue;
        }
        let base = line.len() - trimmed.len();
        let mut depth = braces(line);
        i += 1;
        while i < lines.len() {
            let next = lines[i];
            let next_trimmed = next.trim_start();
            let indent = next.len() - next_trimmed.len();
            if depth > 0 {
                depth += braces(next);
                i += 1;
            } else if !next_trimmed.is_empty() && indent > base {
                i += 1;
            } else {
                break;
            }
        }
    }
    out.join("\n") + "\n"
}

fn braces(line: &str) -> i32 {
    line.chars().filter(|&c| c == '{').count() as i32
        - line.chars().filter(|&c| c == '}').count() as i32
}

fn copy_qml(from: &Path, to: &Path, files: &mut Vec<PathBuf>) {
    std::fs::create_dir_all(to).unwrap();
    for entry in std::fs::read_dir(from).unwrap() {
        let entry = entry.unwrap();
        let target = to.join(entry.file_name());
        if entry.file_type().unwrap().is_dir() {
            copy_qml(&entry.path(), &target, files);
        } else if entry
            .path()
            .extension()
            .map(|e| e == "qml" || e == "js")
            .unwrap_or(false)
        {
            let source = std::fs::read_to_string(entry.path()).unwrap();
            std::fs::write(&target, strip_enter_key(&source)).unwrap();
            if target.extension().unwrap() == "qml" {
                files.push(target);
            }
        }
    }
}

#[test]
fn every_qml_file_loads_against_the_silica_stubs() {
    std::env::set_var("QT_QPA_PLATFORM", "offscreen");
    std::env::set_var("QSG_RENDER_LOOP", "basic");
    let root = workspace_root();
    let temp = tempfile::tempdir().unwrap();

    let mut files = Vec::new();
    copy_qml(&root.join("src/qml"), &temp.path().join("qml"), &mut files);
    files.sort();
    assert!(
        files.len() >= 15,
        "expected the whole chrome, found {} files",
        files.len()
    );

    // The types the QML sees on a device, and a core for the Browser
    // singleton to talk to.
    tuuli_qml::register_types();
    let engine = MockEngine::new();
    tuuli_qml::install(
        engine.clone() as Rc<dyn tuuli_core::engine::Engine>,
        AppPaths::under(&temp.path().join("data")),
        Vec::new(),
    )
    .expect("core installs");

    let mut qml = QmlEngine::new();
    qml.add_import_path(qttypes::QString::from(
        root.join("tests/silica-stubs")
            .to_string_lossy()
            .to_string(),
    ));
    let engine_ptr = qml.cpp_ptr();

    let mut failures = Vec::new();
    for file in &files {
        let url = format!("file://{}", file.display());
        if let Err(e) = tuuli_browser::probe_qml(engine_ptr, &url) {
            let relative = file
                .strip_prefix(temp.path())
                .unwrap()
                .display()
                .to_string();
            failures.push(format!("{relative}:\n{e}"));
        }
    }
    assert!(
        failures.is_empty(),
        "{} of {} QML files failed to load:\n\n{}",
        failures.len(),
        files.len(),
        failures.join("\n\n")
    );
}

#[test]
fn enter_key_stripping_keeps_the_rest() {
    let src = "TextField {\n    text: \"a\"\n    EnterKey.enabled: true\n    EnterKey.iconSource: a ? \"x\"\n                         : \"y\"\n    EnterKey.onClicked: {\n        go()\n    }\n    width: 1\n}\n";
    assert_eq!(
        strip_enter_key(src),
        "TextField {\n    text: \"a\"\n    width: 1\n}\n"
    );
}
