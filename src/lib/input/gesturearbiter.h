/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_GESTUREARBITER_H
#define TUULI_GESTUREARBITER_H

/*
 * Decides who owns a touch sequence (spec 6.2):
 *
 *   swipe from left/right/top screen edge  -> lipstick, never consumed
 *   swipe from bottom edge                 -> Tuuli, toolbar reveal
 *   single-finger drag / pinch / double-tap -> engine
 *   long-press (hold, no movement needed)  -> Tuuli context menu; the engine
 *                                             receives a touch cancel so it
 *                                             does not also scroll or click
 *   vertical drag past the content edge    -> handed to the parent Flickable
 *                                             so Silica pulley menus open
 *
 * The arbiter never implements a kinetic scroller; anything it forwards is
 * handed verbatim to the engine's own async touch pipeline (spec 6.1).
 */

#include "touchconverter.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QSize>
#include <QTimer>
#include <QVector>

namespace Tuuli {

class GestureArbiter : public QObject
{
    Q_OBJECT
public:
    struct Config {
        QSize screenDevice;           // whole screen, device px
        QPointF itemOriginOnScreen;   // where the webview item sits on it
        int sideEdgeMargin = 40;      // lipstick left/right edge zone
        int topEdgeMargin = 40;       // lipstick top-menu zone
        int bottomEdgeMargin = 48;    // Tuuli toolbar-reveal zone
        int longPressMs = 500;
        qreal moveSlop = 18.0;        // device px before a hold becomes a drag
        qreal bottomRevealDistance = 200.0;
        qreal bottomCommitFraction = 0.4;
    };

    enum class State { Idle, Engine, LipstickEdge, BottomEdge, LongPressed, HandedOff };
    enum class Zone { Content, LipstickEdge, BottomEdge };

    struct Result {
        QVector<TouchPoint> forward;  // points for the engine
        bool accepted = true;         // false => event->ignore(), lipstick wins
        bool handoff = false;         // release the grab: parent Flickable takes over
    };

    explicit GestureArbiter(QObject* parent = nullptr);

    void setConfig(const Config& config);
    Config config() const { return m_config; }

    Result process(const QVector<TouchPoint>& points);

    /* Whether the engine content is scrolled to its top/bottom edge; set by
     * the view before each event.  A vertical drag away from an edge the
     * content cannot scroll past is handed to the parent (pulley menus). */
    void setContentEdges(bool atTop, bool atBottom) { m_atTop = atTop; m_atBottom = atBottom; }
    void setParentHandoffEnabled(bool on) { m_handoffEnabled = on; }

    State state() const { return m_state; }
    int activeTouchCount() const { return m_active.size(); }

    /* Cancel points for every active touch; sent to the engine after a
     * long-press fires so it forgets the sequence. */
    QVector<TouchPoint> cancelPoints() const;

    Zone classify(const QPointF& devicePosInItem) const;

    void reset();

signals:
    void longPressed(const QPointF& devicePos, const QPointF& cssPos);
    void bottomEdgeProgress(qreal progress);
    void bottomEdgeFinished(bool committed);
    /* Emitted right after longPressed with the engine cancel points. */
    void engineTouchesCancelled(const QVector<Tuuli::TouchPoint>& cancels);

private:
    struct Active {
        QPointF startDevice;
        QPointF currentDevice;
        QPointF currentCss;
        bool moved = false;
    };

    void onLongPressTimeout();
    void finishSequenceIfIdle();
    bool anyMoved() const;

    Config m_config;
    State m_state = State::Idle;
    QHash<int, Active> m_active;
    bool m_atTop = true;
    bool m_atBottom = false;
    bool m_handoffEnabled = true;
    int m_primaryId = -1;
    qreal m_bottomProgress = 0;
    QTimer m_longPress;
};

} // namespace Tuuli

#endif
