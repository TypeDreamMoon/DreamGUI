"""The DreamGUI benchmark's numbers (DreamGUI.Performance.*, the Perf preset), for comparing two builds on one machine.

    python perf_report.py show     <Benchmark.json | directory holding it>
    python perf_report.py compare  <old Benchmark.json | dir> <new Benchmark.json | dir>
    python perf_report.py insights <Benchmark.utrace> [--engine <UE root>] [--out <dir>] [--top N]

show prints each stretch of frames the benchmark timed: milliseconds per frame for every stage DreamGUI counts, and
what it drew and uploaded per frame. compare puts two runs side by side with the change in per cent; a time is only
worth anything next to another taken on the same machine. insights has Unreal Insights export the CPU timers of the
trace the benchmark wrote, one file per timed stretch and thread, and prints the DreamGUI scopes and the heaviest
timers of each -- where the time inside a stage went.
"""
import argparse
import csv
import glob
import json
import os
import subprocess
import sys

STAGES = ['ManagerTick', 'CanvasUpdate', 'Batching', 'DrawCallSubmit', 'RenderRecord']
COUNTERS = ['BatchesRecorded', 'VerticesRecorded', 'SectionUploads', 'UploadedBytes', 'DataTextureUpdates',
            'GeometryCopies', 'SectionReuses', 'WidgetsUpdated']
DEFAULT_ENGINE = os.environ.get('DREAMGUI_ENGINE', r'C:\Program Files\Epic Games\UE_5.8')
THREADS = [('GameThread', 'GameThread'), ('RenderThread', 'RenderThread*'), ('Workers', '*Worker*')]


def load(path):
    if os.path.isdir(path):
        path = os.path.join(path, 'Benchmark.json')
    with open(path, encoding='utf-8-sig') as f:
        return json.load(f)


def show(report):
    scene = report.get('scene', {})
    print('scene: %s canvas(es), %s widget(s), %s-pixel targets, %s frame(s) a stretch'
          % (scene.get('canvases'), scene.get('widgets'), scene.get('targetExtent'), scene.get('timedFrames')))
    for phase in report.get('phases', []):
        print('\n%s  (%d frame(s), %.2f ms/frame wall, %d draw call(s))'
              % (phase['name'], phase.get('frames', 0), phase.get('wallMsPerFrame', 0.0), phase.get('drawCalls', 0)))
        for stage in STAGES:
            print('  %-16s %9.3f ms/frame   %8d run(s)' % (stage, phase['msPerFrame'].get(stage, 0.0), phase['runs'].get(stage, 0)))
        for counter in COUNTERS:
            print('  %-18s %12.1f /frame' % (counter, phase['perFrame'].get(counter, 0.0)))
    if report.get('trace'):
        print('\ntrace: %s' % report['trace'])


def change(old, new):
    if old == 0:
        return '    n/a' if new == 0 else '    new'
    return '%+6.1f%%' % ((new - old) * 100.0 / old)


def compare(old, new):
    old_phases = dict((p['name'], p) for p in old.get('phases', []))
    for phase in new.get('phases', []):
        before = old_phases.get(phase['name'])
        if before is None:
            print('\n%s: not in the old run' % phase['name'])
            continue
        print('\n%s                     old         new    change' % phase['name'])
        rows = [('wall ms/frame', before.get('wallMsPerFrame', 0.0), phase.get('wallMsPerFrame', 0.0))]
        rows += [(s + ' ms', before['msPerFrame'].get(s, 0.0), phase['msPerFrame'].get(s, 0.0)) for s in STAGES]
        rows += [(c + ' /f', before['perFrame'].get(c, 0.0), phase['perFrame'].get(c, 0.0)) for c in COUNTERS]
        rows += [('draw calls', before.get('drawCalls', 0), phase.get('drawCalls', 0))]
        for name, a, b in rows:
            print('  %-22s %10.3f  %10.3f  %s' % (name, a, b, change(a, b)))


