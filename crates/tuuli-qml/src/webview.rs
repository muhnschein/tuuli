// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! The QML `WebView` item: a `QQuickFramebufferObject` whose renderer hands
//! the engine an FBO (spec 5.1) and whose touch events go through the
//! gesture arbiter to the engine (spec 6).  The C++ subclass below is the
//! only place Tuuli needs C++: qmetaobject has no framebuffer-object item
//! and no touch events, so we subclass `RustObject<QQuickFramebufferObject>`
//! exactly the way the crate subclasses `RustObject<QQuickItem>`.

#![allow(non_snake_case)]

use std::cell::{Cell, RefCell};
use std::ffi::{c_void, CString};
use std::rc::Rc;
use std::time::{Duration, Instant};

use cpp::cpp;
use qmetaobject::prelude::*;
use qmetaobject::{single_shot, QObjectDescriptor, QObjectPinned, QPointer};
use qttypes::{QColor, QPointF, QRectF, QString};
use tuuli_core::browser::BrowserEvent;
use tuuli_core::engine::{EditingAction, RenderingContext};
use tuuli_core::geometry::{self, Point, Size};
use tuuli_core::gesture::{GestureArbiter, GestureConfig, GestureEvent};
use tuuli_core::input::{RawTouchKind, RawTouchPoint, RawTouchState, TouchConverter, TouchPoint};
use tuuli_core::tabs::{TabEvent, TabId};

use crate::core::{pump, register_view, with_core, with_core_opt};
use crate::objects::{InputMethodProxyObject, TabObject};

/// Layout shared with the C++ side (`TuuliRawTouch`).
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct RawTouch {
    pub id: i32,
    pub state: i32,
    pub x: f64,
    pub y: f64,
}

/// QEvent::Type values for touch events (Qt 5).
const EV_TOUCH_BEGIN: i32 = 194;
const EV_TOUCH_UPDATE: i32 = 195;
const EV_TOUCH_END: i32 = 196;
const EV_TOUCH_CANCEL: i32 = 209;
/// Qt::TouchPointState values.
const TP_PRESSED: i32 = 0x01;
const TP_MOVED: i32 = 0x02;
const TP_STATIONARY: i32 = 0x04;
const TP_RELEASED: i32 = 0x08;

const FLAG_ACCEPT: u32 = 1;
const FLAG_KEEP_GRAB: u32 = 2;
const RENDER_PAINTED: u32 = 0x8000_0000;

// The GL facts logger lives in a block of its own: the main block below is
// close to the `cpp!` macro's expansion limit.
cpp! {{
    #include <QtGui/QOpenGLContext>
    #include <QtGui/QOpenGLFunctions>
    #include <QtGui/QSurfaceFormat>
    #include <cstdio>

    // Spec 10 / M0: the driver strings and the GL level WebRender will get,
    // printed once so a device's journal records them.
    static void tuuliLogGlFacts(QOpenGLContext *gl) {
        static bool logged = false;
        if (logged) return;
        logged = true;
        QOpenGLFunctions *f = gl->functions();
        const char *vendor = reinterpret_cast<const char *>(f->glGetString(GL_VENDOR));
        const char *renderer = reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(f->glGetString(GL_VERSION));
        const char *glsl = reinterpret_cast<const char *>(f->glGetString(GL_SHADING_LANGUAGE_VERSION));
        fprintf(stderr, "tuuli: GL vendor=\"%s\" renderer=\"%s\" version=\"%s\" glsl=\"%s\"\n",
                vendor ? vendor : "?", renderer ? renderer : "?", version ? version : "?", glsl ? glsl : "?");
        const QSurfaceFormat fmt = gl->format();
        fprintf(stderr, "tuuli: GL context %s %d.%d, %d extensions; image_external=%d disjoint_timer=%d bgra8888=%d khr_debug=%d\n",
                gl->isOpenGLES() ? "GLES" : "GL", fmt.majorVersion(), fmt.minorVersion(),
                int(gl->extensions().size()),
                int(gl->hasExtension("GL_OES_EGL_image_external")),
                int(gl->hasExtension("GL_EXT_disjoint_timer_query")),
                int(gl->hasExtension("GL_EXT_texture_format_BGRA8888")),
                int(gl->hasExtension("GL_KHR_debug")));
    }

}}

