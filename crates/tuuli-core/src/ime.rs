// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! State behind the hidden QML `TextInput` that Maliit attaches to
//! (spec 6.3).  The engine reports editable focus here; the QML proxy binds
//! its hints/text to it and reports committed edits back, which become
//! engine key/composition actions.

use crate::engine::InputType;
use crate::geometry::Rect;
use crate::textdiff::{diff_text, plan_ime_edit, ImeAction};

/// Qt::InputMethodHints bit values (Qt 5.6).
pub mod hints {
    pub const NONE: u32 = 0;
    pub const HIDDEN_TEXT: u32 = 0x1;
    pub const SENSITIVE_DATA: u32 = 0x2;
    pub const NO_AUTO_UPPERCASE: u32 = 0x4;
    pub const PREFER_NUMBERS: u32 = 0x8;
    pub const NO_PREDICTIVE_TEXT: u32 = 0x40;
    pub const DIGITS_ONLY: u32 = 0x10000;
    pub const FORMATTED_NUMBERS_ONLY: u32 = 0x20000;
    pub const DIALABLE_CHARACTERS_ONLY: u32 = 0x100000;
    pub const EMAIL_CHARACTERS_ONLY: u32 = 0x200000;
    pub const URL_CHARACTERS_ONLY: u32 = 0x400000;
}

/// Qt::EnterKeyType values (Qt 5.6).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[repr(i32)]
pub enum EnterKeyType {
    Default = 0,
    Return = 1,
    Done = 2,
    Go = 3,
    Send = 4,
    Search = 5,
    Next = 6,
    Previous = 7,
}

pub fn hints_for(t: InputType) -> u32 {
    use hints::*;
    match t {
        InputType::Url => URL_CHARACTERS_ONLY | NO_AUTO_UPPERCASE | NO_PREDICTIVE_TEXT,
        InputType::Email => EMAIL_CHARACTERS_ONLY | NO_AUTO_UPPERCASE | NO_PREDICTIVE_TEXT,
        InputType::Number => FORMATTED_NUMBERS_ONLY,
        InputType::Tel => DIALABLE_CHARACTERS_ONLY,
        InputType::Password => {
            HIDDEN_TEXT | SENSITIVE_DATA | NO_AUTO_UPPERCASE | NO_PREDICTIVE_TEXT
        }
        InputType::Search => NO_AUTO_UPPERCASE,
        InputType::Date
        | InputType::Time
        | InputType::DateTime
        | InputType::Month
        | InputType::Week => PREFER_NUMBERS,
        InputType::Color | InputType::Text | InputType::None => NONE,
    }
}

pub fn enter_key_type_for(t: InputType, multiline: bool) -> EnterKeyType {
    if multiline {
        return EnterKeyType::Default;
    }
    match t {
        InputType::Search => EnterKeyType::Search,
        InputType::Url => EnterKeyType::Go,
        InputType::Password => EnterKeyType::Done,
        _ => EnterKeyType::Default,
    }
}

/// W3C `KeyboardEvent.key` name for a Qt key code (`Qt::Key`) and its text.
pub fn w3c_key_name(qt_key: i32, text: &str) -> String {
    const KEY_ESCAPE: i32 = 0x01000000;
    const KEY_TAB: i32 = 0x01000001;
    const KEY_BACKTAB: i32 = 0x01000002;
    const KEY_BACKSPACE: i32 = 0x01000003;
    const KEY_RETURN: i32 = 0x01000004;
    const KEY_ENTER: i32 = 0x01000005;
    const KEY_DELETE: i32 = 0x01000007;
    const KEY_HOME: i32 = 0x01000010;
    const KEY_END: i32 = 0x01000011;
    const KEY_LEFT: i32 = 0x01000012;
    const KEY_UP: i32 = 0x01000013;
    const KEY_RIGHT: i32 = 0x01000014;
    const KEY_DOWN: i32 = 0x01000015;
    const KEY_PAGEUP: i32 = 0x01000016;
    const KEY_PAGEDOWN: i32 = 0x01000017;
    const KEY_SHIFT: i32 = 0x01000020;
    const KEY_CONTROL: i32 = 0x01000021;
    const KEY_META: i32 = 0x01000022;
    const KEY_ALT: i32 = 0x01000023;
    const KEY_SPACE: i32 = 0x20;
    let named = match qt_key {
        KEY_RETURN | KEY_ENTER => "Enter",
        KEY_BACKSPACE => "Backspace",
        KEY_DELETE => "Delete",
        KEY_TAB | KEY_BACKTAB => "Tab",
        KEY_ESCAPE => "Escape",
        KEY_LEFT => "ArrowLeft",
        KEY_RIGHT => "ArrowRight",
        KEY_UP => "ArrowUp",
        KEY_DOWN => "ArrowDown",
        KEY_HOME => "Home",
        KEY_END => "End",
        KEY_PAGEUP => "PageUp",
        KEY_PAGEDOWN => "PageDown",
        KEY_SPACE => " ",
        KEY_SHIFT => "Shift",
        KEY_CONTROL => "Control",
        KEY_ALT => "Alt",
        KEY_META => "Meta",
        _ => "",
    };
    if !named.is_empty() {
        return named.to_string();
    }
    if !text.is_empty() {
        return text.to_string();
    }
    "Unidentified".to_string()
}

