// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Device-pixel <-> CSS-pixel maths (spec 6.1) and the viewport layout used
//! to keep a focused element visible above the virtual keyboard (spec 6.3).

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Point {
    pub x: f64,
    pub y: f64,
}

impl Point {
    pub const fn new(x: f64, y: f64) -> Self {
        Self { x, y }
    }
    pub fn is_zero(&self) -> bool {
        self.x == 0.0 && self.y == 0.0
    }
    pub fn distance_squared(&self, o: &Point) -> f64 {
        let dx = self.x - o.x;
        let dy = self.y - o.y;
        dx * dx + dy * dy
    }
}

impl std::ops::Sub for Point {
    type Output = Point;
    fn sub(self, o: Point) -> Point {
        Point::new(self.x - o.x, self.y - o.y)
    }
}

impl std::ops::Add for Point {
    type Output = Point;
    fn add(self, o: Point) -> Point {
        Point::new(self.x + o.x, self.y + o.y)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Size {
    pub width: f64,
    pub height: f64,
}

impl Size {
    pub const fn new(width: f64, height: f64) -> Self {
        Self { width, height }
    }
    pub fn is_empty(&self) -> bool {
        self.width <= 0.0 || self.height <= 0.0
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Rect {
    pub x: f64,
    pub y: f64,
    pub width: f64,
    pub height: f64,
}

impl Rect {
    pub const fn new(x: f64, y: f64, width: f64, height: f64) -> Self {
        Self {
            x,
            y,
            width,
            height,
        }
    }
    pub fn is_empty(&self) -> bool {
        self.width <= 0.0 || self.height <= 0.0
    }
    pub fn left(&self) -> f64 {
        self.x
    }
    pub fn top(&self) -> f64 {
        self.y
    }
    pub fn right(&self) -> f64 {
        self.x + self.width
    }
    pub fn bottom(&self) -> f64 {
        self.y + self.height
    }
    pub fn adjusted(&self, dx1: f64, dy1: f64, dx2: f64, dy2: f64) -> Rect {
        Rect::new(
            self.x + dx1,
            self.y + dy1,
            self.width - dx1 + dx2,
            self.height - dy1 + dy2,
        )
    }
    pub fn is_null(&self) -> bool {
        self.x == 0.0 && self.y == 0.0 && self.width == 0.0 && self.height == 0.0
    }
}

pub fn sanitize_dpr(dpr: f64) -> f64 {
    if dpr > 0.0 && dpr.is_finite() {
        dpr
    } else {
        1.0
    }
}

pub fn device_to_css(device: Point, dpr: f64) -> Point {
    let dpr = sanitize_dpr(dpr);
    Point::new(device.x / dpr, device.y / dpr)
}

pub fn css_to_device(css: Point, dpr: f64) -> Point {
    let dpr = sanitize_dpr(dpr);
    Point::new(css.x * dpr, css.y * dpr)
}

pub fn size_device_to_css(device: Size, dpr: f64) -> Size {
    let dpr = sanitize_dpr(dpr);
    Size::new(device.width / dpr, device.height / dpr)
}

pub fn rect_device_to_css(device: Rect, dpr: f64) -> Rect {
    let dpr = sanitize_dpr(dpr);
    Rect::new(
        device.x / dpr,
        device.y / dpr,
        device.width / dpr,
        device.height / dpr,
    )
}

/// Rounds to whole device pixels.
pub fn rect_css_to_device(css: Rect, dpr: f64) -> Rect {
    let dpr = sanitize_dpr(dpr);
    Rect::new(
        (css.x * dpr).round(),
        (css.y * dpr).round(),
        (css.width * dpr).round(),
        (css.height * dpr).round(),
    )
}

/// Derive a content DPR for a panel (spec 6.1: "do not hardcode").
///
/// Qt reports 1.0 on Sailfish (Silica scales via `Theme.pixelRatio`, not the
/// `QScreen` DPR), so when Qt gives us 1.0 we fall back to the Android density
/// convention (ppi / 160) rounded to the nearest 0.25, clamped to `[1, 4]`.
pub fn derive_device_pixel_ratio(qt_dpr: f64, physical_dpi: f64) -> f64 {
    if qt_dpr > 1.0 && qt_dpr.is_finite() {
        return qt_dpr;
    }
    if physical_dpi.is_nan() || physical_dpi <= 0.0 || !physical_dpi.is_finite() {
        return 1.0;
    }
    let density = (physical_dpi / 160.0 * 4.0).round() / 4.0;
    density.clamp(1.0, 4.0)
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ViewportLayout {
    /// Part of the surface not covered by keyboard or chrome, device px.
    pub visible_device: Rect,
    pub visible_css: Rect,
    pub obscured: bool,
}

/// Surface is the full FBO.  `bottom_inset_device` is the height of whatever
/// covers the bottom of the surface (VKB plus overlapping chrome).  The
/// surface itself is never resized for the keyboard (spec 6.3); only the
/// viewport rect handed to the engine changes.
pub fn layout_viewport(
    surface_device: Size,
    bottom_inset_device: f64,
    top_inset_device: f64,
    dpr: f64,
) -> ViewportLayout {
    let w = surface_device.width.max(0.0);
    let h = surface_device.height.max(0.0);
    let top = top_inset_device.clamp(0.0, h);
    let bottom = bottom_inset_device.clamp(0.0, h - top);
    let visible_device = Rect::new(0.0, top, w, h - top - bottom);
    ViewportLayout {
        visible_device,
        visible_css: rect_device_to_css(visible_device, dpr),
        obscured: top > 0.0 || bottom > 0.0,
    }
}

/// Scroll delta (CSS px, positive = down/right) required to bring `element`
/// (viewport-relative CSS rect) inside `visible` with `margin` around it.
/// Zero if already visible.  Elements taller than the visible area align to
/// its top edge.
pub fn scroll_delta_to_reveal(element: Rect, visible: Rect, margin: f64) -> Point {
    let mut delta = Point::default();
    if visible.is_empty() {
        return delta;
    }
    let target = element.adjusted(-margin, -margin, margin, margin);

    if target.height >= visible.height {
        delta.y = target.top() - visible.top();
    } else if target.bottom() > visible.bottom() {
        delta.y = target.bottom() - visible.bottom();
    } else if target.top() < visible.top() {
        delta.y = target.top() - visible.top();
    }

    if target.width >= visible.width {
        delta.x = target.left() - visible.left();
    } else if target.right() > visible.right() {
        delta.x = target.right() - visible.right();
    } else if target.left() < visible.left() {
        delta.x = target.left() - visible.left();
    }
    delta
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn point_conversion_round_trips() {
        let css = device_to_css(Point::new(1080.0, 2260.0), 2.0);
        assert_eq!(css, Point::new(540.0, 1130.0));
        assert_eq!(css_to_device(css, 2.0), Point::new(1080.0, 2260.0));
        assert_eq!(device_to_css(Point::new(1080.0, 0.0), 2.5).x, 432.0);
    }

    #[test]
    fn bad_dpr_falls_back_to_one() {
        assert_eq!(sanitize_dpr(0.0), 1.0);
        assert_eq!(sanitize_dpr(-2.0), 1.0);
        assert_eq!(sanitize_dpr(f64::NAN), 1.0);
        assert_eq!(
            device_to_css(Point::new(10.0, 10.0), 0.0),
            Point::new(10.0, 10.0)
        );
    }

    #[test]
    fn rect_conversion_rounds() {
        let r = rect_css_to_device(Rect::new(0.4, 0.6, 10.26, 20.74), 1.0);
        assert_eq!(r, Rect::new(0.0, 1.0, 10.0, 21.0));
        assert_eq!(
            rect_device_to_css(Rect::new(0.0, 0.0, 1080.0, 2260.0), 2.0),
            Rect::new(0.0, 0.0, 540.0, 1130.0)
        );
    }

    #[test]
    fn derived_dpr_uses_qt_when_above_one() {
        assert_eq!(derive_device_pixel_ratio(2.0, 394.0), 2.0);
        assert_eq!(derive_device_pixel_ratio(1.5, 394.0), 1.5);
    }

    #[test]
    fn derived_dpr_from_panel_density() {
        for (dpi, expected) in [
            (394.0, 2.5),
            (457.0, 2.75),
            (160.0, 1.0),
            (40.0, 1.0),
            (1000.0, 4.0),
            (0.0, 1.0),
        ] {
            assert_eq!(derive_device_pixel_ratio(1.0, dpi), expected, "dpi {dpi}");
        }
    }

    #[test]
    fn viewport_layout_with_keyboard() {
        let l = layout_viewport(Size::new(1080.0, 2260.0), 800.0, 0.0, 2.0);
        assert_eq!(l.visible_device, Rect::new(0.0, 0.0, 1080.0, 1460.0));
        assert_eq!(l.visible_css, Rect::new(0.0, 0.0, 540.0, 730.0));
        assert!(l.obscured);
    }

    #[test]
    fn viewport_layout_clamps_insets() {
        let l = layout_viewport(Size::new(100.0, 100.0), 500.0, 500.0, 1.0);
        assert_eq!(l.visible_device, Rect::new(0.0, 100.0, 100.0, 0.0));
        let none = layout_viewport(Size::new(100.0, 100.0), 0.0, 0.0, 1.0);
        assert!(!none.obscured);
        assert_eq!(none.visible_device, Rect::new(0.0, 0.0, 100.0, 100.0));
    }

    #[test]
    fn scroll_delta_cases() {
        let visible = Rect::new(0.0, 0.0, 540.0, 730.0);
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(10.0, 10.0, 50.0, 20.0), visible, 8.0),
            Point::default()
        );
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(10.0, 900.0, 50.0, 20.0), visible, 8.0),
            Point::new(0.0, 198.0)
        );
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(10.0, -50.0, 50.0, 20.0), visible, 8.0),
            Point::new(0.0, -58.0)
        );
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(0.0, 100.0, 50.0, 2000.0), visible, 0.0),
            Point::new(0.0, 100.0)
        );
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(600.0, 10.0, 50.0, 20.0), visible, 0.0),
            Point::new(110.0, 0.0)
        );
        assert_eq!(
            scroll_delta_to_reveal(Rect::new(10.0, 10.0, 50.0, 20.0), Rect::default(), 8.0),
            Point::default()
        );
    }
}