cpp! {{
    #include <qmetaobject_rust.hpp>
    #include <vector>
    #include <QtCore/QByteArray>
    #include <QtCore/QPointer>
    #include <QtGui/QKeyEvent>
    #include <QtGui/QOpenGLContext>
    #include <QtGui/QOpenGLFramebufferObject>
    #include <QtGui/QOpenGLFramebufferObjectFormat>
    #include <QtGui/QOpenGLFunctions>
    #include <QtGui/QScreen>
    #include <QtGui/QTouchEvent>
    #include <QtQuick/QQuickFramebufferObject>
    #include <QtQuick/QQuickWindow>

    struct TuuliRawTouch { int id; int state; double x; double y; };

    struct Rust_WebViewItem;

    struct TuuliFboRenderer : QQuickFramebufferObject::Renderer {
        // QPointer: the scene graph deletes the node (and this renderer)
        // after the item on window shutdown, so a raw pointer would dangle.
        QPointer<Rust_WebViewItem> item;
        explicit TuuliFboRenderer(Rust_WebViewItem *item) : item(item) {}
        ~TuuliFboRenderer() override;
        QOpenGLFramebufferObject *createFramebufferObject(const QSize &size) override {
            // WebRender needs depth; stencil comes along for free on GLES.
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
            return new QOpenGLFramebufferObject(size, format);
        }
        void synchronize(QQuickFramebufferObject *) override {}
        void render() override;
    };

    struct Rust_WebViewItem : RustObject<QQuickFramebufferObject> {
        Renderer *createRenderer() const override {
            return new TuuliFboRenderer(const_cast<Rust_WebViewItem *>(this));
        }

        void touchEvent(QTouchEvent *event) override {
            const QList<QTouchEvent::TouchPoint> &pts = event->touchPoints();
            std::vector<TuuliRawTouch> raw;
            raw.reserve(pts.size());
            for (const QTouchEvent::TouchPoint &p : pts)
                raw.push_back(TuuliRawTouch { p.id(), int(p.state()), p.pos().x(), p.pos().y() });
            int kind = int(event->type());
            const TuuliRawTouch *data = raw.data();
            size_t n = raw.size();
            unsigned flags = rust!(Rust_WebViewItem_touchEvent [
                rust_object: QObjectPinned<dyn WebViewItemImpl> as "TraitObject",
                kind: i32 as "int",
                data: *const RawTouch as "const TuuliRawTouch *",
                n: usize as "size_t"
            ] -> u32 as "unsigned" {
                let points = if n == 0 { &[][..] } else { unsafe { std::slice::from_raw_parts(data, n) } };
                rust_object.borrow_mut().touch_event(kind, points)
            });
            // Spec 6.1: the enclosing SilicaFlickable must not steal drags
            // the engine owns; released for lipstick edges and pulley handoff.
            setKeepTouchGrab(flags & 2u);
            setKeepMouseGrab(flags & 2u);
            if (flags & 1u) event->accept(); else event->ignore();
        }

        bool handleKey(bool down, QKeyEvent *e) {
            int key = e->key();
            unsigned mods = unsigned(e->modifiers());
            QString text = e->text();
            bool handled = rust!(Rust_WebViewItem_keyEvent [
                rust_object: QObjectPinned<dyn WebViewItemImpl> as "TraitObject",
                down: bool as "bool",
                key: i32 as "int",
                mods: u32 as "unsigned",
                text: &QString as "const QString &"
            ] -> bool as "bool" {
                rust_object.borrow_mut().key_event(down, key, mods, text.clone())
            });
            if (handled) e->accept();
            return handled;
        }
        void keyPressEvent(QKeyEvent *e) override { if (!handleKey(true, e)) QQuickFramebufferObject::keyPressEvent(e); }
        void keyReleaseEvent(QKeyEvent *e) override { if (!handleKey(false, e)) QQuickFramebufferObject::keyReleaseEvent(e); }

        void geometryChanged(const QRectF &n, const QRectF &o) override {
            QQuickFramebufferObject::geometryChanged(n, o);
            QPointF origin = mapToScene(QPointF(0, 0));
            rust!(Rust_WebViewItem_geometryChanged [
                rust_object: QObjectPinned<dyn WebViewItemImpl> as "TraitObject",
                n: QRectF as "QRectF",
                o: QRectF as "QRectF",
                origin: QPointF as "QPointF"
            ] {
                rust_object.borrow_mut().geometry_changed(n, o, origin);
            });
        }

        void itemChange(ItemChange change, const ItemChangeData &value) override {
            QQuickFramebufferObject::itemChange(change, value);
            if (change == ItemSceneChange && value.window) {
                // Spec 5.2: keep the GL context across cover/minimise.
                value.window->setPersistentOpenGLContext(true);
                value.window->setPersistentSceneGraph(true);
                QScreen *screen = value.window->screen();
                double dpr = value.window->devicePixelRatio();
                double dpi = screen ? screen->physicalDotsPerInch() : 0.0;
                double hz = screen ? screen->refreshRate() : 60.0;
                int w = value.window->width();
                int h = value.window->height();
                rust!(Rust_WebViewItem_windowChanged [
                    rust_object: QObjectPinned<dyn WebViewItemImpl> as "TraitObject",
                    w: i32 as "int", h: i32 as "int",
                    dpr: f64 as "double", dpi: f64 as "double", hz: f64 as "double"
                ] {
                    rust_object.borrow_mut().window_changed(w, h, dpr, dpi, hz);
                });
            }
        }
    };

    TuuliFboRenderer::~TuuliFboRenderer() {
        // Render thread == GUI thread (basic loop), context current.  Two
        // cases: the scene graph is being invalidated (item alive), or the
        // item was deleted first and the node outlived it until window
        // shutdown (QPointer null, or ~RustObject has already invalidated
        // ptr_qobject).  Either way the engine's GL state must go, and the
        // item learns about it through BrowserEvent::RenderContextLost, so
        // this never touches the item.
        rust!(Tuuli_renderContextLost [] {
            crate::webview::render_context_lost();
        });
    }

    void TuuliFboRenderer::render() {
        QOpenGLFramebufferObject *fbo = framebufferObject();
        QOpenGLContext *gl = QOpenGLContext::currentContext();
        if (!fbo || !gl) return;
        tuuliLogGlFacts(gl);
        unsigned handle = fbo->handle();
        unsigned w = unsigned(fbo->width());
        unsigned h = unsigned(fbo->height());
        int major = gl->format().majorVersion();
        int minor = gl->format().minorVersion();
        bool gles = gl->isOpenGLES();
        if (!item || !item->ptr_qobject.isValid()) return;
        TraitObject ro = item->rust_object;
        unsigned packed = 0;
        if (ro.isValid()) {
            packed = rust!(Rust_WebViewItem_render [
                ro: QObjectPinned<dyn WebViewItemImpl> as "TraitObject",
                handle: u32 as "unsigned", w: u32 as "unsigned", h: u32 as "unsigned",
                major: i32 as "int", minor: i32 as "int", gles: bool as "bool"
            ] -> u32 as "unsigned" {
                ro.borrow_mut().render(handle, w, h, major, minor, gles)
            });
        }
        if (!(packed & 0x80000000u)) {
            // Nothing painted: clear to the placeholder colour (low 24 bits).
            QOpenGLFunctions *f = gl->functions();
            f->glBindFramebuffer(GL_FRAMEBUFFER, handle);
            f->glClearColor(((packed >> 16) & 0xffu) / 255.0f, ((packed >> 8) & 0xffu) / 255.0f, (packed & 0xffu) / 255.0f, 1.0f);
            f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        }
        // The engine leaves GL in an unknown state; hand Qt back a clean one.
        if (item && item->window()) item->window()->resetOpenGLState();
    }
}}

