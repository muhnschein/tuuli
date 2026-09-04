#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Reference-screenshot comparison for the ten-page corpus (spec 13).

    tools/screenshots/compare.py reference/ candidate/ [--threshold 0.02]
                                 [--diff-dir out/]

Both directories hold <corpus id>.png captured on the device (the app
writes them to <cache>/screenshots/ when Performance logging is on and the
corpus is loaded via `tuuli-browser --capture-corpus`).  A page fails when
the fraction of differing pixels (per-channel delta > 16/255) exceeds the
threshold.  Only 8-bit RGB/RGBA non-interlaced PNGs are understood; that is
what the app writes.  No image library required.
"""

import argparse
import os
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s: not a PNG" % path)
    pos = 8
    width = height = None
    idat = bytearray()
    color_type = bit_depth = None
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        tag = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
                raise ValueError("%s: only 8-bit RGB/RGBA non-interlaced PNGs are supported" % path)
        elif tag == b"IDAT":
            idat.extend(body)
        elif tag == b"IEND":
            break
    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(bytes(idat))
    stride = width * channels
    rows = []
    prev = bytearray(stride)
    p = 0
    for _ in range(height):
        filt = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if filt == 1:
                line[i] = (line[i] + a) & 0xFF
            elif filt == 2:
                line[i] = (line[i] + b) & 0xFF
            elif filt == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 0xFF
            elif filt == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 0xFF
        rows.append(bytes(line))
        prev = line
    return width, height, channels, rows


def write_diff(path, width, height, mask_rows):
    raw = bytearray()
    for row in mask_rows:
        raw.append(0)
        for v in row:
            raw.extend((255, 0, 0, 255) if v else (0, 0, 0, 40))

    def chunk(tag, body):
        c = struct.pack(">I", len(body)) + tag + body
        return c + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def compare(ref, cand, delta=16):
    w1, h1, c1, r1 = read_png(ref)
    w2, h2, c2, r2 = read_png(cand)
    if (w1, h1) != (w2, h2):
        return None, None, "size differs: %dx%d vs %dx%d" % (w1, h1, w2, h2)
    differing = 0
    mask = []
    for y in range(h1):
        a, b = r1[y], r2[y]
        row = bytearray(w1)
        for x in range(w1):
            for ch in range(3):
                if abs(a[x * c1 + ch] - b[x * c2 + ch]) > delta:
                    row[x] = 1
                    differing += 1
                    break
        mask.append(row)
    return differing / float(w1 * h1), (w1, h1, mask), None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference")
    ap.add_argument("candidate")
    ap.add_argument("--threshold", type=float, default=0.02)
    ap.add_argument("--diff-dir", default=None)
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.reference) if n.endswith(".png"))
    if not names:
        print("no reference screenshots in %s" % args.reference, file=sys.stderr)
        return 2
    failed = 0
    for name in names:
        ref = os.path.join(args.reference, name)
        cand = os.path.join(args.candidate, name)
        if not os.path.exists(cand):
            print("%-24s MISSING candidate" % name)
            failed += 1
            continue
        frac, mask, err = compare(ref, cand)
        if err:
            print("%-24s FAIL %s" % (name, err))
            failed += 1
            continue
        status = "ok" if frac <= args.threshold else "FAIL"
        if status == "FAIL":
            failed += 1
        print("%-24s %s  %.2f%% pixels differ" % (name, status, frac * 100))
        if args.diff_dir and status == "FAIL":
            os.makedirs(args.diff_dir, exist_ok=True)
            w, h, rows = mask
            write_diff(os.path.join(args.diff_dir, name), w, h, rows)
    print("\n%d of %d pages within %.1f%%" % (len(names) - failed, len(names), args.threshold * 100))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
