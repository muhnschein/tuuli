// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Touch input as it crosses from the toolkit into the engine (spec 6.1).

use crate::geometry::{device_to_css, Point};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TouchPhase {
    Down,
    Move,
    Up,
    Cancel,
}

/// Per-point state as the toolkit reports it (Qt's `Qt::TouchPointState`).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RawTouchState {
    Pressed,
    Moved,
    Stationary,
    Released,
}

/// The event-level type (Qt's `QEvent::Type` for touch events).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RawTouchKind {
    Begin,
    Update,
    End,
    Cancel,
}

/// One raw point from the toolkit, device px relative to the item.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RawTouchPoint {
    pub id: i32,
    pub state: RawTouchState,
    pub pos: Point,
}

/// One engine-bound touch point.  `css` is what the engine gets; `device`
/// is kept for the gesture arbiter, which reasons about screen-edge zones.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct TouchPoint {
    pub id: i32,
    pub phase: TouchPhase,
    pub device: Point,
    pub css: Point,
}

/// Converts toolkit touch events to engine touch points.  Stationary points
/// are dropped (the engine tracks them by id); a cancel event cancels every
/// point regardless of its individual state.
#[derive(Clone, Debug)]
pub struct TouchConverter {
    pub dpr: f64,
    /// Item-space offset subtracted from every point before conversion.
    pub origin: Point,
}

impl Default for TouchConverter {
    fn default() -> Self {
        Self {
            dpr: 1.0,
            origin: Point::default(),
        }
    }
}

impl TouchConverter {
    pub fn new(dpr: f64) -> Self {
        Self {
            dpr,
            ..Default::default()
        }
    }

    pub fn phase_for(kind: RawTouchKind, state: RawTouchState) -> Option<TouchPhase> {
        if kind == RawTouchKind::Cancel {
            return Some(TouchPhase::Cancel);
        }
        match state {
            RawTouchState::Pressed => Some(TouchPhase::Down),
            RawTouchState::Moved => Some(TouchPhase::Move),
            RawTouchState::Released => Some(TouchPhase::Up),
            RawTouchState::Stationary => None,
        }
    }

    pub fn convert(&self, kind: RawTouchKind, points: &[RawTouchPoint]) -> Vec<TouchPoint> {
        points
            .iter()
            .filter_map(|p| {
                let phase = Self::phase_for(kind, p.state)?;
                let device = p.pos - self.origin;
                Some(TouchPoint {
                    id: p.id,
                    phase,
                    device,
                    css: device_to_css(device, self.dpr),
                })
            })
            .collect()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raw(id: i32, state: RawTouchState, x: f64, y: f64) -> RawTouchPoint {
        RawTouchPoint {
            id,
            state,
            pos: Point::new(x, y),
        }
    }

    #[test]
    fn phase_mapping() {
        assert_eq!(
            TouchConverter::phase_for(RawTouchKind::Begin, RawTouchState::Pressed),
            Some(TouchPhase::Down)
        );
        assert_eq!(
            TouchConverter::phase_for(RawTouchKind::Update, RawTouchState::Moved),
            Some(TouchPhase::Move)
        );
        assert_eq!(
            TouchConverter::phase_for(RawTouchKind::End, RawTouchState::Released),
            Some(TouchPhase::Up)
        );
        assert_eq!(
            TouchConverter::phase_for(RawTouchKind::Update, RawTouchState::Stationary),
            None
        );
        assert_eq!(
            TouchConverter::phase_for(RawTouchKind::Cancel, RawTouchState::Stationary),
            Some(TouchPhase::Cancel)
        );
    }

    #[test]
    fn converts_to_css_pixels() {
        let c = TouchConverter::new(2.0);
        let out = c.convert(
            RawTouchKind::Begin,
            &[raw(7, RawTouchState::Pressed, 540.0, 1130.0)],
        );
        assert_eq!(out.len(), 1);
        assert_eq!(out[0].id, 7);
        assert_eq!(out[0].phase, TouchPhase::Down);
        assert_eq!(out[0].device, Point::new(540.0, 1130.0));
        assert_eq!(out[0].css, Point::new(270.0, 565.0));
    }

    #[test]
    fn drops_stationary_and_cancels_everything() {
        let c = TouchConverter::new(1.0);
        let pts = [
            raw(1, RawTouchState::Stationary, 1.0, 1.0),
            raw(2, RawTouchState::Moved, 5.0, 5.0),
        ];
        let out = c.convert(RawTouchKind::Update, &pts);
        assert_eq!(out.len(), 1);
        assert_eq!(out[0].id, 2);
        let out = c.convert(RawTouchKind::Cancel, &pts);
        assert_eq!(out.len(), 2);
        assert!(out.iter().all(|p| p.phase == TouchPhase::Cancel));
    }

    #[test]
    fn origin_and_dpr_apply() {
        let mut c = TouchConverter::new(2.0);
        c.origin = Point::new(0.0, 100.0);
        let out = c.convert(
            RawTouchKind::Begin,
            &[raw(1, RawTouchState::Pressed, 10.0, 300.0)],
        );
        assert_eq!(out[0].device, Point::new(10.0, 200.0));
        assert_eq!(out[0].css, Point::new(5.0, 100.0));
        c.origin = Point::default();
        c.dpr = 2.5;
        let out = c.convert(
            RawTouchKind::Begin,
            &[raw(1, RawTouchState::Pressed, 100.0, 100.0)],
        );
        assert_eq!(out[0].css, Point::new(40.0, 40.0));
    }
}