/// The trait the derive plugs into: mirrors qmetaobject's `QQuickItem`
/// trait for our `QQuickFramebufferObject` subclass.
pub trait WebViewItemImpl: QObject {
    fn get_object_description() -> &'static QObjectDescriptor
    where
        Self: Sized,
    {
        unsafe {
            &*cpp!([] -> *const QObjectDescriptor as "RustQObjectDescriptor const*" {
                return RustQObjectDescriptor::instance<Rust_WebViewItem>();
            })
        }
    }
    /// Returns FLAG_ACCEPT | FLAG_KEEP_GRAB bits.
    fn touch_event(&mut self, kind: i32, points: &[RawTouch]) -> u32;
    fn key_event(&mut self, down: bool, key: i32, modifiers: u32, text: QString) -> bool;
    fn geometry_changed(
        &mut self,
        new_geometry: QRectF,
        old_geometry: QRectF,
        origin_on_scene: QPointF,
    );
    fn window_changed(&mut self, width: i32, height: i32, dpr: f64, dpi: f64, refresh_hz: f64);
    /// GL current, FBO bound.  Returns RENDER_PAINTED or a packed 0xRRGGBB placeholder.
    fn render(
        &mut self,
        fbo: u32,
        width: u32,
        height: u32,
        gl_major: i32,
        gl_minor: i32,
        gles: bool,
    ) -> u32;
}

/// Spec 5.2: the FBO renderer (and with it the engine's rendering context)
/// is gone.  Called from the renderer destructor on the GUI thread with the
/// GL context current; tabs keep their state and the next `render()`
/// re-initialises the engine.
pub fn render_context_lost() {
    let lost = with_core_opt(|b| {
        if b.engine.is_initialized() {
            b.on_render_context_lost();
            true
        } else {
            false
        }
    })
    .unwrap_or(false);
    if lost {
        single_shot(Duration::from_millis(0), pump);
    }
}

/// The scene-graph GL context + the item's FBO, as the engine sees it.
pub struct QtRenderingContext {
    fbo: Cell<u32>,
    size: Cell<(u32, u32)>,
    version: Cell<(u32, u32)>,
    gles: Cell<bool>,
}

impl QtRenderingContext {
    fn new() -> Rc<Self> {
        Rc::new(Self {
            fbo: Cell::new(0),
            size: Cell::new((1, 1)),
            version: Cell::new((3, 2)),
            gles: Cell::new(true),
        })
    }
    fn set_frame(&self, fbo: u32, w: u32, h: u32, major: i32, minor: i32, gles: bool) {
        self.fbo.set(fbo);
        self.size.set((w.max(1), h.max(1)));
        // Spec 5.2: report GLES 3.2 whenever the driver is GLES >= 3.0.
        let (maj, min) = if gles && major >= 3 {
            (3, 2)
        } else {
            (major.max(0) as u32, minor.max(0) as u32)
        };
        self.version.set((maj, min));
        self.gles.set(gles);
    }
}

