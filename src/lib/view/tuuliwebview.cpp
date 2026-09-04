/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "tuuliwebview.h"
#include "browsercontext.h"
#include "input/cssgeometry.h"
#include "webviewrenderer.h"

#include <QKeyEvent>
#include <QQuickWindow>
#include <QScreen>
#include <QTouchEvent>

namespace Tuuli {

TuuliWebView::TuuliWebView(QQuickItem* parent)
    : QQuickFramebufferObject(parent)
    , m_ime(new InputMethodProxy(this))
    , m_arbiter(new GestureArbiter(this))
{
    setAcceptedMouseButtons(Qt::NoButton);
    setFlag(ItemAcceptsInputMethod, false); // the hidden QML TextInput proxy owns IME
    setFlag(ItemHasContents, true);
    setMirrorVertically(false);

    // QML may construct this item before the Browser singleton is first
    // touched; the context (and the engine) must exist before we render.
    BrowserContext* ctx = BrowserContext::ensureCreated();
    m_engine = ctx->engine();
    ctx->registerWebView(this);
    connect(m_engine, &Engine::initialized, this, &TuuliWebView::onEngineInitialized);
    connect(m_engine, &Engine::renderContextLost, this, &TuuliWebView::engineReadyChanged);

    connect(m_arbiter, &GestureArbiter::longPressed, this, &TuuliWebView::onLongPressed);
    connect(m_arbiter, &GestureArbiter::engineTouchesCancelled, this, &TuuliWebView::onEngineTouchesCancelled);
    connect(m_arbiter, &GestureArbiter::bottomEdgeProgress, this, &TuuliWebView::bottomEdgeProgress);
    connect(m_arbiter, &GestureArbiter::bottomEdgeFinished, this, &TuuliWebView::bottomEdgeFinished);

    connect(m_ime, &InputMethodProxy::keyRequested, this, [this](bool down, const QString& key, int mods) {
        if (WebViewHandle* h = currentHandle())
            h->key(down, key, static_cast<Qt::KeyboardModifiers>(mods));
    });
    connect(m_ime, &InputMethodProxy::compositionRequested, this, [this](int state, const QString& text) {
        if (WebViewHandle* h = currentHandle())
            h->imeComposition(static_cast<CompositionState>(state), text);
    });
    connect(m_ime, &InputMethodProxy::dismissRequested, this, [this]() {
        if (WebViewHandle* h = currentHandle())
            h->imeDismissed();
    });

    updateArbiterConfig();
}

TuuliWebView::~TuuliWebView()
{
    if (BrowserContext* ctx = BrowserContext::instance())
        ctx->unregisterWebView(this);
}

QQuickFramebufferObject::Renderer* TuuliWebView::createRenderer() const
{
    return new WebViewRenderer(const_cast<TuuliWebView*>(this));
}

bool TuuliWebView::engineReady() const
{
    return m_engine && m_engine->isInitialized();
}

QString TuuliWebView::engineName() const
{
    return m_engine ? m_engine->name() : QStringLiteral("none");
}

WebViewHandle* TuuliWebView::currentHandle() const
{
    return m_tab ? m_tab->handle() : nullptr;
}

void TuuliWebView::setTab(Tab* tab)
{
    if (m_tab == tab)
        return;
    if (m_tab)
        detachTab(m_tab);
    m_tab = tab;
    if (m_tab)
        attachTab(m_tab);
    m_arbiter->reset();
    m_ime->hideFromEngine();
    emit tabChanged();
    update();
}

void TuuliWebView::attachTab(Tab* tab)
{
    connect(tab, &Tab::frameReadySignal, this, &TuuliWebView::onFrameReady);
    connect(tab, &Tab::hasWebViewChanged, this, [this]() { pushGeometry(); update(); });
    connect(tab, &Tab::imeShow, this, &TuuliWebView::onImeShow);
    connect(tab, &Tab::imeHide, this, [this]() { m_ime->hideFromEngine(); });
    connect(tab, &Tab::imeSelection, this, [this](const QString& t, int c, int a) { m_ime->selectionFromEngine(t, c, a); });
    connect(tab, &Tab::contextMenu, this,
            [this](const QPointF& css, const QUrl& link, const QUrl& image, const QString& sel, bool editable) {
                const QPointF p = cssToItem(css);
                emit contextMenuRequested(p.x(), p.y(), link, image, sel, editable);
            });
    if (BrowserContext* ctx = BrowserContext::instance())
        ctx->tabs()->ensureWebView(tab);
    pushGeometry();
}

void TuuliWebView::detachTab(Tab* tab)
{
    disconnect(tab, nullptr, this, nullptr);
}

void TuuliWebView::onEngineInitialized()
{
    emit engineReadyChanged();
    if (m_tab) {
        if (BrowserContext* ctx = BrowserContext::instance())
            ctx->tabs()->ensureWebView(m_tab);
        pushGeometry();
    }
    update();
}

void TuuliWebView::onFrameReady()
{
    update();
}

void TuuliWebView::syncFrameStats(qint64 lastFrameMs)
{
    m_lastFrameMs = lastFrameMs;
    ++m_frameCount;
    if (BrowserContext* ctx = BrowserContext::instance()) {
        qreal budget = 1000.0 / 60.0;
        if (QQuickWindow* w = window())
            if (w->screen() && w->screen()->refreshRate() > 0)
                budget = 1000.0 / w->screen()->refreshRate();
        ctx->perfLog()->interactionFrame(lastFrameMs, budget);
    }
    // Called with the GUI thread blocked; defer the notify to the GUI loop.
    QMetaObject::invokeMethod(this, "frameStatsChanged", Qt::QueuedConnection);
}

void TuuliWebView::reportEngineInitFailure()
{
    m_engineFailed = true;
    QMetaObject::invokeMethod(this, "engineInitFailed", Qt::QueuedConnection);
    QMetaObject::invokeMethod(this, "engineReadyChanged", Qt::QueuedConnection);
}

/* ---- Geometry / DPR --------------------------------------------------- */

void TuuliWebView::resolveDevicePixelRatio()
{
    qreal dpr = 1.0;
    if (m_dprOverride > 0) {
        dpr = m_dprOverride;
    } else if (QQuickWindow* w = window()) {
        QScreen* screen = w->screen();
        dpr = Css::deriveDevicePixelRatio(w->devicePixelRatio(), screen ? screen->physicalDotsPerInch() : 0);
    }
    if (qFuzzyCompare(dpr, m_dpr))
        return;
    m_dpr = dpr;
    m_converter.setDevicePixelRatio(m_dpr);
    if (WebViewHandle* h = currentHandle())
        h->setDevicePixelRatio(m_dpr);
    if (BrowserContext* ctx = BrowserContext::instance())
        ctx->tabs()->setViewportGeometry(size().toSize(), m_dpr);
    emit contentDevicePixelRatioChanged();
}

void TuuliWebView::setDevicePixelRatioOverride(qreal dpr)
{
    if (qFuzzyCompare(m_dprOverride, dpr))
        return;
    m_dprOverride = dpr;
    resolveDevicePixelRatio();
}

void TuuliWebView::pushGeometry()
{
    const QSize s = size().toSize();
    if (s.isEmpty())
        return;
    if (BrowserContext* ctx = BrowserContext::instance())
        ctx->tabs()->setViewportGeometry(s, m_dpr);
    if (WebViewHandle* h = currentHandle()) {
        h->setSize(s);
        h->setDevicePixelRatio(m_dpr);
    }
    pushViewport();
}

void TuuliWebView::pushViewport()
{
    WebViewHandle* h = currentHandle();
    if (!h)
        return;
    const Css::ViewportLayout layout = Css::layoutViewport(size().toSize(), m_bottomInset, m_topInset, m_dpr);
    h->setViewportRect(layout.visibleDevice);
    // Keep the focused editable visible above the keyboard (spec 6.3).
    if (m_ime->active() && !m_imeCursorRect.isNull()) {
        const QPointF delta = Css::scrollDeltaToReveal(m_imeCursorRect, layout.visibleCss, 16.0);
        if (!delta.isNull() && m_tab)
            h->scrollTo(m_tab->scrollOffset() + delta);
    }
}

void TuuliWebView::setBottomInset(int px)
{
    px = qMax(0, px);
    if (m_bottomInset == px)
        return;
    m_bottomInset = px;
    pushViewport();
    emit insetsChanged();
}

void TuuliWebView::setTopInset(int px)
{
    px = qMax(0, px);
    if (m_topInset == px)
        return;
    m_topInset = px;
    pushViewport();
    emit insetsChanged();
}

void TuuliWebView::geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry)
{
    QQuickFramebufferObject::geometryChanged(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        pushGeometry();
    updateArbiterConfig();
}

void TuuliWebView::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickFramebufferObject::itemChange(change, value);
    if (change == ItemSceneChange && value.window) {
        // Spec 5.2: keep the context across cover/minimise; Qt defaults to
        // persistent but say so explicitly.
        value.window->setPersistentOpenGLContext(true);
        value.window->setPersistentSceneGraph(true);
        resolveDevicePixelRatio();
        updateArbiterConfig();
    }
}

