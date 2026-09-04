/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "webviewrenderer.h"
#include "qtrenderingcontext.h"
#include "tuuliwebview.h"

#include <QMutexLocker>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFramebufferObjectFormat>
#include <QOpenGLFunctions>
#include <QQuickWindow>

namespace Tuuli {

WebViewRenderer::WebViewRenderer(TuuliWebView* item)
    : m_item(item)
{
}

WebViewRenderer::~WebViewRenderer()
{
    // Render thread, context current: the scene graph is being invalidated
    // or the item destroyed.  Tear Servo's GL state down with it.
    if (m_engine && m_engine->isInitialized())
        m_engine->shutdownOnRenderThread();
    delete m_context;
}

QOpenGLFramebufferObject* WebViewRenderer::createFramebufferObject(const QSize& size)
{
    QOpenGLFramebufferObjectFormat format;
    // WebRender needs depth; stencil comes along for free on GLES.
    format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    format.setSamples(0);
    return new QOpenGLFramebufferObject(size, format);
}

void WebViewRenderer::synchronize(QQuickFramebufferObject* item)
{
    // GUI thread is blocked here; safe to read the item.
    TuuliWebView* view = static_cast<TuuliWebView*>(item);
    m_engine = view->engine();
    m_handle = view->currentHandle();
    m_placeholder = view->placeholderColor();
    view->syncFrameStats(m_frameTimer.isValid() ? m_frameTimer.elapsed() : 0);
}

void WebViewRenderer::render()
{
    m_frameTimer.restart();
    QOpenGLFramebufferObject* fbo = framebufferObject();
    QOpenGLContext* gl = QOpenGLContext::currentContext();
    if (!gl || !fbo)
        return;

    if (!m_context)
        m_context = new QtRenderingContext(gl);
    m_context->setFramebuffer(fbo->handle(), fbo->size());

    bool painted = false;
    if (m_engine && !m_initFailed) {
        if (!m_engine->isInitialized()) {
            // Spec 5.3 / M0: WebRender's shaders compile here, on the render
            // thread, on the hybris driver.  If this fails there is nothing
            // to retry; the item shows the placeholder and reports it.
            if (!m_engine->initializeOnRenderThread(m_context)) {
                m_initFailed = true;
                m_item->reportEngineInitFailure();
            }
        }
        if (m_handle && m_engine->isInitialized()) {
            QMutexLocker lock(m_engine->renderLock());
            painted = m_handle->paint();
        }
    }

    if (!painted) {
        QOpenGLFunctions* f = gl->functions();
        f->glBindFramebuffer(GL_FRAMEBUFFER, fbo->handle());
        f->glClearColor(m_placeholder.redF(), m_placeholder.greenF(), m_placeholder.blueF(), 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    // Servo left GL in an unknown state; hand Qt back a clean one.
    if (m_item->window())
        m_item->window()->resetOpenGLState();
}

} // namespace Tuuli