impl RenderingContext for QtRenderingContext {
    fn size(&self) -> (u32, u32) {
        self.size.get()
    }
    fn framebuffer_object(&self) -> u32 {
        self.fbo.get()
    }
    fn proc_address(&self, name: &str) -> *const c_void {
        let Ok(cname) = CString::new(name) else {
            return std::ptr::null();
        };
        let ptr = cname.as_ptr();
        cpp!(unsafe [ptr as "const char *"] -> *const c_void as "const void *" {
            QOpenGLContext *ctx = QOpenGLContext::currentContext();
            if (!ctx) return nullptr;
            return reinterpret_cast<const void *>(ctx->getProcAddress(QByteArray(ptr)));
        })
    }
    fn is_current(&self) -> bool {
        cpp!(unsafe [] -> bool as "bool" { return QOpenGLContext::currentContext() != nullptr; })
    }
    fn gl_version(&self) -> (u32, u32) {
        self.version.get()
    }
    fn is_gles(&self) -> bool {
        self.gles.get()
    }
}

#[derive(QObject)]
pub struct WebViewItem {
    base: qt_base_class!(trait WebViewItemImpl),

    tab: qt_property!(QPointer<TabObject>; WRITE set_tab NOTIFY tabChanged),
    tabChanged: qt_signal!(),
    contentDevicePixelRatio: qt_property!(f64; NOTIFY contentDevicePixelRatioChanged),
    devicePixelRatioOverride: qt_property!(f64; WRITE set_dpr_override NOTIFY contentDevicePixelRatioChanged),
    contentDevicePixelRatioChanged: qt_signal!(),
    bottomInset: qt_property!(i32; WRITE set_bottom_inset NOTIFY insetsChanged),
    topInset: qt_property!(i32; WRITE set_top_inset NOTIFY insetsChanged),
    insetsChanged: qt_signal!(),
    engineReady: qt_property!(bool; NOTIFY engineReadyChanged),
    engineFailed: qt_property!(bool; NOTIFY engineReadyChanged),
    engineReadyChanged: qt_signal!(),
    engineName: qt_property!(QString; CONST),
    inputMethod: qt_property!(RefCell<InputMethodProxyObject>; CONST),
    longPressDuration: qt_property!(i32; WRITE set_long_press NOTIFY gestureConfigChanged),
    edgeMargin: qt_property!(i32; WRITE set_edge_margin NOTIFY gestureConfigChanged),
    bottomEdgeMargin: qt_property!(i32; WRITE set_bottom_edge_margin NOTIFY gestureConfigChanged),
    gestureConfigChanged: qt_signal!(),
    placeholderColor: qt_property!(QColor; NOTIFY placeholderColorChanged),
    placeholderColorChanged: qt_signal!(),
    lastFrameMs: qt_property!(f64; NOTIFY frameStatsChanged),
    frameCount: qt_property!(i32; NOTIFY frameStatsChanged),
    frameStatsChanged: qt_signal!(),

    longPressed: qt_signal!(x: f64, y: f64),
    contextMenuRequested: qt_signal!(x: f64, y: f64, linkUrl: QString, imageUrl: QString, selectedText: QString, editable: bool),
    bottomEdgeProgress: qt_signal!(progress: f64),
    bottomEdgeFinished: qt_signal!(committed: bool),
    engineInitFailed: qt_signal!(),

    cssToItem: qt_method!(fn(&self, css: QPointF) -> QPointF),
    itemToCss: qt_method!(fn(&self, item: QPointF) -> QPointF),
    grabThumbnail: qt_method!(fn(&mut self)),
    sendEditingAction: qt_method!(fn(&mut self, action: i32)),

    // internal
    tab_id: Option<TabId>,
    converter: TouchConverter,
    arbiter: GestureArbiter,
    gesture: GestureConfig,
    ctx: Option<Rc<QtRenderingContext>>,
    init_failed: bool,
    size: Size,
    screen_dpr: f64,
    screen_dpi: f64,
    refresh_hz: f64,
    ime_cursor_rect: geometry::Rect,
    last_frame_at: Option<Instant>,
    long_press_armed: bool,
    registered: bool,
}

