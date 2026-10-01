# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""Median per-frame times from the newest CSV profile of the test host (bench_launch.ps1 -Csv 1), or of --dir=<folder>.

    python csv_summary.py [csv file] [--dir=<csv folder>] [--columns=FrameTime,GameThreadTime,...]
"""
import csv
import glob
import os
import statistics
import sys


def default_csv_dir():
    """<host project>/Saved/Profiling/CSV, from DREAMGUI_TEST_PROJECT."""
    project = os.environ.get("DREAMGUI_TEST_PROJECT", "")
    return os.path.join(os.path.dirname(project), "Saved", "Profiling", "CSV") if project else ""
DEFAULT = ["FrameTime", "GameThreadTime", "RenderThreadTime", "RHIThreadTime", "GPUTime", "RenderThreadTime_CriticalPath",
           "GameThreadTime_CriticalPath", "RHIThreadTime_CriticalPath"]


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    columns = DEFAULT
    for a in sys.argv[1:]:
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
            # The first column holds events and is mostly empty; the profiler ends with the header again and metadata rows.
            if len(row) < 2 or not row[1].replace(".", "", 1).replace("-", "", 1).isdigit():
                if rows:
                    break
                continue
            rows.append(row)
    print("%s: %d frames" % (os.path.basename(path), len(rows)))
    index = dict((name, i) for i, name in enumerate(header))
    for name in columns:
        if name not in index:
            continue
        values = []
        for row in rows:
            try:
                values.append(float(row[index[name]]))
            except (ValueError, IndexError):
                pass
        if values:
            values.sort()
            print("  %-34s median %7.2f  avg %7.2f  p95 %7.2f" % (name, values[len(values) // 2], statistics.mean(values),
                                                                  values[min(len(values) - 1, int(len(values) * 0.95))]))
    others = [h for h in header if "GPU" in h and h not in columns]
    if others:
        print("  other GPU columns:", ", ".join(others[:30]))


if __name__ == "__main__":
    main()
