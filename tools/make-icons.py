#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Renders the launcher icon (a wind mark; "tuuli" is Finnish for wind) at
the Sailfish launcher sizes without any image library, so the icons can be
regenerated anywhere.  Output: icons/<N>x<N>/tuuli-browser.png"""

import math
import os
import struct
import sys
import zlib

SIZES = (86, 108, 128, 172)
BG_TOP = (0x1c, 0x3a, 0x5e)
BG_BOTTOM = (0x0e, 0x1f, 0x33)
INK = (0xf2, 0xf6, 0xfa)
ACCENT = (0x7f, 0xd1, 0xff)


def write_png(path, size, pixels):
    raw = bytearray()
    for y in range(size):
        raw.append(0)
        for x in range(size):
            raw.extend(pixels[y][x])

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def dist_to_arc(px, py, cx, cy, r, a0, a1):
    """Distance from a point to a circular arc (angles in radians)."""
    dx, dy = px - cx, py - cy
    ang = math.atan2(dy, dx)
    d = math.hypot(dx, dy)
    while a0 > a1:
        a1 += 2 * math.pi
    a = ang
    while a < a0:
        a += 2 * math.pi
    if a <= a1:
        return abs(d - r)
    # nearest endpoint
    e0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
    e1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
    return min(math.hypot(px - e0[0], py - e0[1]), math.hypot(px - e1[0], py - e1[1]))


def dist_to_segment(px, py, x0, y0, x1, y1):
    vx, vy = x1 - x0, y1 - y0
    wx, wy = px - x0, py - y0
    l2 = vx * vx + vy * vy
    t = 0.0 if l2 == 0 else max(0.0, min(1.0, (wx * vx + wy * vy) / l2))
    return math.hypot(px - (x0 + t * vx), py - (y0 + t * vy))


def render(size):
    s = size
    corner = s * 0.22
    stroke = s * 0.075
    pixels = []
    # Three wind strokes: horizontal lines that curl up at the right end.
    strokes = [
        (0.20, 0.36, 0.62, 0.11, True),
        (0.14, 0.52, 0.72, 0.13, True),
        (0.26, 0.68, 0.56, 0.09, False),
    ]
    for y in range(s):
        row = []
        for x in range(s):
            px, py = x + 0.5, y + 0.5
            # rounded square coverage
            qx = max(abs(px - s / 2) - (s / 2 - corner), 0)
            qy = max(abs(py - s / 2) - (s / 2 - corner), 0)
            dcorner = math.hypot(qx, qy) - corner
            cov = max(0.0, min(1.0, 0.5 - dcorner))
            if cov <= 0:
                row.append((0, 0, 0, 0))
                continue
            bg = lerp(BG_TOP, BG_BOTTOM, py / s)
            col = bg
            best = 1e9
            accent = False
            for (x0, yy, x1, r, curl) in strokes:
                X0, Y, X1, R = x0 * s, yy * s, x1 * s, r * s
                d = dist_to_segment(px, py, X0, Y, X1, Y)
                if curl:
                    d = min(d, dist_to_arc(px, py, X1, Y - R, R, math.pi / 2, math.pi * 2 - 0.35))
                if d < best:
                    best = d
                    accent = curl and px > X1 - 1
            ink = max(0.0, min(1.0, stroke / 2 - best + 0.5))
            if ink > 0:
                fg = ACCENT if accent else INK
                col = lerp(bg, fg, ink)
            row.append((col[0], col[1], col[2], int(round(255 * cov))))
        pixels.append(row)
    return pixels


def main():
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "icons")
    for size in SIZES:
        write_png(os.path.join(root, "%dx%d" % (size, size), "tuuli-browser.png"), size, render(size))
        print("wrote %dx%d" % (size, size))
    return 0


if __name__ == "__main__":
    sys.exit(main())