impl Default for WebViewItem {
    fn default() -> Self {
        let gesture = GestureConfig::default();
        let engine_name =
            with_core_opt(|b| b.engine.name().to_string()).unwrap_or_else(|| "none".into());
        Self {
            base: Default::default(),
            tab: QPointer::default(),
            tabChanged: Default::default(),
            contentDevicePixelRatio: 1.0,
            devicePixelRatioOverride: 0.0,
            contentDevicePixelRatioChanged: Default::default(),
            bottomInset: 0,
            topInset: 0,
            insetsChanged: Default::default(),
            engineReady: with_core_opt(|b| b.engine.is_initialized()).unwrap_or(false),
            engineFailed: false,
            engineReadyChanged: Default::default(),
            engineName: engine_name.into(),
            inputMethod: RefCell::new(InputMethodProxyObject::default()),
            longPressDuration: gesture.long_press.as_millis() as i32,
            edgeMargin: gesture.side_edge_margin as i32,
            bottomEdgeMargin: gesture.bottom_edge_margin as i32,
            gestureConfigChanged: Default::default(),
            placeholderColor: QColor::from_rgb(0x1a, 0x1a, 0x1a),
            placeholderColorChanged: Default::default(),
            lastFrameMs: 0.0,
            frameCount: 0,
            frameStatsChanged: Default::default(),
            longPressed: Default::default(),
            contextMenuRequested: Default::default(),
            bottomEdgeProgress: Default::default(),
            bottomEdgeFinished: Default::default(),
            engineInitFailed: Default::default(),
            cssToItem: Default::default(),
            itemToCss: Default::default(),
            grabThumbnail: Default::default(),
            sendEditingAction: Default::default(),
            tab_id: None,
            converter: TouchConverter::new(1.0),
            arbiter: GestureArbiter::new(gesture.clone()),
            gesture,
            ctx: None,
            init_failed: false,
            size: Size::default(),
            screen_dpr: 1.0,
            screen_dpi: 0.0,
            refresh_hz: 60.0,
            ime_cursor_rect: geometry::Rect::default(),
            last_frame_at: None,
            long_press_armed: false,
            registered: false,
        }
    }
}

impl WebViewItem {
    fn ensure_registered(&mut self) {
        if !self.registered {
            self.registered = true;
            register_view(QPointer::from(&*self));
        }
    }

    /// `QQuickItem::update()` on our C++ object.
    pub fn update(&self) {
        let obj = self.get_cpp_object();
        cpp!(unsafe [obj as "QQuickItem *"] { if (obj) obj->update(); });
    }

    fn dpr(&self) -> f64 {
        self.contentDevicePixelRatio
    }

    fn set_tab(&mut self, tab: QPointer<TabObject>) {
        self.ensure_registered();
        let id = tab
            .as_pinned()
            .map(|t| t.borrow().tabId as TabId)
            .filter(|id| *id != 0);
        self.tab = tab;
        self.tab_id = id;
        self.arbiter.reset();
        self.inputMethod.borrow_mut().set_tab(id);
        let hide = self.inputMethod.borrow_mut().hide_from_engine();
        self.inputMethod.borrow().emit_changes(hide);
        if let Some(id) = id {
            with_core(|b| {
                b.tabs.borrow_mut().ensure_webview(id);
            });
            self.push_geometry();
        }
        pump();
        self.tabChanged();
        self.update();
    }

    fn set_dpr_override(&mut self, dpr: f64) {
        self.devicePixelRatioOverride = dpr.max(0.0);
        self.resolve_dpr();
    }

    fn resolve_dpr(&mut self) {
        let dpr = if self.devicePixelRatioOverride > 0.0 {
            self.devicePixelRatioOverride
        } else {
            geometry::derive_device_pixel_ratio(self.screen_dpr, self.screen_dpi)
        };
        if (dpr - self.contentDevicePixelRatio).abs() < 1e-9 {
            return;
        }
        self.contentDevicePixelRatio = dpr;
        self.converter.dpr = dpr;
        let size = self.size;
        with_core_opt(|b| {
            b.tabs
                .borrow_mut()
                .set_viewport_geometry((size.width as u32, size.height as u32), dpr);
        });
        if let Some(wv) = self.current_webview() {
            wv.set_device_pixel_ratio(dpr);
        }
        self.contentDevicePixelRatioChanged();
    }

    fn current_webview(&self) -> Option<Rc<dyn tuuli_core::engine::WebView>> {
        let id = self.tab_id?;
        with_core_opt(|b| b.tabs.borrow().by_id(id).and_then(|t| t.webview.clone())).flatten()
    }

    fn push_geometry(&mut self) {
        if self.size.is_empty() {
            return;
        }
        let (w, h) = (self.size.width as u32, self.size.height as u32);
        let dpr = self.dpr();
        with_core_opt(|b| b.tabs.borrow_mut().set_viewport_geometry((w, h), dpr));
        if let Some(wv) = self.current_webview() {
            wv.set_size(w, h);
            wv.set_device_pixel_ratio(dpr);
        }
        self.push_viewport();
    }

