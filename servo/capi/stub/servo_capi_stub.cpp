/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

/*
 * No-op implementation of servo_capi.h.
 *
 * Exists so that the real ServoEngine shim (src/lib/engine/servoengine.cpp)
 * can be compiled and linked on a host without libservo, keeping the shim
 * honest against the ABI it targets.  It renders nothing, loads nothing and
 * never calls back.  Never ship this.
 */

#include "../servo_capi.h"

#include <cstdlib>
#include <cstring>

struct ServoInstance { int dummy; };
struct ServoWebView { bool priv; };
struct ServoRenderingContext { int dummy; };
struct ServoPermissionRequest { int dummy; };
struct ServoNavigationRequest { int dummy; };
struct ServoDownload { int dummy; };
struct ServoSimpleDialog { int dummy; };

extern "C" {

int servo_capi_version_check(uint32_t major, uint32_t minor)
{
    return (major == SERVO_CAPI_VERSION_MAJOR && minor == SERVO_CAPI_VERSION_MINOR) ? 0 : 1;
}
const char* servo_version_string(void) { return "0.0.0-stub"; }
void servo_string_free(char* s) { std::free(s); }

ServoRenderingContext* servo_rendering_context_new_external(
    const ServoRenderingContextVTable*, ServoGlApi, uint32_t, uint32_t)
{
    return new ServoRenderingContext{0};
}
void servo_rendering_context_free(ServoRenderingContext* ctx) { delete ctx; }

ServoInstance* servo_init(const ServoInstanceConfig*, ServoRenderingContext*,
                          const ServoInstanceCallbacks*)
{
    return new ServoInstance{0};
}
bool servo_spin_event_loop(ServoInstance*) { return true; }
void servo_deinit(ServoInstance* s) { delete s; }
void servo_set_pref(ServoInstance*, const char*, const char*) {}
void servo_set_proxy(ServoInstance*, const ServoProxyConfig*) {}
void servo_clear_site_data(ServoInstance*, const char*, uint32_t) {}

ServoWebView* servo_webview_new(ServoInstance*, ServoRenderingContext*,
                                const ServoWebViewCallbacks*, bool private_browsing,
                                float, ServoSize)
{
    return new ServoWebView{private_browsing};
}
ServoWebView* servo_webview_new_auxiliary(ServoInstance*, ServoWebView* parent,
                                          const ServoWebViewCallbacks*)
{
    return new ServoWebView{parent ? parent->priv : false};
}
void servo_webview_close(ServoWebView* wv) { delete wv; }
bool servo_webview_is_private(const ServoWebView* wv) { return wv && wv->priv; }
void servo_webview_load(ServoWebView*, const char*) {}
void servo_webview_reload(ServoWebView*) {}
void servo_webview_stop(ServoWebView*) {}
void servo_webview_go_back(ServoWebView*, uint32_t) {}
void servo_webview_go_forward(ServoWebView*, uint32_t) {}
char* servo_webview_url(const ServoWebView*) { return nullptr; }
char* servo_webview_title(const ServoWebView*) { return nullptr; }
ServoLoadStatus servo_webview_load_status(const ServoWebView*) { return SERVO_LOAD_COMPLETE; }
void servo_webview_show(ServoWebView*, bool) {}
void servo_webview_hide(ServoWebView*) {}
void servo_webview_focus(ServoWebView*) {}
void servo_webview_blur(ServoWebView*) {}
void servo_webview_resize(ServoWebView*, ServoSize) {}
void servo_webview_set_viewport_rect(ServoWebView*, uint32_t, uint32_t, uint32_t, uint32_t) {}
void servo_webview_set_hidpi_scale_factor(ServoWebView*, float) {}
void servo_webview_set_pinch_zoom(ServoWebView*, float) {}
void servo_webview_set_page_zoom(ServoWebView*, float) {}
void servo_webview_reset_zoom(ServoWebView*) {}
void servo_webview_scroll_to(ServoWebView*, float, float) {}
bool servo_webview_paint(ServoWebView*) { return false; }
void servo_webview_touch(ServoWebView*, ServoTouchEventType, int32_t, float, float) {}
void servo_webview_key(ServoWebView*, ServoKeyState, const char*, uint32_t) {}
void servo_webview_ime_composition(ServoWebView*, ServoCompositionState, const char*) {}
void servo_webview_ime_dismissed(ServoWebView*) {}
void servo_webview_editing_action(ServoWebView*, ServoEditingAction) {}
void servo_webview_request_context_menu(ServoWebView*, float, float) {}
void servo_webview_find(ServoWebView*, const char*, bool) {}
void servo_webview_find_next(ServoWebView*, bool) {}
void servo_webview_find_clear(ServoWebView*) {}
void servo_webview_add_user_stylesheet(ServoWebView*, const char*, const char*) {}
void servo_webview_remove_user_stylesheet(ServoWebView*, const char*) {}
void servo_webview_add_user_script(ServoWebView*, const char*, const char*) {}
void servo_webview_remove_user_script(ServoWebView*, const char*) {}
void servo_webview_set_user_agent(ServoWebView*, const char*) {}
void servo_webview_evaluate_javascript(ServoWebView*, const char*, ServoJsResultCallback cb, void* ud)
{
    if (cb) cb(ud, false, "stub engine");
}
uint8_t* servo_webview_capture(ServoWebView*, uint32_t* w, uint32_t* h)
{
    if (w) *w = 0;
    if (h) *h = 0;
    return nullptr;
}
void servo_pixels_free(uint8_t* p) { std::free(p); }

ServoPermissionKind servo_permission_request_kind(const ServoPermissionRequest*) { return SERVO_PERMISSION_GEOLOCATION; }
const char* servo_permission_request_origin(const ServoPermissionRequest*) { return ""; }
void servo_permission_request_allow(ServoPermissionRequest*) {}
void servo_permission_request_deny(ServoPermissionRequest*) {}
const char* servo_navigation_request_url(const ServoNavigationRequest*) { return ""; }
void servo_navigation_request_allow(ServoNavigationRequest*) {}
void servo_navigation_request_deny(ServoNavigationRequest*) {}
ServoSimpleDialogKind servo_simple_dialog_kind(const ServoSimpleDialog*) { return SERVO_DIALOG_ALERT; }
const char* servo_simple_dialog_message(const ServoSimpleDialog*) { return ""; }
const char* servo_simple_dialog_default_value(const ServoSimpleDialog*) { return ""; }
void servo_simple_dialog_accept(ServoSimpleDialog*, const char*) {}
void servo_simple_dialog_dismiss(ServoSimpleDialog*) {}
void servo_download_accept(ServoDownload*, const char*) {}
void servo_download_reject(ServoDownload*) {}
void servo_download_cancel(ServoDownload*) {}

} /* extern "C" */
