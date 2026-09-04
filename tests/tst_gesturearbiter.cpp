/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "input/gesturearbiter.h"

#include <QtTest>

using namespace Tuuli;

static TouchPoint tp(int id, TouchPhase phase, qreal x, qreal y, qreal dpr = 2.0)
{
    TouchPoint p;
    p.id = id;
    p.phase = phase;
    p.devicePos = QPointF(x, y);
    p.cssPos = QPointF(x / dpr, y / dpr);
    return p;
}

class tst_GestureArbiter : public QObject
{
    Q_OBJECT

    GestureArbiter::Config config()
    {
        GestureArbiter::Config c;
        c.screenDevice = QSize(1080, 2260);
        c.itemOriginOnScreen = QPointF(0, 0);
        c.sideEdgeMargin = 40;
        c.topEdgeMargin = 40;
        c.bottomEdgeMargin = 48;
        c.longPressMs = 60;
        c.moveSlop = 18;
        c.bottomRevealDistance = 200;
        c.bottomCommitFraction = 0.4;
        return c;
    }

private slots:
    void classifiesZones()
    {
        GestureArbiter a;
        a.setConfig(config());
        QCOMPARE(a.classify(QPointF(10, 1000)), GestureArbiter::Zone::LipstickEdge);
        QCOMPARE(a.classify(QPointF(1075, 1000)), GestureArbiter::Zone::LipstickEdge);
        QCOMPARE(a.classify(QPointF(500, 5)), GestureArbiter::Zone::LipstickEdge);
        QCOMPARE(a.classify(QPointF(500, 2250)), GestureArbiter::Zone::BottomEdge);
        QCOMPARE(a.classify(QPointF(500, 1000)), GestureArbiter::Zone::Content);
    }

    void itemOffsetIsScreenRelative()
    {
        GestureArbiter a;
        GestureArbiter::Config c = config();
        c.itemOriginOnScreen = QPointF(0, 200);
        a.setConfig(c);
        // y=5 inside the item is y=205 on screen: content, not the top edge.
        QCOMPARE(a.classify(QPointF(500, 5)), GestureArbiter::Zone::Content);
    }

    void contentDragGoesToEngine()
    {
        GestureArbiter a;
        a.setConfig(config());
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        QVERIFY(r.accepted);
        QCOMPARE(r.forward.size(), 1);
        QCOMPARE(a.state(), GestureArbiter::State::Engine);
        r = a.process({ tp(1, TouchPhase::Move, 500, 900) });
        QCOMPARE(r.forward.size(), 1);
        r = a.process({ tp(1, TouchPhase::Up, 500, 900) });
        QCOMPARE(r.forward.size(), 1);
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
        QCOMPARE(a.activeTouchCount(), 0);
    }