    /// Spec 6.3: the surface is never resized for the keyboard; only the
    /// viewport rect handed to the engine changes, and the caret is
    /// scrolled into view.
    fn push_viewport(&self) {
        let Some(wv) = self.current_webview() else {
            return;
        };
        let layout = geometry::layout_viewport(
            self.size,
            self.bottomInset as f64,
            self.topInset as f64,
            self.dpr(),
        );
        wv.set_viewport_rect(layout.visible_device);
        if self.inputMethod.borrow().state.active && !self.ime_cursor_rect.is_null() {
            let delta =
                geometry::scroll_delta_to_reveal(self.ime_cursor_rect, layout.visible_css, 16.0);
            if !delta.is_zero() {
                if let Some(id) = self.tab_id {
                    let scroll = with_core_opt(|b| b.tabs.borrow().by_id(id).map(|t| t.scroll))
                        .flatten()
                        .unwrap_or_default();
                    wv.scroll_to(scroll + delta);
                }
            }
        }
    }

    fn set_bottom_inset(&mut self, px: i32) {
        let px = px.max(0);
        if self.bottomInset == px {
            return;
        }
        self.bottomInset = px;
        self.push_viewport();
        self.insetsChanged();
    }

    fn set_top_inset(&mut self, px: i32) {
        let px = px.max(0);
        if self.topInset == px {
            return;
        }
        self.topInset = px;
        self.push_viewport();
        self.insetsChanged();
    }

    fn set_long_press(&mut self, ms: i32) {
        self.longPressDuration = ms.max(100);
        self.gesture.long_press = Duration::from_millis(self.longPressDuration as u64);
        self.arbiter.set_config(self.gesture.clone());
        self.gestureConfigChanged();
    }

    fn set_edge_margin(&mut self, px: i32) {
        self.edgeMargin = px.max(0);
        self.gesture.side_edge_margin = self.edgeMargin as f64;
        self.gesture.top_edge_margin = self.edgeMargin as f64;
        self.arbiter.set_config(self.gesture.clone());
        self.gestureConfigChanged();
    }

    fn set_bottom_edge_margin(&mut self, px: i32) {
        self.bottomEdgeMargin = px.max(0);
        self.gesture.bottom_edge_margin = self.bottomEdgeMargin as f64;
        self.arbiter.set_config(self.gesture.clone());
        self.gestureConfigChanged();
    }

    fn cssToItem(&self, css: QPointF) -> QPointF {
        let p =
            geometry::css_to_device(Point::new(css.x, css.y), self.dpr()) + self.converter.origin;
        QPointF { x: p.x, y: p.y }
    }

    fn itemToCss(&self, item: QPointF) -> QPointF {
        let p = geometry::device_to_css(
            Point::new(item.x, item.y) - self.converter.origin,
            self.dpr(),
        );
        QPointF { x: p.x, y: p.y }
    }

    fn grabThumbnail(&mut self) {
        if let Some(id) = self.tab_id {
            with_core(|b| b.tabs.borrow_mut().capture_thumbnail(id));
            pump();
        }
    }

    fn sendEditingAction(&mut self, action: i32) {
        if let (Some(wv), Some(a)) = (self.current_webview(), EditingAction::from_index(action)) {
            wv.editing_action(a);
        }
    }

    fn forward(&self, points: &[TouchPoint]) {
        let Some(wv) = self.current_webview() else {
            return;
        };
        for p in points {
            wv.touch(p.phase, p.id, p.css);
        }
    }

    fn arm_long_press(&mut self) {
        let Some(deadline) = self.arbiter.long_press_deadline() else {
            return;
        };
        if self.long_press_armed {
            return;
        }
        self.long_press_armed = true;
        let qptr = QPointer::from(&*self);
        let delay = deadline.saturating_duration_since(Instant::now());
        single_shot(delay + Duration::from_millis(1), move || {
            if let Some(item) = qptr.as_pinned() {
                item.borrow_mut().on_long_press_timer();
            }
        });
    }

    fn on_long_press_timer(&mut self) {
        self.long_press_armed = false;
        let Some((ev, cancels)) = self.arbiter.fire_long_press_if_due(Instant::now()) else {
            // Deadline moved (e.g. a new sequence): re-arm for it.
            self.arm_long_press();
            return;
        };
        self.forward(&cancels);
        if let GestureEvent::LongPressed { device, css } = ev {
            if let Some(wv) = self.current_webview() {
                wv.request_context_menu(css);
            }
            self.longPressed(device.x, device.y);
        }
    }

