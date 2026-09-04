/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_WEBVIEWRENDERER_H
#define TUULI_WEBVIEWRENDERER_H

/*
 * QQuickFramebufferObject::Renderer for TuuliWebView (spec 5.1).  Runs on
 * the Qt render thread.  The first render() initialises the engine (its
 * WebRender needs a current context); every render() paints the current
 * webview into the FBO Qt bound for us; destruction (scene-graph
 * invalidation, spec 5.2) tears the engine down rather than leaking GL.
 */

#include "engine/engine.h"

#include <QColor>
#include <QElapsedTimer>
#include <QQuickFramebufferObject>
#include <QSize>

namespace Tuuli {

class QtRenderingContext;
class TuuliWebView;

class WebViewRenderer : public QQuickFramebufferObject::Renderer
{
public:
    explicit WebViewRenderer(TuuliWebView* item);
    ~WebViewRenderer();

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override;
    void synchronize(QQuickFramebufferObject* item) override;
    void render() override;

private:
    TuuliWebView* m_item;
    Engine* m_engine = nullptr;
    WebViewHandle* m_handle = nullptr;
    QtRenderingContext* m_context = nullptr;
    QColor m_placeholder;
    bool m_initFailed = false;
    QElapsedTimer m_frameTimer;
};

} // namespace Tuuli

#endif