    void lipstickEdgeIsNeverConsumed()
    {
        GestureArbiter a;
        a.setConfig(config());
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Down, 5, 1000) });
        QVERIFY(!r.accepted);
        QVERIFY(r.forward.isEmpty());
        r = a.process({ tp(1, TouchPhase::Move, 200, 1000) });
        QVERIFY(!r.accepted);
        QVERIFY(r.forward.isEmpty());
        r = a.process({ tp(1, TouchPhase::Up, 200, 1000) });
        QVERIFY(r.forward.isEmpty());
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
    }

    void bottomEdgeRevealsToolbar()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy progress(&a, &GestureArbiter::bottomEdgeProgress);
        QSignalSpy finished(&a, &GestureArbiter::bottomEdgeFinished);
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Down, 500, 2250) });
        QVERIFY(r.accepted);
        QVERIFY(r.forward.isEmpty());
        QCOMPARE(a.state(), GestureArbiter::State::BottomEdge);
        r = a.process({ tp(1, TouchPhase::Move, 500, 2150) });
        QVERIFY(r.forward.isEmpty());
        QCOMPARE(progress.size(), 1);
        QCOMPARE(progress.last().first().toReal(), 0.5);
        a.process({ tp(1, TouchPhase::Up, 500, 2150) });
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.last().first().toBool(), true);
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
    }

    void bottomEdgeShortDragDoesNotCommit()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy finished(&a, &GestureArbiter::bottomEdgeFinished);
        a.process({ tp(1, TouchPhase::Down, 500, 2250) });
        a.process({ tp(1, TouchPhase::Move, 500, 2220) });
        a.process({ tp(1, TouchPhase::Up, 500, 2220) });
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.last().first().toBool(), false);
    }

    void longPressFiresWithoutMovement()
    {
        // Spec 6.2: a hold must trigger without incidental movement.
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy pressed(&a, &GestureArbiter::longPressed);
        QSignalSpy cancelled(&a, &GestureArbiter::engineTouchesCancelled);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        QVERIFY(pressed.wait(500));
        QCOMPARE(pressed.size(), 1);
        QCOMPARE(pressed.first().at(0).toPointF(), QPointF(500, 1000));
        QCOMPARE(pressed.first().at(1).toPointF(), QPointF(250, 500));
        QCOMPARE(cancelled.size(), 1);
        const QVector<TouchPoint> cancels = cancelled.first().first().value<QVector<TouchPoint>>();
        QCOMPARE(cancels.size(), 1);
        QCOMPARE(cancels.first().phase, TouchPhase::Cancel);
        QCOMPARE(a.state(), GestureArbiter::State::LongPressed);
        // The rest of the sequence stays with Tuuli.
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Move, 520, 1000) });
        QVERIFY(r.forward.isEmpty());
        r = a.process({ tp(1, TouchPhase::Up, 520, 1000) });
        QVERIFY(r.forward.isEmpty());
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
    }

    void longPressSurvivesJitterInsideSlop()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy pressed(&a, &GestureArbiter::longPressed);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        a.process({ tp(1, TouchPhase::Move, 505, 1004) });
        QVERIFY(pressed.wait(500));
        QCOMPARE(pressed.size(), 1);
    }

    void dragCancelsLongPress()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy pressed(&a, &GestureArbiter::longPressed);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        a.process({ tp(1, TouchPhase::Move, 500, 900) });
        QVERIFY(!pressed.wait(150));
        QCOMPARE(a.state(), GestureArbiter::State::Engine);
    }

    void secondFingerCancelsLongPress()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy pressed(&a, &GestureArbiter::longPressed);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        GestureArbiter::Result r = a.process({ tp(2, TouchPhase::Down, 600, 1100) });
        QCOMPARE(r.forward.size(), 1); // pinch goes to the engine
        QVERIFY(!pressed.wait(150));
        QCOMPARE(a.activeTouchCount(), 2);
    }

    void releaseBeforeTimeoutIsATap()
    {
        GestureArbiter a;
        a.setConfig(config());
        QSignalSpy pressed(&a, &GestureArbiter::longPressed);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Up, 500, 1000) });
        QCOMPARE(r.forward.size(), 1);
        QVERIFY(!pressed.wait(150));
    }

    void verticalDragAtTopHandsOffToParent()
    {
        // Spec 7.2: pulley menus open from the content edge.
        GestureArbiter a;
        a.setConfig(config());
        a.setContentEdges(true, false);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Move, 502, 1060) });
        QVERIFY(r.handoff);
        QVERIFY(r.accepted);
        QCOMPARE(r.forward.size(), 1);
        QCOMPARE(r.forward.first().phase, TouchPhase::Cancel);
        QCOMPARE(a.state(), GestureArbiter::State::HandedOff);
        r = a.process({ tp(1, TouchPhase::Move, 502, 1200) });
        QVERIFY(r.forward.isEmpty());
        a.process({ tp(1, TouchPhase::Up, 502, 1200) });
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
    }

    void dragIntoContentStaysWithEngine()
    {
        GestureArbiter a;
        a.setConfig(config());
        a.setContentEdges(true, false);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        // Upward drag at the top scrolls the page: engine keeps it.
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Move, 500, 940) });
        QVERIFY(!r.handoff);
        QCOMPARE(r.forward.first().phase, TouchPhase::Move);
        // Not at the bottom, so continuing up never hands off either.
        r = a.process({ tp(1, TouchPhase::Move, 500, 400) });
        QVERIFY(!r.handoff);
        // A diagonal drag at the top is a pan, not a pulley pull.
        GestureArbiter b;
        b.setConfig(config());
        b.setContentEdges(true, false);
        b.process({ tp(1, TouchPhase::Down, 500, 1000) });
        r = b.process({ tp(1, TouchPhase::Move, 580, 1050) });
        QVERIFY(!r.handoff);
    }

    void handoffCanBeDisabled()
    {
        GestureArbiter a;
        a.setConfig(config());
        a.setContentEdges(true, true);
        a.setParentHandoffEnabled(false);
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Move, 500, 1100) });
        QVERIFY(!r.handoff);
        QCOMPARE(a.state(), GestureArbiter::State::Engine);
    }

    void strayMoveWithoutDownIsIgnored()
    {
        GestureArbiter a;
        a.setConfig(config());
        GestureArbiter::Result r = a.process({ tp(1, TouchPhase::Move, 500, 1000) });
        QVERIFY(!r.accepted);
        QVERIFY(r.forward.isEmpty());
    }

    void resetClearsState()
    {
        GestureArbiter a;
        a.setConfig(config());
        a.process({ tp(1, TouchPhase::Down, 500, 1000) });
        a.reset();
        QCOMPARE(a.state(), GestureArbiter::State::Idle);
        QCOMPARE(a.activeTouchCount(), 0);
    }
};

QTEST_GUILESS_MAIN(tst_GestureArbiter)
#include "tst_gesturearbiter.moc"