    /// Dispatcher entry: react to what the core produced.
    pub(crate) fn on_browser_event(&mut self, ev: &BrowserEvent) {
        let mine = |id: &TabId| Some(*id) == self.tab_id;
        match ev {
            BrowserEvent::FrameReady { tab } if mine(tab) => self.update(),
            BrowserEvent::EngineInitialized => {
                self.engineReady = true;
                if let Some(id) = self.tab_id {
                    with_core(|b| {
                        b.tabs.borrow_mut().ensure_webview(id);
                    });
                    self.push_geometry();
                }
                self.engineReadyChanged();
                self.update();
            }
            BrowserEvent::RenderContextLost => {
                self.engineReady = false;
                self.engineReadyChanged();
            }
            BrowserEvent::EngineCrashed { .. } => {
                self.engineReady = with_core_opt(|b| b.engine.is_initialized()).unwrap_or(false);
                self.engineReadyChanged();
            }
            BrowserEvent::Tab(TabEvent::WebViewAttached { id }) if mine(id) => {
                self.push_geometry();
                self.update();
            }
            BrowserEvent::ContextMenu { tab, info } if mine(tab) => {
                let p = self.cssToItem(QPointF {
                    x: info.css.x,
                    y: info.css.y,
                });
                self.contextMenuRequested(
                    p.x,
                    p.y,
                    info.link_url.clone().unwrap_or_default().into(),
                    info.image_url.clone().unwrap_or_default().into(),
                    info.selected_text.clone().into(),
                    info.editable,
                );
            }
            BrowserEvent::ImeShow {
                tab,
                input_type,
                text,
                multiline,
                cursor_rect,
            } if mine(tab) => {
                self.ime_cursor_rect = *cursor_rect;
                let ch = self.inputMethod.borrow_mut().show_from_engine(
                    *input_type,
                    text,
                    *multiline,
                    *cursor_rect,
                );
                self.inputMethod.borrow().emit_changes(ch);
                self.push_viewport();
            }
            BrowserEvent::ImeHide { tab } if mine(tab) => {
                let ch = self.inputMethod.borrow_mut().hide_from_engine();
                self.inputMethod.borrow().emit_changes(ch);
            }
            BrowserEvent::ImeSelection {
                tab,
                text,
                cursor,
                anchor,
            } if mine(tab) => {
                let ch = self.inputMethod.borrow_mut().selection_from_engine(
                    text,
                    *cursor,
                    Some(*anchor),
                );
                self.inputMethod.borrow().emit_changes(ch);
            }
            _ => {}
        }
    }
}

impl WebViewItemImpl for WebViewItem {
    fn touch_event(&mut self, kind: i32, points: &[RawTouch]) -> u32 {
        self.ensure_registered();
        let kind = match kind {
            EV_TOUCH_BEGIN => RawTouchKind::Begin,
            EV_TOUCH_UPDATE => RawTouchKind::Update,
            EV_TOUCH_END => RawTouchKind::End,
            EV_TOUCH_CANCEL => RawTouchKind::Cancel,
            _ => return 0,
        };
        if kind == RawTouchKind::Begin {
            let viewport_css_h = geometry::size_device_to_css(self.size, self.dpr()).height;
            let edges = self.tab_id.and_then(|id| {
                with_core_opt(|b| {
                    b.tabs
                        .borrow()
                        .by_id(id)
                        .map(|t| t.content_edges(viewport_css_h))
                })
                .flatten()
            });
            let (top, bottom) = edges.unwrap_or((true, true));
            self.arbiter.set_content_edges(top, bottom);
            let url = self
                .tab_id
                .and_then(|id| {
                    with_core_opt(|b| b.tabs.borrow().by_id(id).map(|t| t.url.clone())).flatten()
                })
                .unwrap_or_default();
            let interaction = if points.len() > 1 { "pinch" } else { "scroll" };
            with_core_opt(|b| b.perf.interaction_begin(interaction, &url));
        }
        let raw: Vec<RawTouchPoint> = points
            .iter()
            .map(|p| RawTouchPoint {
                id: p.id,
                state: match p.state {
                    TP_PRESSED => RawTouchState::Pressed,
                    TP_MOVED => RawTouchState::Moved,
                    TP_RELEASED => RawTouchState::Released,
                    _ => RawTouchState::Stationary,
                },
                pos: Point::new(p.x, p.y),
            })
            .collect();
        let _ = TP_STATIONARY;
        let converted = self.converter.convert(kind, &raw);
        let out = self.arbiter.process(&converted, Instant::now());
        for ev in &out.events {
            match ev {
                GestureEvent::BottomEdgeProgress(p) => self.bottomEdgeProgress(*p),
                GestureEvent::BottomEdgeFinished { committed } => {
                    self.bottomEdgeFinished(*committed)
                }
                GestureEvent::LongPressed { .. } => {}
            }
        }
        if !out.accepted {
            return 0;
        }
        self.forward(&out.forward);
        if out.handoff {
            // Let the parent Flickable (pulley menus) take the rest of the drag.
            return 0;
        }
        self.arm_long_press();
        let ended = matches!(kind, RawTouchKind::End | RawTouchKind::Cancel)
            && self.arbiter.active_count() == 0;
        if ended {
            with_core_opt(|b| b.perf.interaction_end());
            FLAG_ACCEPT
        } else {
            FLAG_ACCEPT | FLAG_KEEP_GRAB
        }
    }

    fn key_event(&mut self, down: bool, key: i32, modifiers: u32, text: QString) -> bool {
        let Some(wv) = self.current_webview() else {
            return false;
        };
        // Qt::KeyboardModifier bits -> engine bits (shift, ctrl, alt, meta).
        let mut m = 0;
        if modifiers & 0x0200_0000 != 0 {
            m |= 1;
        }
        if modifiers & 0x0400_0000 != 0 {
            m |= 2;
        }
        if modifiers & 0x0800_0000 != 0 {
            m |= 4;
        }
        if modifiers & 0x1000_0000 != 0 {
            m |= 8;
        }
        wv.key(
            down,
            &tuuli_core::ime::w3c_key_name(key, &text.to_string()),
            m,
        );
        true
    }

