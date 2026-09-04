/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_SERVOPREFS_H
#define TUULI_SERVOPREFS_H

/*
 * Servo preference names Tuuli sets (spec 9.4 defaults).  The names are the
 * pinned release's; they are validated against the tag's
 * components/config/prefs.rs at every rebase (docs/UPSTREAM.md).  Kept in
 * one table so a rename upstream is a one-line change here.
 */

namespace Tuuli {
namespace ServoPref {

constexpr const char* NetworkBlockThirdPartyCookies = "network_cookies_block_third_party";
constexpr const char* NetworkSendDnt = "network_http_dnt";
constexpr const char* NetworkSendGpc = "network_http_gpc";
constexpr const char* NetworkReferrerPolicy = "network_http_referrer_policy";
constexpr const char* NetworkEnforceTlsLocalhost = "network_enforce_tls_localhost";
constexpr const char* JsEnabled = "js_enabled";
constexpr const char* JsBaselineJit = "js_baseline_jit_enabled";
constexpr const char* JsIonJit = "js_ion_enabled";
constexpr const char* LayoutThreads = "layout_threads";
constexpr const char* MediaGlVideo = "media_glvideo_enabled";
constexpr const char* DomTouchEnabled = "dom_touch_enabled";
constexpr const char* ShellBackgroundColor = "shell_background_color_rgba";
constexpr const char* GfxTextAntialiasing = "gfx_text_antialiasing_enabled";
constexpr const char* GfxSubpixelAntialiasing = "gfx_subpixel_text_antialiasing_enabled";
constexpr const char* WebRenderDebug = "gfx_webrender_debug_flags";

} // namespace ServoPref
} // namespace Tuuli

#endif
