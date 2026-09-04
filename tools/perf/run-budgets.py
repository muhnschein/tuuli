#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Evaluates an on-device performance log against tools/budgets.json
(spec 11, 13).

The app writes JSON lines to <cache>/perf.log when Settings → Developer →
"Performance logging" is on (see src/lib/perf/perflog.cpp).  Copy that
file off the device and run:

    tools/perf/run-budgets.py perf.log [--budgets tools/budgets.json]
                              [--panel-hz 90] [--json report.json]

Exit status is non-zero when any budget fails, so this can gate a release.
Budgets that the log carries no sample for are reported as "no data", not
as passes.

Log record kinds:
  {"kind":"start","cold":true|false,"first_paint_ms":N}
  {"kind":"load","page":"<corpus id or url>","fcp_ms":N,"rss_mb":N,"tabs":N}
  {"kind":"frames","page":"...","interaction":"scroll"|"pinch","frames":N,"dropped":N,"duration_ms":N}
  {"kind":"rss","tabs":N,"rss_mb":N}
  {"kind":"battery","minutes":30,"drop_percent":N}
"""

import argparse
import json
import statistics
import sys


def load_log(path):
    records = []
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            try:
                records.append(json.loads(line))
            except ValueError:
                continue
    return records


def median(values):
    return statistics.median(values) if values else None


def evaluate(records, budgets, panel_hz):
    b = budgets["budgets"]
    results = []

    def add(name, value, limit, kind, unit):
        ok = None
        if value is not None:
            ok = (value <= limit) if kind == "max" else (value >= limit)
        results.append({"metric": name, "value": value, "limit": limit, "kind": kind, "unit": unit, "ok": ok})

    starts_cold = [r["first_paint_ms"] for r in records if r.get("kind") == "start" and r.get("cold")]
    starts_warm = [r["first_paint_ms"] for r in records if r.get("kind") == "start" and not r.get("cold")]
    add("cold_start_first_paint_ms", median(starts_cold), b["cold_start_first_paint_ms"]["max"], "max", "ms")
    add("warm_start_first_paint_ms", median(starts_warm), b["warm_start_first_paint_ms"]["max"], "max", "ms")

    article = b["article_first_contentful_paint_ms"]["page"]
    fcp = [r["fcp_ms"] for r in records if r.get("kind") == "load" and r.get("page") == article and "fcp_ms" in r]
    add("article_first_contentful_paint_ms", median(fcp), b["article_first_contentful_paint_ms"]["max"], "max", "ms")

    scroll = [r for r in records if r.get("kind") == "frames" and r.get("interaction") == "scroll" and r.get("page") == article]
    fps = None
    if scroll:
        fps_samples = [r["frames"] / (r["duration_ms"] / 1000.0) for r in scroll if r.get("duration_ms")]
        fps = median(fps_samples)
    fps_limit = round(panel_hz * b["scroll_fps_article"]["min_fraction_of_panel"], 1)
    add("scroll_fps_article", None if fps is None else round(fps, 1), fps_limit, "min", "fps @ %d Hz" % panel_hz)

    pinch = [r for r in records if r.get("kind") == "frames" and r.get("interaction") == "pinch"]
    dropped = None
    if pinch:
        total = sum(r["frames"] + r.get("dropped", 0) for r in pinch)
        dropped = (sum(r.get("dropped", 0) for r in pinch) / total) if total else 0.0
    add("pinch_dropped_frames_fraction", None if dropped is None else round(dropped, 4),
        b["pinch_dropped_frames_fraction"]["max"], "max", "fraction")

    rss1 = [r["rss_mb"] for r in records if r.get("kind") in ("load", "rss") and r.get("tabs") == 1
            and (r.get("page") in (article, None) or r.get("kind") == "rss") and "rss_mb" in r]
    rss8 = [r["rss_mb"] for r in records if r.get("kind") in ("load", "rss") and r.get("tabs") == 8 and "rss_mb" in r]
    add("rss_one_tab_article_mb", None if not rss1 else max(rss1), b["rss_one_tab_article_mb"]["max"], "max", "MB")
    add("rss_eight_tabs_mb", None if not rss8 else max(rss8), b["rss_eight_tabs_mb"]["max"], "max", "MB")

    battery = [r["drop_percent"] for r in records if r.get("kind") == "battery" and r.get("minutes") == 30]
    add("battery_30min_reading_percent", median(battery), b["battery_30min_reading_percent"]["max"], "max", "%")
    return results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--budgets", default="tools/budgets.json")
    ap.add_argument("--panel-hz", type=int, default=None, help="override the panel refresh rate")
    ap.add_argument("--json", default=None, help="write the report as JSON")
    args = ap.parse_args()

    with open(args.budgets, "r", encoding="utf-8") as f:
        budgets = json.load(f)
    panel_hz = args.panel_hz or budgets.get("panel_hz", 60)
    results = evaluate(load_log(args.log), budgets, panel_hz)

    failed = 0
    nodata = 0
    print("%-36s %12s %12s  %s" % ("metric", "value", "limit", "result"))
    for r in results:
        if r["ok"] is None:
            status = "no data"
            nodata += 1
        elif r["ok"]:
            status = "ok"
        else:
            status = "FAIL"
            failed += 1
        value = "-" if r["value"] is None else str(r["value"])
        print("%-36s %12s %12s  %s (%s %s)" % (r["metric"], value, r["limit"], status, r["kind"], r["unit"]))
    print()
    print("panel: %d Hz, gate: %s, failed: %d, no data: %d" % (panel_hz, budgets.get("gate", "?"), failed, nodata))

    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump({"panel_hz": panel_hz, "results": results, "failed": failed, "nodata": nodata}, f, indent=2)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