def read_timers(path):
    """(header, rows) of an exported timer statistics file; rows as dicts."""
    with open(path, encoding='utf-8-sig', newline='') as f:
        sample = f.read(4096)
        f.seek(0)
        delimiter = '\t' if sample.count('\t') > sample.count(',') else ','
        reader = csv.DictReader(f, delimiter=delimiter)
        return reader.fieldnames or [], list(reader)


def pick(header, *words):
    """The first column whose name contains every word (any case)."""
    for name in header:
        low = name.lower()
        if all(w in low for w in words):
            return name
    return None


def number(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return 0.0


def insights(trace, engine, out, top):
    exe = os.path.join(engine, 'Engine', 'Binaries', 'Win64', 'UnrealInsights.exe')
    if not os.path.isfile(exe):
        sys.exit('no Unreal Insights at %s' % exe)
    if not os.path.isfile(trace):
        sys.exit('no trace at %s' % trace)
    out = os.path.abspath(out or os.path.join(os.path.dirname(os.path.abspath(trace)), 'Insights'))
    os.makedirs(out, exist_ok=True)
    for old in glob.glob(os.path.join(out, '*.csv')):
        os.remove(old)
    rsp = os.path.join(out, 'export.rsp')
    with open(rsp, 'w', encoding='utf-8') as f:
        for label, pattern in THREADS:
            # Forward slashes: Insights reads the command with escapes on, and a backslash would be taken for one.
            target = os.path.join(out, '{region}_%s.csv' % label).replace(os.sep, '/')
            f.write('TimingInsights.ExportTimerStatistics "%s" -threads="%s" -region="DreamGUI.Benchmark.*" -sortBy=TotalInclusiveTime\n'
                    % (target, pattern))
    log = os.path.join(out, 'insights.log')
    # One string, not a list: the engine reads -Name="value", and a path with spaces has to keep its quotes inside it.
    command = '"%s" -OpenTraceFile="%s" -ABSLOG="%s" -AutoQuit -NoUI -ExecOnAnalysisCompleteCmd="@=%s" -log' % (
        exe, os.path.abspath(trace), log, rsp)
    subprocess.run(command, timeout=3600)
    files = sorted(glob.glob(os.path.join(out, '*.csv')))
    if not files:
        sys.exit('Unreal Insights exported nothing; see %s' % log)
    for path in files:
        header, rows = read_timers(path)
        name_col = pick(header, 'name') or (header[0] if header else None)
        count_col = pick(header, 'count')
        incl_col = pick(header, 'incl') or pick(header, 'inclusive')
        excl_col = pick(header, 'excl') or pick(header, 'exclusive')
        print('\n== %s  (%d timer(s))' % (os.path.basename(path), len(rows)))
        if not rows or name_col is None or incl_col is None:
            print('   columns: %s' % ', '.join(header))
            continue
        rows.sort(key=lambda r: number(r.get(incl_col)), reverse=True)
        shown = set()
        dream = [r for r in rows if 'dreamui' in (r.get(name_col) or '').lower() or 'dreamgui' in (r.get(name_col) or '').lower()]
        for title, chosen in (('DreamGUI scopes', dream), ('heaviest timers', rows[:top])):
            print('   %s:' % title)
            for r in chosen:
                key = (title, r.get(name_col))
                if key in shown:
                    continue
                shown.add(key)
                print('     %-56s incl %10.3f  excl %10.3f  count %8d'
                      % ((r.get(name_col) or '')[:56], number(r.get(incl_col)), number(r.get(excl_col)) if excl_col else 0.0,
                         int(number(r.get(count_col))) if count_col else 0))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('show')
    p.add_argument('report')
    p = sub.add_parser('compare')
    p.add_argument('old')
    p.add_argument('new')
    p = sub.add_parser('insights')
    p.add_argument('trace')
    p.add_argument('--engine', default=DEFAULT_ENGINE)
    p.add_argument('--out')
    p.add_argument('--top', type=int, default=25)
    args = parser.parse_args(argv)
    if args.command == 'show':
        show(load(args.report))
    elif args.command == 'compare':
        compare(load(args.old), load(args.new))
    else:
        insights(args.trace, args.engine, args.out, args.top)
    return 0


if __name__ == '__main__':
    sys.exit(main())