/// What the proxy asks the engine to do (drained by the Qt layer).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ImeRequest {
    Key {
        down: bool,
        key: String,
        modifiers: u32,
    },
    /// Committed composition text.
    Commit(String),
    Dismiss,
}

#[derive(Clone, Debug, Default)]
pub struct InputMethodState {
    pub active: bool,
    pub text: String,
    pub cursor: usize,
    pub anchor: usize,
    pub input_type: InputType,
    pub multiline: bool,
    pub cursor_rect: Rect,
    requests: Vec<ImeRequest>,
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct ImeChanges {
    pub active: bool,
    pub text: bool,
    pub selection: bool,
    pub input_type: bool,
    pub cursor_rect: bool,
}

impl InputMethodState {
    pub fn hints(&self) -> u32 {
        hints_for(self.input_type)
    }
    pub fn enter_key_type(&self) -> EnterKeyType {
        enter_key_type_for(self.input_type, self.multiline)
    }
    pub fn password_mode(&self) -> bool {
        self.input_type == InputType::Password
    }

    pub fn take_requests(&mut self) -> Vec<ImeRequest> {
        std::mem::take(&mut self.requests)
    }

    /// Engine side.
    pub fn show_from_engine(
        &mut self,
        input_type: InputType,
        text: &str,
        multiline: bool,
        cursor_rect: Rect,
    ) -> ImeChanges {
        let mut ch = ImeChanges::default();
        if input_type != self.input_type || multiline != self.multiline {
            self.input_type = input_type;
            self.multiline = multiline;
            ch.input_type = true;
        }
        if self.text != text {
            self.text = text.to_string();
            ch.text = true;
        }
        let len = self.text.chars().count();
        if self.cursor != len || self.anchor != len {
            self.cursor = len;
            self.anchor = len;
            ch.selection = true;
        }
        if self.cursor_rect != cursor_rect {
            self.cursor_rect = cursor_rect;
            ch.cursor_rect = true;
        }
        if !self.active {
            self.active = true;
            ch.active = true;
        }
        ch
    }

    pub fn hide_from_engine(&mut self) -> ImeChanges {
        let mut ch = ImeChanges::default();
        if self.active {
            self.active = false;
            ch.active = true;
        }
        ch
    }

    pub fn selection_from_engine(
        &mut self,
        text: &str,
        cursor: usize,
        anchor: Option<usize>,
    ) -> ImeChanges {
        let mut ch = ImeChanges::default();
        if self.text != text {
            self.text = text.to_string();
            ch.text = true;
        }
        let len = self.text.chars().count();
        let cursor = cursor.min(len);
        let anchor = anchor.unwrap_or(cursor).min(len);
        if cursor != self.cursor || anchor != self.anchor {
            self.cursor = cursor;
            self.anchor = anchor;
            ch.selection = true;
        }
        ch
    }

    /// QML side: the proxy's text changed to `new_text`.
    pub fn text_edited(&mut self, new_text: &str) -> ImeChanges {
        let mut ch = ImeChanges::default();
        if !self.active || new_text == self.text {
            return ch;
        }
        let e = diff_text(&self.text, new_text);
        let actions = plan_ime_edit(&self.text, self.cursor, Some(self.anchor), new_text);
        // Optimistically mirror the edit; the engine's selection update
        // corrects us if it disagrees.
        self.text = new_text.to_string();
        let len = self.text.chars().count();
        self.cursor = (e.position + e.inserted.chars().count()).min(len);
        self.anchor = self.cursor;
        for a in actions {
            match a {
                ImeAction::Key { key, repeat } => {
                    for _ in 0..repeat {
                        self.requests.push(ImeRequest::Key {
                            down: true,
                            key: key.to_string(),
                            modifiers: 0,
                        });
                        self.requests.push(ImeRequest::Key {
                            down: false,
                            key: key.to_string(),
                            modifiers: 0,
                        });
                    }
                }
                ImeAction::Commit(t) => self.requests.push(ImeRequest::Commit(t)),
            }
        }
        ch.text = true;
        ch.selection = true;
        ch
    }

    pub fn send_key(&mut self, qt_key: i32, text: &str, modifiers: u32) {
        let key = w3c_key_name(qt_key, text);
        self.requests.push(ImeRequest::Key {
            down: true,
            key: key.clone(),
            modifiers,
        });
        self.requests.push(ImeRequest::Key {
            down: false,
            key,
            modifiers,
        });
    }

