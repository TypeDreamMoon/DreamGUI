# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""Frames of a CSV profile over a threshold, with their neighbours, and how the frame times are spread.

    python csv_spikes.py [csv file] [--dir=<csv folder>] [--over=20] [--columns=FrameTime,GameThreadTime]
"""
import csv
import glob
import os
import sys


def default_csv_dir():
    """<host project>/Saved/Profiling/CSV, from DREAMGUI_TEST_PROJECT."""
    project = os.environ.get("DREAMGUI_TEST_PROJECT", "")
    return os.path.join(os.path.dirname(project), "Saved", "Profiling", "CSV") if project else ""


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    over = 20.0
    columns = ["FrameTime", "GameThreadTime", "RenderThreadTime"]
    for a in sys.argv[1:]:
        if a.startswith("--over="):
            over = float(a.split("=", 1)[1])
        if a.startswith("--columns="):
            columns = a.split("=", 1)[1].split(",")
    csv_dir = default_csv_dir()
    for a in sys.argv[1:]:
        if a.startswith("--dir="):
            csv_dir = a.split("=", 1)[1]
    path = args[0] if args else max(glob.glob(os.path.join(csv_dir, "*.csv")), key=os.path.getmtime)
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader)
        rows = []
        for row in reader:
            if len(row) < 2 or not row[1].replace(".", "", 1).replace("-", "", 1).isdigit():
                if rows:
                    break
                continue
            rows.append(row)
    index = dict((name, i) for i, name in enumerate(header))
    cols = [c for c in columns if c in index]
    ft = [float(r[index["FrameTime"]]) for r in rows]
    print("%s: %d frames" % (os.path.basename(path), len(rows)))
    for limit in (16.7, 17.5, 20.0, 25.0, 33.3, 50.0):
        n = sum(1 for v in ft if v > limit)
        print("  frames over %5.1f ms: %4d (%.1f%%)" % (limit, n, 100.0 * n / max(1, len(ft))))
    total = sum(ft)
    over_time = sum(v - 16.67 for v in ft if v > 16.67)
    print("  time over the 16.67 ms budget: %.0f ms of %.0f ms" % (over_time, total))
    print("frame  " + "  ".join("%14s" % c[:14] for c in cols))
    shown = set()
    for i, v in enumerate(ft):
        if v > over:
            for j in range(max(0, i - 1), min(len(ft), i + 3)):
                if j in shown:
                    continue
                shown.add(j)
                print("%5d  " % j + "  ".join("%14.2f" % float(rows[j][index[c]]) for c in cols) + ("   <" if j == i else ""))


main()
