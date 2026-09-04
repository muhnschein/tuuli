/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TUULIWEBVIEW_H
#define TUULI_TUULIWEBVIEW_H

/*
 * The QML `WebView` item (spec 4, 5.1, 6).  One item shows the current
 * tab's engine webview; switching tabs swaps the handle the renderer
 * paints.  Touch goes Qt -> TouchConverter -> GestureArbiter -> engine.
 */

#include "engine/engine.h"
#include "input/gesturearbiter.h"
#include "input/inputmethodproxy.h"
#include "input/touchconverter.h"
#include "model/tab.h"

#include <QColor>
#include <QPointer>
#include <QQuickFramebufferObject>

namespace Tuuli {

class TuuliWebView : public QQuickFramebufferObject
{
    Q_OBJECT
    Q_PROPERTY(Tuuli::Tab* tab READ tab WRITE setTab NOTIFY tabChanged)
    Q_PROPERTY(qreal contentDevicePixelRatio READ contentDevicePixelRatio NOTIFY contentDevicePixelRatioChanged)
    Q_PROPERTY(qreal devicePixelRatioOverride READ devicePixelRatioOverride WRITE setDevicePixelRatioOverride NOTIFY contentDevicePixelRatioChanged)
    Q_PROPERTY(int bottomInset READ bottomInset WRITE setBottomInset NOTIFY insetsChanged)
    Q_PROPERTY(int topInset READ topInset WRITE setTopInset NOTIFY insetsChanged)
    Q_PROPERTY(bool engineReady READ engineReady NOTIFY engineReadyChanged)
    Q_PROPERTY(bool engineFailed READ engineFailed NOTIFY engineReadyChanged)
    Q_PROPERTY(QString engineName READ engineName CONSTANT)
    Q_PROPERTY(Tuuli::InputMethodProxy* inputMethod READ inputMethod CONSTANT)
    Q_PROPERTY(int longPressDuration READ longPressDuration WRITE setLongPressDuration NOTIFY gestureConfigChanged)
    Q_PROPERTY(int edgeMargin READ edgeMargin WRITE setEdgeMargin NOTIFY gestureConfigChanged)
    Q_PROPERTY(int bottomEdgeMargin READ bottomEdgeMargin WRITE setBottomEdgeMargin NOTIFY gestureConfigChanged)
    Q_PROPERTY(QColor placeholderColor READ placeholderColor WRITE setPlaceholderColor NOTIFY placeholderColorChanged)
    Q_PROPERTY(qreal lastFrameMs READ lastFrameMs NOTIFY frameStatsChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameStatsChanged)

public:
    explicit TuuliWebView(QQuickItem* parent = nullptr);
    ~TuuliWebView();

    Renderer* createRenderer() const override;

    Tab* tab() const { return m_tab; }
    void setTab(Tab* tab);
    qreal contentDevicePixelRatio() const { return m_dpr; }
    qreal devicePixelRatioOverride() const { return m_dprOverride; }
    void setDevicePixelRatioOverride(qreal dpr);
    int bottomInset() const { return m_bottomInset; }
    void setBottomInset(int px);
    int topInset() const { return m_topInset; }
    void setTopInset(int px);
    bool engineReady() const;
    bool engineFailed() const { return m_engineFailed; }
    QString engineName() const;
    InputMethodProxy* inputMethod() const { return m_ime; }
    int longPressDuration() const { return m_gestureConfig.longPressMs; }
    void setLongPressDuration(int ms);
    int edgeMargin() const { return m_gestureConfig.sideEdgeMargin; }
    void setEdgeMargin(int px);
    int bottomEdgeMargin() const { return m_gestureConfig.bottomEdgeMargin; }
    void setBottomEdgeMargin(int px);
    QColor placeholderColor() const { return m_placeholder; }
    void setPlaceholderColor(const QColor& c);
    qreal lastFrameMs() const { return m_lastFrameMs; }
    int frameCount() const { return m_frameCount; }

    /* Renderer access (render thread during synchronize / GUI-blocked). */
    Engine* engine() const { return m_engine; }
    WebViewHandle* currentHandle() const;
    void syncFrameStats(qint64 lastFrameMs);
    void reportEngineInitFailure();

    Q_INVOKABLE QPointF cssToItem(const QPointF& css) const;
    Q_INVOKABLE QPointF itemToCss(const QPointF& item) const;
    Q_INVOKABLE void grabThumbnail();
    Q_INVOKABLE void sendEditingAction(int action);

signals:
    void tabChanged();
    void contentDevicePixelRatioChanged();
    void insetsChanged();
    void engineReadyChanged();
    void gestureConfigChanged();
    void placeholderColorChanged();
    void frameStatsChanged();
    void longPressed(qreal x, qreal y);
    void contextMenuRequested(qreal x, qreal y, const QUrl& linkUrl, const QUrl& imageUrl,
                              const QString& selectedText, bool editable);
    void bottomEdgeProgress(qreal progress);
    void bottomEdgeFinished(bool committed);
    void engineInitFailed();

protected:
    void touchEvent(QTouchEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void geometryChanged(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void attachTab(Tab* tab);
    void detachTab(Tab* tab);
    void resolveDevicePixelRatio();
    void pushGeometry();
    void pushViewport();
    void updateArbiterConfig();
    void forwardTouches(const QVector<TouchPoint>& points);
    void onFrameReady();
    void onLongPressed(const QPointF& devicePos, const QPointF& cssPos);
    void onEngineTouchesCancelled(const QVector<TouchPoint>& cancels);
    void onImeShow(int type, const QString& text, bool multiline, const QRectF& rect);
    void onEngineInitialized();

    Engine* m_engine = nullptr;
    QPointer<Tab> m_tab;
    InputMethodProxy* m_ime;
    TouchConverter m_converter;
    GestureArbiter* m_arbiter;
    GestureArbiter::Config m_gestureConfig;
    qreal m_dpr = 1.0;
    qreal m_dprOverride = 0.0;
    int m_bottomInset = 0;
    int m_topInset = 0;
    QColor m_placeholder = QColor(0x1a, 0x1a, 0x1a);
    qreal m_lastFrameMs = 0;
    int m_frameCount = 0;
    bool m_engineFailed = false;
    QRectF m_imeCursorRect;
};

} // namespace Tuuli

#endif
