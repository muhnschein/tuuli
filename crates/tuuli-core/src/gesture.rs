// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Decides who owns a touch sequence (spec 6.2):
//!
//! | start / condition | owner |
//! |---|---|
//! | left/right/top screen edge | lipstick, never consumed |
//! | bottom edge | Tuuli, toolbar reveal |
//! | single-finger drag, pinch, double-tap | engine |
//! | hold without movement for `long_press` | Tuuli context menu; engine gets a cancel |
//! | vertical drag past the content edge | parent flickable (pulley menus) |
//!
//! The arbiter never implements a kinetic scroller; anything it forwards is
//! handed verbatim to the engine's own async touch pipeline (spec 6.1).
//! Time is passed in explicitly so the logic is testable; the Qt layer runs
//! a timer that fires [`GestureArbiter::fire_long_press_if_due`].

use std::collections::HashMap;
use std::time::{Duration, Instant};

use crate::geometry::{Point, Size};
use crate::input::{TouchPhase, TouchPoint};

#[derive(Clone, Debug, PartialEq)]
pub struct GestureConfig {
    /// Whole screen, device px.
    pub screen: Size,
    /// Where the webview item sits on the screen.
    pub item_origin_on_screen: Point,
    pub side_edge_margin: f64,
    pub top_edge_margin: f64,
    pub bottom_edge_margin: f64,
    pub long_press: Duration,
    /// Device px of movement before a hold becomes a drag.
    pub move_slop: f64,
    pub bottom_reveal_distance: f64,
    pub bottom_commit_fraction: f64,
}

