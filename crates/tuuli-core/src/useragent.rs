// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! UA strings (spec 5.4, 7.2 desktop-mode toggle).  The mobile string
//! keeps Servo's own mobile convention so upstream compat work applies to
//! us, and appends a Tuuli token so sites and bug reports can tell us
//! apart.

/// Tracks Servo's own Firefox compat token; bump with each engine rebase.
pub const FIREFOX_COMPAT_VERSION: &str = "128.0";

pub fn mobile(servo_version: &str, tuuli_version: &str) -> String {
    format!(
        "Mozilla/5.0 (Android; Mobile; rv:{ff}) Servo/{servo} Firefox/{ff} Tuuli/{tuuli}",
        ff = FIREFOX_COMPAT_VERSION,
        servo = servo_version,
        tuuli = tuuli_version
    )
}

pub fn desktop(servo_version: &str, tuuli_version: &str) -> String {
    format!(
        "Mozilla/5.0 (X11; Linux aarch64; rv:{ff}) Servo/{servo} Firefox/{ff} Tuuli/{tuuli}",
        ff = FIREFOX_COMPAT_VERSION,
        servo = servo_version,
        tuuli = tuuli_version
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strings() {
        let m = mobile("0.5.0", "0.1.0");
        assert!(m.starts_with("Mozilla/5.0 (Android; Mobile;"));
        assert!(m.contains("Servo/0.5.0") && m.contains("Tuuli/0.1.0") && m.contains("Firefox/"));
        let d = desktop("0.5.0", "0.1.0");
        assert!(!d.contains("Mobile"));
        assert!(d.contains("Linux aarch64") && d.contains("Tuuli/0.1.0"));
    }
}
