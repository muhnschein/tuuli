/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "qtrenderingcontext.h"

namespace Tuuli {

QtRenderingContext::QtRenderingContext(QOpenGLContext* context)
    : m_context(context)
{
    if (m_context) {
        const QSurfaceFormat f = m_context->format();
        m_major = f.majorVersion();
        m_minor = f.minorVersion();
        m_gles = m_context->isOpenGLES();
        // Spec 5.2: report GLES 3.2 so WebRender takes its modern path.  If
        // the driver reports lower than 3.0 we keep what it says; lying
        // about that would only move the failure into WebRender.
        if (m_gles && m_major >= 3 && (m_major > 3 || m_minor < 2)) {
            m_major = 3;
            m_minor = 2;
        }
    }
}

void QtRenderingContext::setFramebuffer(unsigned fbo, const QSize& size)
{
    m_fbo = fbo;
    m_size = size;
}

void* QtRenderingContext::procAddress(const char* name)
{
    if (!m_context || !name)
        return nullptr;
    return reinterpret_cast<void*>(m_context->getProcAddress(QByteArray(name)));
}

bool QtRenderingContext::makeCurrent()
{
    // Qt made the context current on this thread before render(); anything
    // else is a threading bug we want to hear about, not paper over.
    return m_context && QOpenGLContext::currentContext() == m_context;
}

} // namespace Tuuli