void TuuliWebView::updateArbiterConfig()
{
    if (QQuickWindow* w = window()) {
        m_gestureConfig.screenDevice = w->size();
        m_gestureConfig.itemOriginOnScreen = mapToScene(QPointF(0, 0));
    } else {
        m_gestureConfig.screenDevice = size().toSize();
        m_gestureConfig.itemOriginOnScreen = QPointF(0, 0);
    }
    m_arbiter->setConfig(m_gestureConfig);
}

void TuuliWebView::setLongPressDuration(int ms)
{
    ms = qMax(100, ms);
    if (m_gestureConfig.longPressMs == ms) return;
    m_gestureConfig.longPressMs = ms;
    updateArbiterConfig();
    emit gestureConfigChanged();
}

void TuuliWebView::setEdgeMargin(int px)
{
    px = qMax(0, px);
    if (m_gestureConfig.sideEdgeMargin == px) return;
    m_gestureConfig.sideEdgeMargin = px;
    m_gestureConfig.topEdgeMargin = px;
    updateArbiterConfig();
    emit gestureConfigChanged();
}

void TuuliWebView::setBottomEdgeMargin(int px)
{
    px = qMax(0, px);
    if (m_gestureConfig.bottomEdgeMargin == px) return;
    m_gestureConfig.bottomEdgeMargin = px;
    updateArbiterConfig();
    emit gestureConfigChanged();
}

