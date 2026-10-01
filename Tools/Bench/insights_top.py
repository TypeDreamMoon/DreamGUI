# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""Export a trace's CPU timers with Unreal Insights and print the heaviest per frame.

    python insights_top.py <trace.utrace> <frames> [--top N] [--threads GameThread,RenderThread*]
"""
import argparse
import csv
import os
import subprocess
import sys

ENGINE = os.environ.get("DREAMGUI_ENGINE", r"C:\Program Files\Epic Games\UE_5.8")


def export(trace, threads, region=None):
    out = os.path.splitext(trace)[0] + "_export"
    os.makedirs(out, exist_ok=True)
    rsp = os.path.join(out, "cmds.rsp")
    files = {}
    with open(rsp, "w", encoding="utf-8") as f:
        for thread in threads:
            label = thread.replace("*", "")
            name = ("{region}_" + label) if region else label
            target = os.path.join(out, name + ".csv").replace(os.sep, "/")
            files[thread] = target.replace("{region}", region.replace("*", "")) if region else target
            region_arg = (' -region="%s"' % region) if region else ""
            f.write('TimingInsights.ExportTimerStatistics "%s" -threads="%s"%s -sortBy=TotalInclusiveTime\n' % (target, thread, region_arg))
    exe = os.path.join(ENGINE, "Engine", "Binaries", "Win64", "UnrealInsights.exe")
    log = os.path.join(out, "insights.log")
    command = '"%s" -OpenTraceFile="%s" -ABSLOG="%s" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@=%s" -log' % (
        exe, os.path.abspath(trace), log, rsp)
    subprocess.run(command, timeout=3600)
    return files


def read(path):
    rows = []
    with open(path, encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            rows.append(row)
    return rows


def number(row, *keys):
    for key in keys:
        if key in row and row[key] not in ("", None):
            try:
                return float(row[key])
            except ValueError:
                pass
    return 0.0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("frames", type=float, help="0: the FEngineLoop::Tick count of the game thread")
    parser.add_argument("--top", type=int, default=40)
    parser.add_argument("--threads", default="GameThread,RenderThread*")
    parser.add_argument("--no-export", action="store_true")
    parser.add_argument("--region", default=None, help="an exact region name, e.g. DreamPerf_PIE")
    args = parser.parse_args()
    threads = args.threads.split(",")
    if args.no_export:
        out = os.path.splitext(args.trace)[0] + "_export"
        prefix = (args.region + "_") if args.region else ""
        files = dict((t, os.path.join(out, prefix + t.replace("*", "") + ".csv")) for t in threads)
    else:
        files = export(args.trace, threads, args.region)
    for thread, path in files.items():
        if not os.path.isfile(path):
            print("no export for %s at %s" % (thread, path))
            continue
        rows = read(path)
        if not rows:
            print("empty export for %s" % thread)
            continue
        frames = args.frames or next((number(r, "Count") for r in rows if r.get("Name") == "FEngineLoop::Tick"), 0) or 1
        print("\n=== %s (%d timers) ms/frame over %.0f frames ===" % (thread, len(rows), frames))
        print("%-70s %10s %10s %10s" % ("timer", "incl", "excl", "count/f"))
        key_incl = ["Incl"]
        key_excl = ["Excl"]
        name_key = "Name" if "Name" in rows[0] else list(rows[0].keys())[0]
        for label, sort_key in (("by inclusive", key_incl[0]), ("by exclusive", key_excl[0])):
            print("-- %s --" % label)
            for row in sorted(rows, key=lambda r: -number(r, sort_key))[:args.top]:
                incl = number(row, key_incl[0]) * 1000.0 / frames
                excl = number(row, key_excl[0]) * 1000.0 / frames
                count = number(row, "Count") / frames
                print("%-70s %10.3f %10.3f %10.1f" % (row[name_key][:70], incl, excl, count))


if __name__ == "__main__":
    main()
