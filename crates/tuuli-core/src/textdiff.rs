// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Minimal single-span edit between two strings, and its expression as
//! engine key/composition events (spec 6.3 step 3): the engine has no
//! "set selection" entry point, so caret movement is arrow keys, deletion
//! is Backspace, insertion is a committed composition.  Positions are in
//! `char`s (Unicode scalar values), never inside a code point.

#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct TextEdit {
    pub position: usize,
    pub removed: usize,
    pub inserted: String,
}

impl TextEdit {
    pub fn is_noop(&self) -> bool {
        self.removed == 0 && self.inserted.is_empty()
    }
}

pub fn diff_text(old: &str, new: &str) -> TextEdit {
    let old_chars: Vec<char> = old.chars().collect();
    let new_chars: Vec<char> = new.chars().collect();
    let prefix = old_chars
        .iter()
        .zip(new_chars.iter())
        .take_while(|(a, b)| a == b)
        .count();
    let max_suffix = old_chars.len().min(new_chars.len()) - prefix;
    let suffix = old_chars
        .iter()
        .rev()
        .zip(new_chars.iter().rev())
        .take(max_suffix)
        .take_while(|(a, b)| a == b)
        .count();
    TextEdit {
        position: prefix,
        removed: old_chars.len() - prefix - suffix,
        inserted: new_chars[prefix..new_chars.len() - suffix].iter().collect(),
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ImeAction {
    /// W3C `KeyboardEvent.key` name, pressed `repeat` times.
    Key { key: &'static str, repeat: usize },
    /// Committed composition text.
    Commit(String),
}

fn push_key(out: &mut Vec<ImeAction>, key: &'static str, repeat: usize) {
    if repeat > 0 {
        out.push(ImeAction::Key { key, repeat });
    }
}

/// Plans the engine actions that turn `engine_text` (caret at
/// `engine_cursor`, selection anchor at `engine_anchor`) into `new_text`.
pub fn plan_ime_edit(
    engine_text: &str,
    engine_cursor: usize,
    engine_anchor: Option<usize>,
    new_text: &str,
) -> Vec<ImeAction> {
    let mut out = Vec::new();
    let e = diff_text(engine_text, new_text);
    if e.is_noop() {
        return out;
    }
    let len = engine_text.chars().count();
    let cursor = engine_cursor.min(len);
    let anchor = engine_anchor.unwrap_or(cursor).min(len);
    let sel_start = cursor.min(anchor);
    let sel_end = cursor.max(anchor);
    let has_selection = sel_start != sel_end;

    if has_selection && e.position == sel_start && e.removed == sel_end - sel_start {
        if e.inserted.is_empty() {
            push_key(&mut out, "Backspace", 1);
        } else {
            out.push(ImeAction::Commit(e.inserted));
        }
        return out;
    }

    // Collapse any selection to its end (ArrowRight semantics), then walk
    // the caret to the end of the removed span.
    let mut caret = cursor;
    if has_selection {
        push_key(&mut out, "ArrowRight", 1);
        caret = sel_end;
    }
    let target = e.position + e.removed;
    if target > caret {
        push_key(&mut out, "ArrowRight", target - caret);
    } else if target < caret {
        push_key(&mut out, "ArrowLeft", caret - target);
    }
    push_key(&mut out, "Backspace", e.removed);
    if !e.inserted.is_empty() {
        out.push(ImeAction::Commit(e.inserted));
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn key(k: &'static str, repeat: usize) -> ImeAction {
        ImeAction::Key { key: k, repeat }
    }

    #[test]
    fn diffs() {
        assert_eq!(
            diff_text("hell", "hello"),
            TextEdit {
                position: 4,
                removed: 0,
                inserted: "o".into()
            }
        );
        assert_eq!(
            diff_text("hello", "hell"),
            TextEdit {
                position: 4,
                removed: 1,
                inserted: String::new()
            }
        );
        assert_eq!(
            diff_text("abcdef", "abXYef"),
            TextEdit {
                position: 2,
                removed: 2,
                inserted: "XY".into()
            }
        );
        assert!(diff_text("same", "same").is_noop());
        assert_eq!(
            diff_text("", "abc"),
            TextEdit {
                position: 0,
                removed: 0,
                inserted: "abc".into()
            }
        );
        // Astral characters are one position each.
        assert_eq!(
            diff_text("a😀", "a😉"),
            TextEdit {
                position: 1,
                removed: 1,
                inserted: "😉".into()
            }
        );
    }

    #[test]
    fn plan_typing_and_backspace_at_caret() {
        assert_eq!(
            plan_ime_edit("hell", 4, None, "hello"),
            vec![ImeAction::Commit("o".into())]
        );
        assert_eq!(
            plan_ime_edit("hello", 5, None, "hell"),
            vec![key("Backspace", 1)]
        );
    }

    #[test]
    fn plan_moves_caret() {
        assert_eq!(
            plan_ime_edit("abcdef", 6, None, "abXdef"),
            vec![
                key("ArrowLeft", 3),
                key("Backspace", 1),
                ImeAction::Commit("X".into())
            ]
        );
        assert_eq!(
            plan_ime_edit("abcdef", 0, None, "abcdefg"),
            vec![key("ArrowRight", 6), ImeAction::Commit("g".into())]
        );
    }

    #[test]
    fn plan_with_selection() {
        assert_eq!(
            plan_ime_edit("abcdef", 4, Some(2), "abXef"),
            vec![ImeAction::Commit("X".into())]
        );
        assert_eq!(
            plan_ime_edit("abcdef", 2, Some(4), "abef"),
            vec![key("Backspace", 1)]
        );
        assert!(plan_ime_edit("x", 1, None, "x").is_empty());
    }
}
