# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
"""Side-by-side inclusive shares of chosen functions in two StackSampler reports.

    python cmp_samples.py perf/S14_world_samples.txt perf/S15_world_samples.txt [substring ...]

Without substrings, lists every DreamGUI function whose inclusive share is at least 0.5% in either report.
"""
import re
import sys

LINE = re.compile(r'^\s+([\d.]+)% (\S.*?)(?:\s{4}<-.*)?$')


def inclusive(path):
    out = {}
    section = None
    for raw in open(path, encoding='utf-8', errors='replace'):
        line = raw.rstrip('\n')
        if line.startswith('== '):
            section = line[3:].strip()
            continue
        if section != 'inclusive':
            continue
        m = LINE.match(line)
        if m:
            out[m.group(2).strip()] = float(m.group(1))
    return out


a_path, b_path = sys.argv[1], sys.argv[2]
wanted = sys.argv[3:]
a, b = inclusive(a_path), inclusive(b_path)
names = set(a) | set(b)
if wanted:
    rows = [n for n in names if any(w.lower() in n.lower() for w in wanted)]
else:
    rows = [n for n in names if ('DreamGUI' in n) and max(a.get(n, 0), b.get(n, 0)) >= 0.5]
rows.sort(key=lambda n: -max(a.get(n, 0), b.get(n, 0)))
print('%8s %8s %8s  %s' % ('A%', 'B%', 'B-A', 'function'))
for n in rows:
    va, vb = a.get(n, 0.0), b.get(n, 0.0)
    print('%8.2f %8.2f %+8.2f  %s' % (va, vb, vb - va, n))
