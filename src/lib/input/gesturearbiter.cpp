/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */
#include "gesturearbiter.h"

#include <QtMath>

namespace Tuuli {

GestureArbiter::GestureArbiter(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<Tuuli::TouchPoint>>("QVector<Tuuli::TouchPoint>");
    qRegisterMetaType<QVector<Tuuli::TouchPoint>>("QVector<TouchPoint>");
    m_longPress.setSingleShot(true);
    connect(&m_longPress, &QTimer::timeout, this, &GestureArbiter::onLongPressTimeout);
}

void GestureArbiter::setConfig(const Config& config)
{
    m_config = config;
}

void GestureArbiter::reset()
{
    m_longPress.stop();
    m_active.clear();
    m_state = State::Idle;
    m_primaryId = -1;
    m_bottomProgress = 0;
}

GestureArbiter::Zone GestureArbiter::classify(const QPointF& devicePosInItem) const
{
    const QPointF screen = devicePosInItem + m_config.itemOriginOnScreen;
    const QSize s = m_config.screenDevice;
    if (!s.isValid() || s.isEmpty())
        return Zone::Content;
    if (screen.x() < m_config.sideEdgeMargin || screen.x() >= s.width() - m_config.sideEdgeMargin)
        return Zone::LipstickEdge;
    if (screen.y() < m_config.topEdgeMargin)
        return Zone::LipstickEdge;
    if (screen.y() >= s.height() - m_config.bottomEdgeMargin)
        return Zone::BottomEdge;
    return Zone::Content;
}

bool GestureArbiter::anyMoved() const
{
    for (auto it = m_active.constBegin(); it != m_active.constEnd(); ++it)
        if (it.value().moved)
            return true;
    return false;
}

GestureArbiter::Result GestureArbiter::process(const QVector<TouchPoint>& points)
{
    Result result;

    if (m_state == State::Idle) {
        int downIndex = -1;
        for (int i = 0; i < points.size(); ++i) {
            if (points.at(i).phase == TouchPhase::Down) { downIndex = i; break; }
        }
        if (downIndex < 0) {
            // Stray move/up without a down (e.g. after reset): swallow.
            result.accepted = false;
            return result;
        }
        const TouchPoint& first = points.at(downIndex);
        m_primaryId = first.id;
        m_bottomProgress = 0;
        switch (classify(first.devicePos)) {
        case Zone::LipstickEdge:
            m_state = State::LipstickEdge;
            break;
        case Zone::BottomEdge:
            m_state = State::BottomEdge;
            break;
        case Zone::Content:
            m_state = State::Engine;
            m_longPress.start(m_config.longPressMs);
            break;
        }
    }

    // Track every point of the batch.
    for (const TouchPoint& p : points) {
        switch (p.phase) {
        case TouchPhase::Down: {
            Active a;
            a.startDevice = p.devicePos;
            a.currentDevice = p.devicePos;
            a.currentCss = p.cssPos;
            m_active.insert(p.id, a);
            break;
        }
        case TouchPhase::Move: {
            auto it = m_active.find(p.id);
            if (it == m_active.end()) {
                Active a;
                a.startDevice = p.devicePos;
                it = m_active.insert(p.id, a);
            }
            it->currentDevice = p.devicePos;
            it->currentCss = p.cssPos;
            const QPointF d = p.devicePos - it->startDevice;
            if (!it->moved && (d.x() * d.x() + d.y() * d.y()) > m_config.moveSlop * m_config.moveSlop)
                it->moved = true;
            break;
        }
        case TouchPhase::Up:
        case TouchPhase::Cancel:
            m_active.remove(p.id);
            break;
        }
    }

    switch (m_state) {
    case State::Idle:
        break;

    case State::Engine: {
        result.forward = points;
        if (m_active.size() > 1 || anyMoved())
            m_longPress.stop();
        // Pulley handoff: one finger, a mostly vertical drag past the slop,
        // in a direction the content cannot scroll.
        if (m_handoffEnabled && m_active.size() == 1) {
            auto it = m_active.constFind(m_primaryId);
            if (it != m_active.constEnd() && it->moved) {
                const QPointF d = it->currentDevice - it->startDevice;
                const bool vertical = qAbs(d.y()) > qAbs(d.x()) * 1.5;
                if (vertical && ((d.y() > 0 && m_atTop) || (d.y() < 0 && m_atBottom))) {
                    m_longPress.stop();
                    result.forward = cancelPoints();
                    result.handoff = true;
                    m_state = State::HandedOff;
                }
            }
        }
        break;
    }

    case State::HandedOff:
        // The parent Flickable owns the rest of this sequence.
        break;

    case State::LongPressed:
        // Everything after a long-press belongs to Tuuli's context menu.
        break;

    case State::LipstickEdge:
        result.accepted = false;
        break;

    case State::BottomEdge: {
        for (const TouchPoint& p : points) {
            if (p.id != m_primaryId)
                continue;
            if (p.phase == TouchPhase::Move) {
                const qreal dist = m_config.bottomRevealDistance > 0 ? m_config.bottomRevealDistance : 1.0;
                auto it = m_active.constFind(p.id);
                const qreal startY = it != m_active.constEnd() ? it->startDevice.y() : p.devicePos.y();
                m_bottomProgress = qBound<qreal>(0.0, (startY - p.devicePos.y()) / dist, 1.0);
                emit bottomEdgeProgress(m_bottomProgress);
            } else if (p.phase == TouchPhase::Up) {
                emit bottomEdgeFinished(m_bottomProgress >= m_config.bottomCommitFraction);
            } else if (p.phase == TouchPhase::Cancel) {
                emit bottomEdgeFinished(false);
            }
        }
        break;
    }
    }

    finishSequenceIfIdle();
    return result;
}

void GestureArbiter::finishSequenceIfIdle()
{
    if (m_active.isEmpty()) {
        m_longPress.stop();
        m_state = State::Idle;
        m_primaryId = -1;
    }
}

QVector<TouchPoint> GestureArbiter::cancelPoints() const
{
    QVector<TouchPoint> out;
    for (auto it = m_active.constBegin(); it != m_active.constEnd(); ++it) {
        TouchPoint tp;
        tp.id = it.key();
        tp.phase = TouchPhase::Cancel;
        tp.devicePos = it.value().currentDevice;
        tp.cssPos = it.value().currentCss;
        out.append(tp);
    }
    return out;
}

void GestureArbiter::onLongPressTimeout()
{
    if (m_state != State::Engine || m_active.size() != 1 || anyMoved())
        return;
    m_state = State::LongPressed;
    const Active a = m_active.constBegin().value();
    const QVector<TouchPoint> cancels = cancelPoints();
    emit longPressed(a.currentDevice, a.currentCss);
    emit engineTouchesCancelled(cancels);
}

} // namespace Tuuli
