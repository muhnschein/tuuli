/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#ifndef TUULI_CSSGEOMETRY_H
#define TUULI_CSSGEOMETRY_H

/*
 * Device-pixel <-> CSS-pixel maths (spec 6.1) and the viewport layout used
 * to keep a focused element visible above the virtual keyboard (spec 6.3).
 * Pure functions; covered by tests/tst_cssgeometry.cpp.
 */

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QtGlobal>
#include <cmath>

namespace Tuuli {
namespace Css {

inline qreal sanitizeDpr(qreal dpr)
{
    return (dpr > 0.0 && std::isfinite(dpr)) ? dpr : 1.0;
}

inline QPointF deviceToCss(const QPointF& device, qreal dpr)
{
    dpr = sanitizeDpr(dpr);
    return QPointF(device.x() / dpr, device.y() / dpr);
}

inline QPointF cssToDevice(const QPointF& css, qreal dpr)
{
    dpr = sanitizeDpr(dpr);
    return QPointF(css.x() * dpr, css.y() * dpr);
}

inline QSizeF deviceToCss(const QSize& device, qreal dpr)
{
    dpr = sanitizeDpr(dpr);
    return QSizeF(device.width() / dpr, device.height() / dpr);
}

inline QRectF deviceToCss(const QRect& device, qreal dpr)
{
    dpr = sanitizeDpr(dpr);
    return QRectF(device.x() / dpr, device.y() / dpr, device.width() / dpr, device.height() / dpr);
}

inline QRect cssToDevice(const QRectF& css, qreal dpr)
{
    dpr = sanitizeDpr(dpr);
    return QRect(qRound(css.x() * dpr), qRound(css.y() * dpr),
                 qRound(css.width() * dpr), qRound(css.height() * dpr));
}

/* Derive a content DPR for a panel (spec 6.1: "do not hardcode").
 * Qt reports 1.0 on Sailfish (Silica scales via Theme.pixelRatio, not the
 * QScreen DPR), so when Qt gives us 1.0 we fall back to the Android density
 * convention (ppi / 160) rounded to the nearest 0.25, clamped to [1, 4]. */
inline qreal deriveDevicePixelRatio(qreal qtDpr, qreal physicalDpi)
{
    if (qtDpr > 1.0 && std::isfinite(qtDpr))
        return qtDpr;
    if (!(physicalDpi > 0.0) || !std::isfinite(physicalDpi))
        return 1.0;
    qreal density = physicalDpi / 160.0;
    density = std::round(density * 4.0) / 4.0;
    return qBound<qreal>(1.0, density, 4.0);
}

struct ViewportLayout {
    QRect visibleDevice;   // part of the surface not covered by keyboard / chrome
    QRectF visibleCss;
    bool obscured = false; // true when the keyboard or chrome eats part of it
};

/* Surface is the full FBO.  bottomInsetDevice is the height of whatever
 * covers the bottom of the surface (VKB plus any chrome that overlaps
 * content).  The surface itself is never resized for the keyboard (spec
 * 6.3); only the viewport rect handed to the engine changes. */
inline ViewportLayout layoutViewport(const QSize& surfaceDevice, int bottomInsetDevice,
                                     int topInsetDevice, qreal dpr)
{
    ViewportLayout out;
    const int w = qMax(0, surfaceDevice.width());
    const int h = qMax(0, surfaceDevice.height());
    const int top = qBound(0, topInsetDevice, h);
    const int bottom = qBound(0, bottomInsetDevice, h - top);
    out.visibleDevice = QRect(0, top, w, h - top - bottom);
    out.visibleCss = deviceToCss(out.visibleDevice, dpr);
    out.obscured = (top > 0 || bottom > 0);
    return out;
}

/* Scroll delta (CSS px, positive = scroll down/right) required to bring
 * `element` (viewport-relative CSS rect) inside `visible` with `margin`
 * around it.  Zero if it is already visible. Elements taller than the
 * visible area are aligned to its top edge. */
inline QPointF scrollDeltaToReveal(const QRectF& element, const QRectF& visible, qreal margin)
{
    QPointF delta(0, 0);
    if (visible.isEmpty())
        return delta;
    const QRectF target = element.adjusted(-margin, -margin, margin, margin);

    if (target.height() >= visible.height()) {
        delta.setY(target.top() - visible.top());
    } else if (target.bottom() > visible.bottom()) {
        delta.setY(target.bottom() - visible.bottom());
    } else if (target.top() < visible.top()) {
        delta.setY(target.top() - visible.top());
    }

    if (target.width() >= visible.width()) {
        delta.setX(target.left() - visible.left());
    } else if (target.right() > visible.right()) {
        delta.setX(target.right() - visible.right());
    } else if (target.left() < visible.left()) {
        delta.setX(target.left() - visible.left());
    }
    return delta;
}

} // namespace Css
} // namespace Tuuli

#endif
