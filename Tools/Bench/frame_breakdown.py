# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""Break the slowest game-thread frames of a trace down by timer, one frame at a time.

    python frame_breakdown.py <trace.utrace> [--frames N] [--region NAME] [--top K] [--min-ms X]

Pass 1 exports the game thread's FEngineLoop::Tick events (one per frame); the N slowest (inside the region, if one is
named) are picked. Pass 2 exports timer statistics for each picked frame's own time span, and the heaviest timers of
each frame are printed, inclusive, with their count.
"""
import argparse
import csv
import os
import subprocess

ENGINE = os.environ.get("DREAMGUI_ENGINE", r"C:\Program Files\Epic Games\UE_5.8")
INSIGHTS = os.path.join(ENGINE, "Engine", "Binaries", "Win64", "UnrealInsights.exe")


def run_insights(trace, out, commands, tag):
    rsp = os.path.join(out, tag + ".rsp")
    with open(rsp, "w", encoding="utf-8") as f:
        for command in commands:
            f.write(command + "\n")
    log = os.path.join(out, tag + ".log")
    command = '"%s" -OpenTraceFile="%s" -ABSLOG="%s" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@=%s" -log' % (
        INSIGHTS, os.path.abspath(trace), log, rsp)
    subprocess.run(command, timeout=3600)
    return log


def read_rows(path):
    with open(path, encoding="utf-8-sig") as f:
        return list(csv.DictReader(f))


def fwd(path):
    return path.replace(os.sep, "/")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("trace")
    parser.add_argument("--frames", type=int, default=4)
    parser.add_argument("--region", default=None)
    parser.add_argument("--top", type=int, default=45)
    parser.add_argument("--min-ms", type=float, default=0.3)
    parser.add_argument("--thread", default="GameThread")
    parser.add_argument("--after", type=float, default=0.0, help="only frames starting this many seconds after the first")
    parser.add_argument("--max-ms", type=float, default=1000.0, help="leave out frames longer than this (loading)")
    args = parser.parse_args()

    out = os.path.splitext(os.path.abspath(args.trace))[0] + "_frames"
    os.makedirs(out, exist_ok=True)
    ticks = os.path.join(out, "ticks.csv")
    regions = os.path.join(out, "regions.csv")
    commands = ['TimingInsights.ExportTimingEvents "%s" -threads="%s" -timers="FEngineLoop::Tick" '
                '-columns=ThreadName,TimerName,StartTime,EndTime,Duration,Depth' % (fwd(ticks), args.thread)]
    if args.region:
        commands.append('TimingInsights.ExportTimerStatistics "%s" -threads="%s" -timers="FEngineLoop::Tick" -region="%s"'
                        % (fwd(regions), args.thread, args.region))
    log = run_insights(args.trace, out, commands, "pass1")
    if not os.path.exists(ticks):
        print("no tick export; see", log)
        return
    rows = [r for r in read_rows(ticks) if r.get("TimerName", "").startswith("FEngineLoop::Tick")]
    frames = sorted(((float(r["StartTime"]), float(r["EndTime"])) for r in rows), key=lambda t: t[0])
    print("%d frames in the trace" % len(frames))
    first = frames[0][0] if frames else 0.0
    candidates = [f for f in frames if f[0] - first >= args.after and (f[1] - f[0]) * 1000.0 <= args.max_ms]
    slow = sorted(candidates, key=lambda t: t[1] - t[0], reverse=True)
    print("slowest frames (s after first, ms):", ", ".join("%.2f:%.1f" % (f[0] - first, (f[1] - f[0]) * 1000.0) for f in slow[:16]))
    picked = slow[:args.frames]
    picked.sort()
    durations = sorted((e - s) * 1000.0 for s, e in frames)
    if durations:
        print("frame ms: median %.2f, p95 %.2f, max %.2f" % (durations[len(durations) // 2],
              durations[int(len(durations) * 0.95)], durations[-1]))

    commands = []
    files = []
    for index, (start, end) in enumerate(picked):
        target = os.path.join(out, "frame%d.csv" % index)
        files.append(target)
        commands.append('TimingInsights.ExportTimerStatistics "%s" -threads="%s" -startTime=%.6f -endTime=%.6f -sortBy=TotalInclusiveTime'
                        % (fwd(target), args.thread, start, end))
    run_insights(args.trace, out, commands, "pass2")
    for index, ((start, end), path) in enumerate(zip(picked, files)):
        print()
        print("=== frame at %.3f s: %.2f ms" % (start, (end - start) * 1000.0))
        if not os.path.exists(path):
            print("  (no export)")
            continue
        # Insights writes Name, Count, Incl, Excl (seconds); the thread filter does not hold for every timer, so the
        # render thread's scopes may be listed too -- read the names.
        stats = sorted(read_rows(path), key=lambda r: -float(r.get("Incl", "0") or 0))
        shown = 0
        for row in stats:
            name = row.get("Name") or row.get("Timer") or "?"
            incl = float(row.get("Incl", "0") or 0) * 1000.0
            excl = float(row.get("Excl", "0") or 0) * 1000.0
            count = row.get("Count", "")
            if incl < args.min_ms:
                continue
            print("  %8.2f ms incl %8.2f excl  x%-6s %s" % (incl, excl, count, name))
            shown += 1
            if shown >= args.top:
                break


main()