impl Default for GestureConfig {
    fn default() -> Self {
        Self {
            screen: Size::new(1080.0, 2260.0),
            item_origin_on_screen: Point::default(),
            side_edge_margin: 40.0,
            top_edge_margin: 40.0,
            bottom_edge_margin: 48.0,
            long_press: Duration::from_millis(500),
            move_slop: 18.0,
            bottom_reveal_distance: 200.0,
            bottom_commit_fraction: 0.4,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum State {
    Idle,
    Engine,
    LipstickEdge,
    BottomEdge,
    LongPressed,
    HandedOff,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Zone {
    Content,
    LipstickEdge,
    BottomEdge,
}

/// What happened as a result of one batch of points.
#[derive(Clone, Debug, PartialEq)]
pub enum GestureEvent {
    LongPressed { device: Point, css: Point },
    BottomEdgeProgress(f64),
    BottomEdgeFinished { committed: bool },
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct Outcome {
    /// Points for the engine, in order.
    pub forward: Vec<TouchPoint>,
    /// `false` => the toolkit event is ignored so lipstick's gesture proceeds.
    pub accepted: bool,
    /// Release the grab: the parent flickable takes the rest of the drag.
    pub handoff: bool,
    pub events: Vec<GestureEvent>,
}

#[derive(Clone, Debug)]
struct Active {
    start: Point,
    current: Point,
    current_css: Point,
    moved: bool,
}

#[derive(Debug)]
pub struct GestureArbiter {
    config: GestureConfig,
    state: State,
    active: HashMap<i32, Active>,
    order: Vec<i32>,
    primary: Option<i32>,
    bottom_progress: f64,
    long_press_deadline: Option<Instant>,
    at_top: bool,
    at_bottom: bool,
    handoff_enabled: bool,
}

impl Default for GestureArbiter {
    fn default() -> Self {
        Self::new(GestureConfig::default())
    }
}

impl GestureArbiter {
    pub fn new(config: GestureConfig) -> Self {
        Self {
            config,
            state: State::Idle,
            active: HashMap::new(),
            order: Vec::new(),
            primary: None,
            bottom_progress: 0.0,
            long_press_deadline: None,
            at_top: true,
            at_bottom: false,
            handoff_enabled: true,
        }
    }

    pub fn config(&self) -> &GestureConfig {
        &self.config
    }
    pub fn set_config(&mut self, config: GestureConfig) {
        self.config = config;
    }
    pub fn state(&self) -> State {
        self.state
    }
    pub fn active_count(&self) -> usize {
        self.active.len()
    }
    /// When the Qt layer should call [`fire_long_press_if_due`](Self::fire_long_press_if_due).
    pub fn long_press_deadline(&self) -> Option<Instant> {
        self.long_press_deadline
    }

    /// Whether the engine content is scrolled to its top/bottom edge; set by
    /// the view before each sequence.  A vertical drag away from an edge the
    /// content cannot scroll past is handed to the parent (pulley menus).
    pub fn set_content_edges(&mut self, at_top: bool, at_bottom: bool) {
        self.at_top = at_top;
        self.at_bottom = at_bottom;
    }
    pub fn set_parent_handoff_enabled(&mut self, on: bool) {
        self.handoff_enabled = on;
    }

    pub fn reset(&mut self) {
        self.long_press_deadline = None;
        self.active.clear();
        self.order.clear();
        self.state = State::Idle;
        self.primary = None;
        self.bottom_progress = 0.0;
    }

    pub fn classify(&self, device_pos_in_item: Point) -> Zone {
        let screen = device_pos_in_item + self.config.item_origin_on_screen;
        let s = self.config.screen;
        if s.is_empty() {
            return Zone::Content;
        }
        if screen.x < self.config.side_edge_margin
            || screen.x >= s.width - self.config.side_edge_margin
        {
            return Zone::LipstickEdge;
        }
        if screen.y < self.config.top_edge_margin {
            return Zone::LipstickEdge;
        }
        if screen.y >= s.height - self.config.bottom_edge_margin {
            return Zone::BottomEdge;
        }
        Zone::Content
    }

    fn any_moved(&self) -> bool {
        self.active.values().any(|a| a.moved)
    }

    /// Cancel points for every active touch; sent to the engine when Tuuli
    /// takes a sequence over.
    pub fn cancel_points(&self) -> Vec<TouchPoint> {
        self.order
            .iter()
            .filter_map(|id| self.active.get(id).map(|a| (id, a)))
            .map(|(id, a)| TouchPoint {
                id: *id,
                phase: TouchPhase::Cancel,
                device: a.current,
                css: a.current_css,
            })
            .collect()
    }

    pub fn process(&mut self, points: &[TouchPoint], now: Instant) -> Outcome {
        let mut out = Outcome {
            accepted: true,
            ..Default::default()
        };

        if self.state == State::Idle {
            let Some(first) = points.iter().find(|p| p.phase == TouchPhase::Down) else {
                // Stray move/up without a down (e.g. after reset): swallow.
                out.accepted = false;
                return out;
            };
            self.primary = Some(first.id);
            self.bottom_progress = 0.0;
            self.state = match self.classify(first.device) {
                Zone::LipstickEdge => State::LipstickEdge,
                Zone::BottomEdge => State::BottomEdge,
                Zone::Content => {
                    self.long_press_deadline = Some(now + self.config.long_press);
                    State::Engine
                }
            };
        }

        for p in points {
            match p.phase {
                TouchPhase::Down => {
                    self.active.insert(
                        p.id,
                        Active {
                            start: p.device,
                            current: p.device,
                            current_css: p.css,
                            moved: false,
                        },
                    );
                    if !self.order.contains(&p.id) {
                        self.order.push(p.id);
                    }
                }
                TouchPhase::Move => {
                    let slop2 = self.config.move_slop * self.config.move_slop;
                    let entry = self.active.entry(p.id).or_insert(Active {
                        start: p.device,
                        current: p.device,
                        current_css: p.css,
                        moved: false,
                    });
                    if !self.order.contains(&p.id) {
                        self.order.push(p.id);
                    }
                    entry.current = p.device;
                    entry.current_css = p.css;
                    if !entry.moved && p.device.distance_squared(&entry.start) > slop2 {
                        entry.moved = true;
                    }
                }
                TouchPhase::Up | TouchPhase::Cancel => {
                    self.active.remove(&p.id);
                    self.order.retain(|id| *id != p.id);
                }
            }
        }

        match self.state {
            State::Idle => {}
            State::Engine => {
                out.forward = points.to_vec();
                if self.active.len() > 1 || self.any_moved() {
                    self.long_press_deadline = None;
                }
                if self.handoff_enabled && self.active.len() == 1 {
                    if let Some(a) = self.primary.and_then(|id| self.active.get(&id)) {
                        if a.moved {
                            let d = a.current - a.start;
                            let vertical = d.y.abs() > d.x.abs() * 1.5;
                            if vertical
                                && ((d.y > 0.0 && self.at_top) || (d.y < 0.0 && self.at_bottom))
                            {
                                self.long_press_deadline = None;
                                out.forward = self.cancel_points();
                                out.handoff = true;
                                self.state = State::HandedOff;
                            }
                        }
                    }
                }
            }
            State::HandedOff | State::LongPressed => {}
            State::LipstickEdge => out.accepted = false,
            State::BottomEdge => {
                let primary = self.primary;
                for p in points.iter().filter(|p| Some(p.id) == primary) {
                    match p.phase {
                        TouchPhase::Move => {
                            let dist = if self.config.bottom_reveal_distance > 0.0 {
                                self.config.bottom_reveal_distance
                            } else {
                                1.0
                            };
                            let start_y = self
                                .active
                                .get(&p.id)
                                .map(|a| a.start.y)
                                .unwrap_or(p.device.y);
                            self.bottom_progress = ((start_y - p.device.y) / dist).clamp(0.0, 1.0);
                            out.events
                                .push(GestureEvent::BottomEdgeProgress(self.bottom_progress));
                        }
                        TouchPhase::Up => out.events.push(GestureEvent::BottomEdgeFinished {
                            committed: self.bottom_progress >= self.config.bottom_commit_fraction,
                        }),
                        TouchPhase::Cancel => out
                            .events
                            .push(GestureEvent::BottomEdgeFinished { committed: false }),
                        TouchPhase::Down => {}
                    }
                }
            }
        }

        if self.active.is_empty() {
            self.long_press_deadline = None;
            self.state = State::Idle;
            self.primary = None;
        }
        out
    }

    /// Called by the timer the Qt layer arms from [`long_press_deadline`](Self::long_press_deadline).
    /// Returns the long-press event and the engine cancel points when the
    /// hold is still valid at `now`.
    pub fn fire_long_press_if_due(
        &mut self,
        now: Instant,
    ) -> Option<(GestureEvent, Vec<TouchPoint>)> {
        let deadline = self.long_press_deadline?;
        if now < deadline
            || self.state != State::Engine
            || self.active.len() != 1
            || self.any_moved()
        {
            return None;
        }
        self.long_press_deadline = None;
        self.state = State::LongPressed;
        let a = self.active.values().next().expect("one active point");
        let ev = GestureEvent::LongPressed {
            device: a.current,
            css: a.current_css,
        };
        Some((ev, self.cancel_points()))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tp(id: i32, phase: TouchPhase, x: f64, y: f64) -> TouchPoint {
        TouchPoint {
            id,
            phase,
            device: Point::new(x, y),
            css: Point::new(x / 2.0, y / 2.0),
        }
    }

    fn cfg() -> GestureConfig {
        GestureConfig {
            long_press: Duration::from_millis(60),
            ..Default::default()
        }
    }

    fn arbiter() -> (GestureArbiter, Instant) {
        (GestureArbiter::new(cfg()), Instant::now())
    }

    #[test]
    fn classifies_zones() {
        let (a, _) = arbiter();
        assert_eq!(a.classify(Point::new(10.0, 1000.0)), Zone::LipstickEdge);
        assert_eq!(a.classify(Point::new(1075.0, 1000.0)), Zone::LipstickEdge);
        assert_eq!(a.classify(Point::new(500.0, 5.0)), Zone::LipstickEdge);
        assert_eq!(a.classify(Point::new(500.0, 2250.0)), Zone::BottomEdge);
        assert_eq!(a.classify(Point::new(500.0, 1000.0)), Zone::Content);
    }

    #[test]
    fn item_offset_is_screen_relative() {
        let mut c = cfg();
        c.item_origin_on_screen = Point::new(0.0, 200.0);
        let a = GestureArbiter::new(c);
        assert_eq!(a.classify(Point::new(500.0, 5.0)), Zone::Content);
    }

    #[test]
    fn content_drag_goes_to_engine() {
        let (mut a, t) = arbiter();
        let r = a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        assert!(r.accepted);
        assert_eq!(r.forward.len(), 1);
        assert_eq!(a.state(), State::Engine);
        assert!(a.long_press_deadline().is_some());
        let r = a.process(&[tp(1, TouchPhase::Move, 500.0, 900.0)], t);
        assert_eq!(r.forward.len(), 1);
        assert!(!r.handoff);
        let r = a.process(&[tp(1, TouchPhase::Up, 500.0, 900.0)], t);
        assert_eq!(r.forward.len(), 1);
        assert_eq!(a.state(), State::Idle);
        assert_eq!(a.active_count(), 0);
    }

    #[test]
    fn lipstick_edge_is_never_consumed() {
        let (mut a, t) = arbiter();
        let r = a.process(&[tp(1, TouchPhase::Down, 5.0, 1000.0)], t);
        assert!(!r.accepted);
        assert!(r.forward.is_empty());
        let r = a.process(&[tp(1, TouchPhase::Move, 200.0, 1000.0)], t);
        assert!(!r.accepted);
        let r = a.process(&[tp(1, TouchPhase::Up, 200.0, 1000.0)], t);
        assert!(r.forward.is_empty());
        assert_eq!(a.state(), State::Idle);
    }

    #[test]
    fn bottom_edge_reveals_toolbar() {
        let (mut a, t) = arbiter();
        let r = a.process(&[tp(1, TouchPhase::Down, 500.0, 2250.0)], t);
        assert!(r.accepted && r.forward.is_empty());
        assert_eq!(a.state(), State::BottomEdge);
        let r = a.process(&[tp(1, TouchPhase::Move, 500.0, 2150.0)], t);
        assert!(r.forward.is_empty());
        assert_eq!(r.events, vec![GestureEvent::BottomEdgeProgress(0.5)]);
        let r = a.process(&[tp(1, TouchPhase::Up, 500.0, 2150.0)], t);
        assert_eq!(
            r.events,
            vec![GestureEvent::BottomEdgeFinished { committed: true }]
        );
        assert_eq!(a.state(), State::Idle);

        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 2250.0)], t);
        a.process(&[tp(1, TouchPhase::Move, 500.0, 2220.0)], t);
        let r = a.process(&[tp(1, TouchPhase::Up, 500.0, 2220.0)], t);
        assert_eq!(
            r.events,
            vec![GestureEvent::BottomEdgeFinished { committed: false }]
        );
    }

    #[test]
    fn long_press_fires_without_movement() {
        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        assert!(a
            .fire_long_press_if_due(t + Duration::from_millis(10))
            .is_none());
        let (ev, cancels) = a
            .fire_long_press_if_due(t + Duration::from_millis(70))
            .expect("fires");
        assert_eq!(
            ev,
            GestureEvent::LongPressed {
                device: Point::new(500.0, 1000.0),
                css: Point::new(250.0, 500.0)
            }
        );
        assert_eq!(cancels.len(), 1);
        assert_eq!(cancels[0].phase, TouchPhase::Cancel);
        assert_eq!(a.state(), State::LongPressed);
        let r = a.process(&[tp(1, TouchPhase::Move, 520.0, 1000.0)], t);
        assert!(r.forward.is_empty());
        let r = a.process(&[tp(1, TouchPhase::Up, 520.0, 1000.0)], t);
        assert!(r.forward.is_empty());
        assert_eq!(a.state(), State::Idle);
    }

    #[test]
    fn long_press_survives_jitter_but_not_drag_or_second_finger() {
        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        a.process(&[tp(1, TouchPhase::Move, 505.0, 1004.0)], t);
        assert!(a
            .fire_long_press_if_due(t + Duration::from_millis(100))
            .is_some());

        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        a.process(&[tp(1, TouchPhase::Move, 500.0, 900.0)], t);
        assert!(a.long_press_deadline().is_none());
        assert!(a
            .fire_long_press_if_due(t + Duration::from_millis(100))
            .is_none());
        assert_eq!(a.state(), State::Engine);

        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = a.process(&[tp(2, TouchPhase::Down, 600.0, 1100.0)], t);
        assert_eq!(r.forward.len(), 1);
        assert!(a
            .fire_long_press_if_due(t + Duration::from_millis(100))
            .is_none());
        assert_eq!(a.active_count(), 2);
    }

    #[test]
    fn release_before_timeout_is_a_tap() {
        let (mut a, t) = arbiter();
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = a.process(&[tp(1, TouchPhase::Up, 500.0, 1000.0)], t);
        assert_eq!(r.forward.len(), 1);
        assert!(a.long_press_deadline().is_none());
        assert!(a
            .fire_long_press_if_due(t + Duration::from_secs(1))
            .is_none());
    }

    #[test]
    fn vertical_drag_at_top_hands_off_to_parent() {
        let (mut a, t) = arbiter();
        a.set_content_edges(true, false);
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = a.process(&[tp(1, TouchPhase::Move, 502.0, 1060.0)], t);
        assert!(r.handoff && r.accepted);
        assert_eq!(r.forward.len(), 1);
        assert_eq!(r.forward[0].phase, TouchPhase::Cancel);
        assert_eq!(a.state(), State::HandedOff);
        let r = a.process(&[tp(1, TouchPhase::Move, 502.0, 1200.0)], t);
        assert!(r.forward.is_empty());
        a.process(&[tp(1, TouchPhase::Up, 502.0, 1200.0)], t);
        assert_eq!(a.state(), State::Idle);
    }

    #[test]
    fn drag_into_content_stays_with_engine() {
        let (mut a, t) = arbiter();
        a.set_content_edges(true, false);
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = a.process(&[tp(1, TouchPhase::Move, 500.0, 940.0)], t);
        assert!(!r.handoff);
        assert_eq!(r.forward[0].phase, TouchPhase::Move);
        let r = a.process(&[tp(1, TouchPhase::Move, 500.0, 400.0)], t);
        assert!(!r.handoff);

        let (mut b, t) = arbiter();
        b.set_content_edges(true, false);
        b.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = b.process(&[tp(1, TouchPhase::Move, 580.0, 1050.0)], t);
        assert!(!r.handoff, "diagonal drag is a pan");

        let (mut c, t) = arbiter();
        c.set_content_edges(true, true);
        c.set_parent_handoff_enabled(false);
        c.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        let r = c.process(&[tp(1, TouchPhase::Move, 500.0, 1100.0)], t);
        assert!(!r.handoff);
        assert_eq!(c.state(), State::Engine);
    }

    #[test]
    fn stray_move_and_reset() {
        let (mut a, t) = arbiter();
        let r = a.process(&[tp(1, TouchPhase::Move, 500.0, 1000.0)], t);
        assert!(!r.accepted && r.forward.is_empty());
        a.process(&[tp(1, TouchPhase::Down, 500.0, 1000.0)], t);
        a.reset();
        assert_eq!(a.state(), State::Idle);
        assert_eq!(a.active_count(), 0);
        assert!(a.long_press_deadline().is_none());
    }
}
