/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "touchconverter.h"
#include "cssgeometry.h"

namespace Tuuli {

bool TouchConverter::phaseFor(QEvent::Type type, Qt::TouchPointState state, TouchPhase* out)
{
    if (type == QEvent::TouchCancel) {
        *out = TouchPhase::Cancel;
        return true;
    }
    switch (state) {
    case Qt::TouchPointPressed: *out = TouchPhase::Down; return true;
    case Qt::TouchPointMoved: *out = TouchPhase::Move; return true;
    case Qt::TouchPointReleased: *out = TouchPhase::Up; return true;
    case Qt::TouchPointStationary:
    default:
        return false;
    }
}

QVector<TouchPoint> TouchConverter::convert(const QTouchEvent* event) const
{
    return convert(event->type(), event->touchPoints(), static_cast<qint64>(event->timestamp()));
}

QVector<TouchPoint> TouchConverter::convert(QEvent::Type type,
                                            const QList<QTouchEvent::TouchPoint>& points,
                                            qint64 timestamp) const
{
    QVector<TouchPoint> out;
    out.reserve(points.size());
    for (const QTouchEvent::TouchPoint& qp : points) {
        TouchPhase phase;
        if (!phaseFor(type, qp.state(), &phase))
            continue;
        TouchPoint tp;
        tp.id = qp.id();
        tp.phase = phase;
        tp.devicePos = qp.pos() - m_origin;
        tp.cssPos = Css::deviceToCss(tp.devicePos, m_dpr);
        tp.timestamp = timestamp;
        out.append(tp);
    }
    return out;
}

} // namespace Tuuli
