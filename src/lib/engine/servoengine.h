/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_SERVOENGINE_H
#define TUULI_SERVOENGINE_H

/*
 * Tuuli::Engine over servo_capi (spec 3.3, 4.2).  The only translation
 * unit besides servowebview.cpp that includes servo_capi.h.
 *
 * Thread notes:
 *  - initializeOnRenderThread()/shutdownOnRenderThread() and paint() run
 *    on the Qt render thread with the scene-graph context current.
 *  - Everything else runs on the GUI thread.  Servo's wake_up callback
 *    arrives on arbitrary threads and only posts a QEvent to this object;
 *    the event handler spins Servo's loop on the GUI thread.
 *  - All Servo callbacks are marshalled through Qt::QueuedConnection.
 */

#include "engine.h"

#include <QAtomicInt>
#include <QByteArray>
#include <QEvent>
#include <QVector>

struct ServoInstance;
struct ServoRenderingContext;
struct ServoRenderingContextVTable;

namespace Tuuli {

class ServoWebView;

class ServoEngine : public Engine
{
    Q_OBJECT
public:
    explicit ServoEngine(QObject* parent = nullptr);
    ~ServoEngine();

    QString name() const override { return QStringLiteral("servo"); }
    QString versionString() const override;
    void configure(const EngineConfig& config) override;
    EngineConfig config() const override { return m_config; }
    bool initializeOnRenderThread(RenderingContext* ctx) override;
    void shutdownOnRenderThread() override;
    bool isInitialized() const override { return m_initialized.load() != 0; }
    WebViewHandle* createWebView(WebViewClient* client, bool isPrivate, qreal dpr, const QSize& devicePx) override;
    void spinEventLoop() override;
    void setPref(const QString& name, const QString& value) override;
    void setProxy(const ProxyConfig& proxy) override;
    void clearSiteData(const QString& origin, unsigned kinds) override;

    ServoInstance* instance() const { return m_servo; }
    ServoRenderingContext* renderingContext() const { return m_ctx; }

    /* A webview Servo created on its own (window.open).  The receiver
     * adopts it into a tab with Tab::attachWebView(). */
    void adoptAuxiliaryWebView(ServoWebView* wv);

signals:
    void auxiliaryWebViewCreated(Tuuli::WebViewHandle* handle);

protected:
    bool event(QEvent* e) override;

private:
    struct Callbacks;
    friend struct Callbacks;

    EngineConfig m_config;
    ServoInstance* m_servo = nullptr;
    ServoRenderingContext* m_ctx = nullptr;
    RenderingContext* m_qtCtx = nullptr;
    QAtomicInt m_initialized;
    QAtomicInt m_wakePending;
    QByteArray m_clipboardScratch;
    QVector<QByteArray> m_configStrings;
    QVector<const char*> m_prefPointers;
};

extern const QEvent::Type ServoWakeEventType;

} // namespace Tuuli

#endif