    pub fn submit(&mut self) {
        self.send_key(0x01000004, "", 0);
    }

    pub fn dismiss(&mut self) -> ImeChanges {
        if !self.active {
            return ImeChanges::default();
        }
        self.requests.push(ImeRequest::Dismiss);
        self.hide_from_engine()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hints_per_input_type() {
        assert!(hints_for(InputType::Url) & hints::URL_CHARACTERS_ONLY != 0);
        assert!(hints_for(InputType::Url) & hints::NO_AUTO_UPPERCASE != 0);
        assert!(hints_for(InputType::Email) & hints::EMAIL_CHARACTERS_ONLY != 0);
        assert!(hints_for(InputType::Number) & hints::FORMATTED_NUMBERS_ONLY != 0);
        assert!(hints_for(InputType::Tel) & hints::DIALABLE_CHARACTERS_ONLY != 0);
        assert!(hints_for(InputType::Password) & hints::HIDDEN_TEXT != 0);
        assert!(hints_for(InputType::Password) & hints::SENSITIVE_DATA != 0);
        assert_eq!(hints_for(InputType::Text), hints::NONE);
    }

    #[test]
    fn enter_key_types_and_key_names() {
        assert_eq!(
            enter_key_type_for(InputType::Search, false),
            EnterKeyType::Search
        );
        assert_eq!(enter_key_type_for(InputType::Url, false), EnterKeyType::Go);
        assert_eq!(
            enter_key_type_for(InputType::Search, true),
            EnterKeyType::Default
        );
        assert_eq!(w3c_key_name(0x01000004, "\r"), "Enter");
        assert_eq!(w3c_key_name(0x01000003, ""), "Backspace");
        assert_eq!(w3c_key_name(0x01000012, ""), "ArrowLeft");
        assert_eq!(w3c_key_name(0x41, "a"), "a");
        assert_eq!(w3c_key_name(0x01000050, ""), "Unidentified");
    }

    #[test]
    fn show_from_engine_activates_with_state() {
        let mut p = InputMethodState::default();
        let ch = p.show_from_engine(
            InputType::Email,
            "me@",
            false,
            Rect::new(1.0, 2.0, 3.0, 4.0),
        );
        assert!(p.active && ch.active && ch.input_type && ch.text);
        assert_eq!(p.text, "me@");
        assert_eq!(p.cursor, 3);
        assert_eq!(p.cursor_rect, Rect::new(1.0, 2.0, 3.0, 4.0));
        assert!(p.hints() & hints::EMAIL_CHARACTERS_ONLY != 0);
        assert!(!p.password_mode());
        p.show_from_engine(InputType::Password, "", false, Rect::default());
        assert!(p.password_mode());
        assert!(p.hide_from_engine().active);
        assert!(!p.active);
        assert!(!p.hide_from_engine().active);
    }

    #[test]
    fn edits_become_engine_requests() {
        let mut p = InputMethodState::default();
        p.show_from_engine(InputType::Text, "hel", false, Rect::default());
        p.text_edited("hello");
        assert_eq!(p.take_requests(), vec![ImeRequest::Commit("lo".into())]);
        assert_eq!(p.text, "hello");
        assert_eq!(p.cursor, 5);
        p.text_edited("hell");
        assert_eq!(
            p.take_requests(),
            vec![
                ImeRequest::Key {
                    down: true,
                    key: "Backspace".into(),
                    modifiers: 0
                },
                ImeRequest::Key {
                    down: false,
                    key: "Backspace".into(),
                    modifiers: 0
                }
            ]
        );
        assert_eq!(p.cursor, 4);
    }

    #[test]
    fn engine_selection_updates_state() {
        let mut p = InputMethodState::default();
        p.show_from_engine(InputType::Text, "abc", false, Rect::default());
        let ch = p.selection_from_engine("abcd", 1, Some(3));
        assert!(ch.text && ch.selection);
        assert_eq!((p.cursor, p.anchor), (1, 3));
        p.selection_from_engine("abcd", 99, None);
        assert_eq!((p.cursor, p.anchor), (4, 4));
    }

    #[test]
    fn inactive_edits_dropped_and_dismiss_submit() {
        let mut p = InputMethodState::default();
        p.text_edited("x");
        assert!(p.take_requests().is_empty());
        p.show_from_engine(InputType::Search, "", false, Rect::default());
        p.submit();
        let r = p.take_requests();
        assert_eq!(r.len(), 2);
        assert!(matches!(&r[0], ImeRequest::Key { key, .. } if key == "Enter"));
        assert!(p.dismiss().active);
        assert_eq!(p.take_requests(), vec![ImeRequest::Dismiss]);
        assert!(!p.dismiss().active);
    }
}
