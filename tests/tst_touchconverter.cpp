/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "input/touchconverter.h"

#include <QtTest>

using namespace Tuuli;

static QTouchEvent::TouchPoint makePoint(int id, Qt::TouchPointState state, const QPointF& pos)
{
    QTouchEvent::TouchPoint p(id);
    p.setState(state);
    p.setPos(pos);
    return p;
}

class tst_TouchConverter : public QObject
{
    Q_OBJECT
private slots:
    void phaseMapping()
    {
        TouchPhase phase;
        QVERIFY(TouchConverter::phaseFor(QEvent::TouchBegin, Qt::TouchPointPressed, &phase));
        QCOMPARE(phase, TouchPhase::Down);
        QVERIFY(TouchConverter::phaseFor(QEvent::TouchUpdate, Qt::TouchPointMoved, &phase));
        QCOMPARE(phase, TouchPhase::Move);
        QVERIFY(TouchConverter::phaseFor(QEvent::TouchEnd, Qt::TouchPointReleased, &phase));
        QCOMPARE(phase, TouchPhase::Up);
        QVERIFY(!TouchConverter::phaseFor(QEvent::TouchUpdate, Qt::TouchPointStationary, &phase));
        // Cancel overrides every per-point state.
        QVERIFY(TouchConverter::phaseFor(QEvent::TouchCancel, Qt::TouchPointStationary, &phase));
        QCOMPARE(phase, TouchPhase::Cancel);
    }

    void convertsToCssPixels()
    {
        TouchConverter c(2.0);
        QList<QTouchEvent::TouchPoint> pts;
        pts << makePoint(7, Qt::TouchPointPressed, QPointF(540, 1130));
        const QVector<TouchPoint> out = c.convert(QEvent::TouchBegin, pts, 1234);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().id, 7);
        QCOMPARE(out.first().phase, TouchPhase::Down);
        QCOMPARE(out.first().devicePos, QPointF(540, 1130));
        QCOMPARE(out.first().cssPos, QPointF(270, 565));
        QCOMPARE(out.first().timestamp, qint64(1234));
    }

    void dropsStationaryPoints()
    {
        TouchConverter c(1.0);
        QList<QTouchEvent::TouchPoint> pts;
        pts << makePoint(1, Qt::TouchPointStationary, QPointF(1, 1));
        pts << makePoint(2, Qt::TouchPointMoved, QPointF(5, 5));
        const QVector<TouchPoint> out = c.convert(QEvent::TouchUpdate, pts, 0);
        QCOMPARE(out.size(), 1);
        QCOMPARE(out.first().id, 2);
    }

    void cancelCancelsEverything()
    {
        TouchConverter c(1.0);
        QList<QTouchEvent::TouchPoint> pts;
        pts << makePoint(1, Qt::TouchPointStationary, QPointF(1, 1));
        pts << makePoint(2, Qt::TouchPointMoved, QPointF(5, 5));
        const QVector<TouchPoint> out = c.convert(QEvent::TouchCancel, pts, 0);
        QCOMPARE(out.size(), 2);
        QCOMPARE(out.at(0).phase, TouchPhase::Cancel);
        QCOMPARE(out.at(1).phase, TouchPhase::Cancel);
    }

    void originOffsetApplies()
    {
        TouchConverter c(2.0);
        c.setOrigin(QPointF(0, 100));
        QList<QTouchEvent::TouchPoint> pts;
        pts << makePoint(1, Qt::TouchPointPressed, QPointF(10, 300));
        const QVector<TouchPoint> out = c.convert(QEvent::TouchBegin, pts, 0);
        QCOMPARE(out.first().devicePos, QPointF(10, 200));
        QCOMPARE(out.first().cssPos, QPointF(5, 100));
    }

    void dprChangeAffectsSubsequentEvents()
    {
        TouchConverter c(1.0);
        QList<QTouchEvent::TouchPoint> pts;
        pts << makePoint(1, Qt::TouchPointPressed, QPointF(100, 100));
        QCOMPARE(c.convert(QEvent::TouchBegin, pts, 0).first().cssPos, QPointF(100, 100));
        c.setDevicePixelRatio(2.5);
        QCOMPARE(c.convert(QEvent::TouchBegin, pts, 0).first().cssPos, QPointF(40, 40));
    }
};

QTEST_MAIN(tst_TouchConverter)
#include "tst_touchconverter.moc"
