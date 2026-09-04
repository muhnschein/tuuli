/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_QTRENDERINGCONTEXT_H
#define TUULI_QTRENDERINGCONTEXT_H

/*
 * Tuuli::RenderingContext over the scene-graph QOpenGLContext (spec 5.2).
 * Servo never owns a context: make_current is a check, swap is a no-op,
 * and the framebuffer is whatever QQuickFramebufferObject bound for us.
 * Lives and dies on the render thread with the renderer.
 */

#include "engine/engine.h"

#include <QOpenGLContext>
#include <QSize>
#include <QSurfaceFormat>

namespace Tuuli {

class QtRenderingContext : public RenderingContext
{
public:
    explicit QtRenderingContext(QOpenGLContext* context);

    void setFramebuffer(unsigned fbo, const QSize& size);

    QSize size() const override { return m_size; }
    unsigned framebufferObject() const override { return m_fbo; }
    void* procAddress(const char* name) override;
    bool makeCurrent() override;
    int glMajorVersion() const override { return m_major; }
    int glMinorVersion() const override { return m_minor; }
    bool isGles() const override { return m_gles; }

    QOpenGLContext* context() const { return m_context; }

private:
    QOpenGLContext* m_context;
    unsigned m_fbo = 0;
    QSize m_size;
    int m_major = 3;
    int m_minor = 2;
    bool m_gles = true;
};

} // namespace Tuuli

#endif