    fn geometry_changed(
        &mut self,
        new_geometry: QRectF,
        old_geometry: QRectF,
        origin_on_scene: QPointF,
    ) {
        self.ensure_registered();
        self.size = Size::new(new_geometry.width, new_geometry.height);
        self.gesture.item_origin_on_screen = Point::new(origin_on_scene.x, origin_on_scene.y);
        self.arbiter.set_config(self.gesture.clone());
        if (new_geometry.width - old_geometry.width).abs() > 0.5
            || (new_geometry.height - old_geometry.height).abs() > 0.5
        {
            self.push_geometry();
        }
    }

    fn window_changed(&mut self, width: i32, height: i32, dpr: f64, dpi: f64, refresh_hz: f64) {
        self.ensure_registered();
        self.screen_dpr = dpr;
        self.screen_dpi = dpi;
        self.refresh_hz = if refresh_hz > 0.0 { refresh_hz } else { 60.0 };
        self.gesture.screen = Size::new(width as f64, height as f64);
        self.arbiter.set_config(self.gesture.clone());
        self.resolve_dpr();
    }

    fn render(
        &mut self,
        fbo: u32,
        width: u32,
        height: u32,
        gl_major: i32,
        gl_minor: i32,
        gles: bool,
    ) -> u32 {
        let now = Instant::now();
        if let Some(last) = self.last_frame_at {
            self.lastFrameMs = last.elapsed().as_secs_f64() * 1000.0;
            let budget = 1000.0 / self.refresh_hz;
            let ms = self.lastFrameMs;
            with_core_opt(|b| b.perf.interaction_frame(ms, budget));
        }
        self.last_frame_at = Some(now);
        self.frameCount += 1;

        let ctx = self.ctx.get_or_insert_with(QtRenderingContext::new).clone();
        ctx.set_frame(fbo, width, height, gl_major, gl_minor, gles);

        let mut painted = false;
        let mut init_failed_now = false;
        let tab_id = self.tab_id;
        let failed_before = self.init_failed;
        let on_gui_thread = with_core_opt(|b| {
            if !b.engine.is_initialized() && !failed_before {
                // Spec 5.3 / M0: the engine compiles WebRender's shaders
                // here, on the hybris driver.  Failure is final.
                if b.initialize_engine(ctx.clone()).is_err() {
                    init_failed_now = true;
                    return;
                }
            }
            if b.engine.is_initialized() {
                let wv =
                    tab_id.and_then(|id| b.tabs.borrow().by_id(id).and_then(|t| t.webview.clone()));
                if let Some(wv) = wv {
                    painted = wv.paint();
                }
            }
        })
        .is_some();
        if self.frameCount == 1 {
            // Spec 15 / M0: the panel facts the budgets depend on, once per
            // run, from the first frame (the window has its size by then).
            eprintln!(
                "tuuli: first frame {width}x{height} px, window {}x{}, Qt dpr {}, physical dpi {:.0}, refresh {:.0} Hz, content dpr {}",
                self.gesture.screen.width, self.gesture.screen.height, self.screen_dpr, self.screen_dpi, self.refresh_hz, self.contentDevicePixelRatio
            );
            if on_gui_thread {
                eprintln!("tuuli: first frame rendered on the GUI thread (basic render loop)");
            } else {
                log::error!("WebView rendered off the GUI thread: Tuuli needs QSG_RENDER_LOOP=basic (see docs/ARCHITECTURE.md)");
                eprintln!("tuuli: WebView rendered off the GUI thread (threaded render loop); the engine cannot initialise");
            }
        }
        if init_failed_now {
            self.init_failed = true;
            self.engineFailed = true;
            let qptr = QPointer::from(&*self);
            // Signals from inside render: defer to the event loop.
            single_shot(Duration::from_millis(0), move || {
                if let Some(item) = qptr.as_pinned() {
                    item.borrow().engineInitFailed();
                    item.borrow().engineReadyChanged();
                }
                pump();
            });
        }
        // The engine may have queued events (Initialized, frames); deliver
        // them once we are back in the event loop, not inside render().
        let needs_pump = with_core_opt(|b| b.has_pending_events()).unwrap_or(false);
        if needs_pump {
            single_shot(Duration::from_millis(0), pump);
        }
        self.frameStatsChanged();
        if painted {
            RENDER_PAINTED
        } else {
            let c = &self.placeholderColor;
            let (r, g, b) = (
                c.get_rgba().0 as u32,
                c.get_rgba().1 as u32,
                c.get_rgba().2 as u32,
            );
            (r << 16) | (g << 8) | b
        }
    }
}
