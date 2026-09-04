/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_TOUCHCONVERTER_H
#define TUULI_TOUCHCONVERTER_H

#include "engine/engine.h"

#include <QEvent>
#include <QPointF>
#include <QTouchEvent>
#include <QVector>

namespace Tuuli {

/* One engine-bound touch point (spec 6.1).  cssPos is what the engine gets;
 * devicePos is kept for the gesture arbiter, which reasons in device px
 * about screen-edge margins. */
struct TouchPoint {
    int id = 0;
    TouchPhase phase = TouchPhase::Down;
    QPointF devicePos;   // relative to the webview item, device px
    QPointF cssPos;      // relative to the webview item, CSS px
    qint64 timestamp = 0;
};

/* Converts Qt touch events to engine touch points. Stationary points are
 * dropped (the engine tracks them by id); a TouchCancel cancels every point
 * regardless of its individual state. */
class TouchConverter
{
public:
    explicit TouchConverter(qreal dpr = 1.0) : m_dpr(dpr) {}

    void setDevicePixelRatio(qreal dpr) { m_dpr = dpr; }
    qreal devicePixelRatio() const { return m_dpr; }

    /* Item-space offset subtracted from every point before conversion, e.g.
     * when the webview item is inset from the surface origin. */
    void setOrigin(const QPointF& deviceOrigin) { m_origin = deviceOrigin; }
    QPointF origin() const { return m_origin; }

    QVector<TouchPoint> convert(const QTouchEvent* event) const;
    QVector<TouchPoint> convert(QEvent::Type type, const QList<QTouchEvent::TouchPoint>& points,
                                qint64 timestamp) const;

    static bool phaseFor(QEvent::Type type, Qt::TouchPointState state, TouchPhase* out);

private:
    qreal m_dpr;
    QPointF m_origin;
};

} // namespace Tuuli

Q_DECLARE_METATYPE(Tuuli::TouchPoint)
Q_DECLARE_METATYPE(QVector<Tuuli::TouchPoint>)

#endif