void TuuliWebView::setPlaceholderColor(const QColor& c)
{
    if (m_placeholder == c) return;
    m_placeholder = c;
    emit placeholderColorChanged();
    update();
}

QPointF TuuliWebView::cssToItem(const QPointF& css) const
{
    return Css::cssToDevice(css, m_dpr) + m_converter.origin();
}

QPointF TuuliWebView::itemToCss(const QPointF& item) const
{
    return Css::deviceToCss(item - m_converter.origin(), m_dpr);
}

/* ---- Input ------------------------------------------------------------ */

void TuuliWebView::touchEvent(QTouchEvent* event)
{
    if (event->type() == QEvent::TouchBegin) {
        // Own the sequence: the enclosing SilicaFlickable must not steal
        // drags from the engine (spec 6.1: Servo owns scrolling).
        setKeepTouchGrab(true);
        setKeepMouseGrab(true);
        if (BrowserContext* ctx = BrowserContext::instance())
            ctx->perfLog()->interactionBegin(event->touchPoints().size() > 1 ? QStringLiteral("pinch") : QStringLiteral("scroll"),
                                             m_tab ? m_tab->url() : QUrl());
        if (m_tab) {
            const QSizeF content = m_tab->contentSize();
            const qreal viewportH = Css::deviceToCss(size().toSize(), m_dpr).height();
            const QPointF scroll = m_tab->scrollOffset();
            const bool atTop = scroll.y() <= 0.5;
            const bool atBottom = content.height() > 0 && scroll.y() + viewportH >= content.height() - 0.5;
            m_arbiter->setContentEdges(atTop, atBottom);
        } else {
            m_arbiter->setContentEdges(true, true);
        }
    }
    const QVector<TouchPoint> points = m_converter.convert(event);
    const GestureArbiter::Result r = m_arbiter->process(points);
    if (!r.accepted) {
        setKeepTouchGrab(false);
        setKeepMouseGrab(false);
        event->ignore();
        return;
    }
    forwardTouches(r.forward);
    if (r.handoff) {
        // Let the parent Flickable (pulley menus) take the rest of the drag.
        setKeepTouchGrab(false);
        setKeepMouseGrab(false);
        event->ignore();
        return;
    }
    event->accept();
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel) {
        setKeepTouchGrab(false);
        setKeepMouseGrab(false);
        if (BrowserContext* ctx = BrowserContext::instance())
            ctx->perfLog()->interactionEnd();
    }
}

void TuuliWebView::forwardTouches(const QVector<TouchPoint>& points)
{
    WebViewHandle* h = currentHandle();
    if (!h)
        return;
    for (const TouchPoint& p : points)
        h->touch(p.phase, p.id, p.cssPos);
}

void TuuliWebView::onEngineTouchesCancelled(const QVector<TouchPoint>& cancels)
{
    forwardTouches(cancels);
}

void TuuliWebView::onLongPressed(const QPointF& devicePos, const QPointF& cssPos)
{
    emit longPressed(devicePos.x(), devicePos.y());
    if (WebViewHandle* h = currentHandle())
        h->requestContextMenu(cssPos);
}

void TuuliWebView::keyPressEvent(QKeyEvent* event)
{
    if (WebViewHandle* h = currentHandle()) {
        h->key(true, InputMethodProxy::w3cKeyName(event->key(), event->text()), event->modifiers());
        event->accept();
        return;
    }
    QQuickFramebufferObject::keyPressEvent(event);
}

void TuuliWebView::keyReleaseEvent(QKeyEvent* event)
{
    if (WebViewHandle* h = currentHandle()) {
        h->key(false, InputMethodProxy::w3cKeyName(event->key(), event->text()), event->modifiers());
        event->accept();
        return;
    }
    QQuickFramebufferObject::keyReleaseEvent(event);
}

void TuuliWebView::onImeShow(int type, const QString& text, bool multiline, const QRectF& rect)
{
    m_imeCursorRect = rect;
    m_ime->showFromEngine(static_cast<InputType>(type), text, multiline, rect);
    pushViewport();
}

void TuuliWebView::sendEditingAction(int action)
{
    if (WebViewHandle* h = currentHandle())
        h->editingAction(static_cast<EditingAction>(action));
}

void TuuliWebView::grabThumbnail()
{
    if (!m_tab || !m_tab->handle())
        return;
    const QImage img = m_tab->handle()->capture();
    if (!img.isNull())
        m_tab->setThumbnail(img.scaled(QSize(360, 640), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
}

} // namespace Tuuli
