"""The coverage matrix: for every interactive control or behaviour, which inputs under which
configurations some driver test exercises -- and where the holes are.

    python coverage_matrix.py [--root <plugin>] [--heuristic] [--out <file.md>] [--write-doc]
                              [--json <file>] [--fail-on-holes]

Rows come from the UCLASSes of Source/DreamGUI/Public/Controls/*.h and Public/Interaction/*.h,
grouped and filtered by coverage.json (a class in neither its rows nor its exclusions becomes an
"unclassified" row of holes). Columns are the inputs (mouse, touch, navigation, keyboard text)
crossed with the configurations (default and animated, disabled, scaled canvas, world space).

A test says which cells it covers with the engine's tag registration, next to its declaration:

    REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FMyTest, "DreamGUI.Button.ATapClicksIt", "[Touch][Animated]")

and belongs to the rows whose test-name areas its name starts with (coverage.json). COVERAGE.md
has the convention. Tags are the only thing that counts, unless --heuristic: then a test without
tags is guessed from what its body calls (Click(, TouchDown(, Navigate(, Type(, a scaled canvas, a
world-space camera, SetIsEnabled(false), ...), only if it drives the pointer through the driver at
all, and every guessed cell is marked as a guess. With --heuristic a guess closes a hole in the
count; without it, it does not.

--write-doc replaces the generated part of COVERAGE.md (between its coverage-matrix markers).
--fail-on-holes exits 1 when any applicable cell has no test, for wiring into a gate later.
"""

import argparse
import collections
import json
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import sourcescan  # noqa: E402

DEFAULT_COVERAGE = os.path.join(HERE, 'coverage.json')
DOC = os.path.join(HERE, 'COVERAGE.md')
BEGIN_MARK = '<!-- coverage-matrix:begin -->'
END_MARK = '<!-- coverage-matrix:end -->'
CLASS_DIRS = ('Source/DreamGUI/Public/Controls', 'Source/DreamGUI/Public/Interaction')

UCLASS = re.compile(r'\bUCLASS\s*\(((?:[^()]|\((?:[^()]|\([^()]*\))*\))*)\)\s*class\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_0-9]+)')

# What a body calls, for --heuristic. The driver's own vocabulary (Private/Driver/*.h).
DRIVER = re.compile(r'\bFDreamDriverRig\b|\bFDreamDriverSequence\b|\bFDreamDriverElement\b|\bFDreamDriverPieRig\b|\bFDreamBy\s*::')
GUESS_INPUT = collections.OrderedDict([
    ('Pointer', re.compile(r'\b(?:MoveTo|MoveToPixel|MoveBy|Press|Release|Click|DoubleClick|DragTo|DragBy|ScrollBy|Hover|LongPress|Hold)\s*\(')),
    ('Touch', re.compile(r'\b(?:TouchDown|TouchMoveTo|TouchUp|Tap|TouchDragBy)\s*\(|\bEDreamDriverTouchPhase\b')),
    ('Nav', re.compile(r'\b(?:Navigate|NavigationTrigger|Back|ActivateVirtualCursor|VirtualCursorStick|VirtualCursorPress|VirtualCursorRelease)\s*\(')),
    ('Text', re.compile(r'\b(?:Type|TypeChord)\s*\(|\bHandleCharacterInput\s*\(')),
])
GUESS_CONFIG = collections.OrderedDict([
    ('Disabled', re.compile(r'\bSetIsEnabled\s*\(\s*false\b|\bSetInteractable\s*\(\s*false\b|\bbInteractable\s*=\s*false\b')),
    ('Scaled', re.compile(r'\bCanvasScaleMode\b|\bReferenceResolution\b|\bEDreamCanvasScaleMode\b|\bMatchFromWidthToHeight\b')),
    ('World', re.compile(r'\bWorldSpace\w*|\bDreamDriverWorld\b|\bFDreamDriverVirtualCamera\b|\bRenderTargetMesh\w*|\bUDreamWorldSpaceRaycaster\b')),
])
# A test that switches its control's transitions or animations off to make its assertions easy does
# not cover the default configuration.
ANIMATION_OFF = re.compile(r'\b\w*Duration\s*=\s*0(?:\.0*)?f?\b|\bSetTransitionDuration\s*\(\s*0|\bbAnimate\w*\s*=\s*false\b|\bSetAnimat\w*\s*\(\s*false\b')


def load_config(path):
    data = sourcescan.read_json(path)
    inputs = data.get('inputs') or []
    configs = data.get('configs') or []
    if not inputs or not configs:
        raise ValueError('%s needs "inputs" and "configs"' % path)
    return data, inputs, configs


def find_classes(root):
    found = collections.OrderedDict()
    for d in CLASS_DIRS:
        for path in sourcescan.source_files(root, d, ('.h',)):
            sf = sourcescan.load(root, path)
            for m in UCLASS.finditer(sf.code):
                found[m.group(2)] = sf.rel
    return found


def derived_area(cls):
    """UDreamSpinBox -> SpinBox, UUIScrollbar -> Scrollbar, UDreamUITooltipSubsystem -> TooltipSubsystem."""
    name = cls[1:] if cls[:1] in ('U', 'A') else cls
    for prefix in ('DreamUI', 'Dream', 'UI'):
        if name.startswith(prefix) and len(name) > len(prefix) and name[len(prefix)].isupper():
            return name[len(prefix):]
    return name


def build_rows(data, classes):
    rows = []
    claimed = {}
    for r in data.get('rows') or []:
        row = {'id': r['id'], 'classes': list(r.get('classes') or []), 'areas': list(r.get('areas') or [r['id']]),
               'text': bool(r.get('text')), 'na': dict(r.get('na') or {}), 'note': r.get('note', ''),
               'unclassified': False, 'missing': []}
        for c in row['classes']:
            claimed[c] = row['id']
            if c not in classes:
                row['missing'].append(c)
        rows.append(row)
    excluded = dict((e['class'], e.get('reason', '')) for e in (data.get('excluded') or []) if e.get('class'))
    stale_excluded = sorted(c for c in excluded if c not in classes)
    for cls, rel in classes.items():
        if cls in claimed or cls in excluded:
            continue
        area = derived_area(cls)
        rows.append({'id': area, 'classes': [cls], 'areas': [area], 'text': False, 'na': {},
                     'note': 'unclassified: %s (%s) is in neither the rows nor the exclusions of coverage.json' % (cls, rel),
                     'unclassified': True, 'missing': []})
    return rows, excluded, stale_excluded


def row_matches(row, path):
    for area in row['areas']:
        prefix = 'DreamGUI.' + area
        if path == prefix or path.startswith(prefix + '.'):
            return True
    return False


def applicable(row, inp, cfg):
    """None when the cell applies, else why it does not."""
    if inp.get('textRowsOnly') and not row['text']:
        return 'not a text control'
    na = row['na']
    for key in (inp['id'] + '.' + cfg['id'], inp['id'], cfg['id']):
        if key in na:
            return na[key]
    return None


def classify(test, inputs, configs, heuristic):
    """(input ids, config ids, how) for one test: from its tags, else guessed, else nothing."""
    tag_to_input = dict((i['tag'], i['id']) for i in inputs)
    tag_to_config = dict((c['tag'], c['id']) for c in configs)
    if test.tags:
        ins = [tag_to_input[t] for t in test.tags if t in tag_to_input]
        cfs = [tag_to_config[t] for t in test.tags if t in tag_to_config]
        if ins and cfs:
            return ins, cfs, 'tag'
        return [], [], None
    if not heuristic or not test.body or not sourcescan.body_reaches(test, DRIVER):
        return [], [], None
    ins = [name for name, rx in GUESS_INPUT.items() if sourcescan.body_reaches(test, rx)]
    cfs = [name for name, rx in GUESS_CONFIG.items() if sourcescan.body_reaches(test, rx)]
    if not cfs and not sourcescan.body_reaches(test, ANIMATION_OFF):
        cfs = ['Default']
    known_inputs = set(i['id'] for i in inputs)
    known_configs = set(c['id'] for c in configs)
    ins = [i for i in ins if i in known_inputs]
    cfs = [c for c in cfs if c in known_configs]
    if ins and cfs:
        return ins, cfs, 'guess'
    return [], [], None


def compute(root, data, inputs, configs, heuristic):
    classes = find_classes(root)
    rows, excluded, stale_excluded = build_rows(data, classes)
    scan = sourcescan.scan(root)
    cells = {}   # (row id, input id, config id) -> {'tag': [paths], 'guess': [paths]}
    unmapped_tagged = []
    for t in scan.named():
        ins, cfs, how = classify(t, inputs, configs, heuristic)
        if not how:
            continue
        hit = [r for r in rows if row_matches(r, t.path)]
        if not hit and how == 'tag':
            unmapped_tagged.append(t.path)
        for r in hit:
            for i in ins:
                for c in cfs:
                    cell = cells.setdefault((r['id'], i, c), {'tag': [], 'guess': []})
                    cell[how].append(t.path)
    holes, guessed_only = [], []
    for r in rows:
        for inp in inputs:
            for cfg in configs:
                if applicable(r, inp, cfg) is not None:
                    continue
                cell = cells.get((r['id'], inp['id'], cfg['id']))
                if not cell or not cell['tag']:
                    if cell and cell['guess']:
                        guessed_only.append((r['id'], inp['id'], cfg['id']))
                    else:
                        holes.append((r['id'], inp['id'], cfg['id']))
    return {'rows': rows, 'cells': cells, 'holes': holes, 'guessed_only': guessed_only, 'excluded': excluded,
            'stale_excluded': stale_excluded, 'unmapped_tagged': sorted(set(unmapped_tagged)), 'classes': classes}


def render(result, inputs, configs, heuristic):
    rows, cells = result['rows'], result['cells']
    counted_holes = len(result['holes']) + (0 if heuristic else len(result['guessed_only']))
    L = []
    L.append('Generated by `python Tools/Tests/coverage_matrix.py%s`.' % (' --heuristic' if heuristic else ''))
    L.append('')
    L.append('Each input column holds one mark per configuration, in this order: %s.'
             % ', '.join('%s (`[%s]`)' % (c['id'], c['tag']) for c in configs))
    L.append('`#` a tagged test covers it, `?` only a guess from the test body%s, `-` not applicable, `.` a hole.'
             % (' (counted as covered in this heuristic view)' if heuristic else ' (not counted)'))
    L.append('')
    head = '| Row | ' + ' | '.join('%s (`[%s]`)' % (i['title'], i['tag']) for i in inputs) + ' | Holes |'
    L.append(head)
    L.append('|---|' + '|'.join(':---:' for _ in inputs) + '|---:|')
    for r in rows:
        marks = []
        row_holes = 0
        for inp in inputs:
            s = ''
            for cfg in configs:
                if applicable(r, inp, cfg) is not None:
                    s += '-'
                    continue
                cell = cells.get((r['id'], inp['id'], cfg['id']))
                if cell and cell['tag']:
                    s += '#'
                elif cell and cell['guess']:
                    s += '?'
                    if not heuristic:
                        row_holes += 1
                else:
                    s += '.'
                    row_holes += 1
            marks.append('`%s`' % s)
        name = r['id'] + (' (unclassified)' if r['unclassified'] else '')
        L.append('| %s | %s | %d |' % (name, ' | '.join(marks), row_holes))
    L.append('')
    L.append('**Holes: %d** of %d applicable cells%s.' % (
        counted_holes, sum(1 for r in rows for i in inputs for c in configs if applicable(r, i, c) is None),
        (' (%d more are only guessed)' % len(result['guessed_only'])) if heuristic and result['guessed_only'] else ''))
    L.append('')
    notes = [r for r in rows if r['note'] or r['missing']]
    if notes:
        L.append('Notes:')
        L.append('')
        for r in notes:
            if r['note']:
                L.append('- %s: %s' % (r['id'], r['note']))
            if r['missing']:
                L.append('- %s: coverage.json names classes that no longer exist: %s' % (r['id'], ', '.join(r['missing'])))
        L.append('')
    if result['stale_excluded']:
        L.append('- coverage.json excludes classes that no longer exist: %s' % ', '.join(result['stale_excluded']))
        L.append('')
    if result['unmapped_tagged']:
        L.append('Tagged tests whose name matches no row (add the area to a row in coverage.json):')
        L.append('')
        for p in result['unmapped_tagged'][:40]:
            L.append('- `%s`' % p)
        L.append('')
    return '\n'.join(L), counted_holes


def write_doc(text):
    with open(DOC, 'r', encoding='utf-8') as f:
        doc = f.read()
    a, b = doc.find(BEGIN_MARK), doc.find(END_MARK)
    if a < 0 or b < a:
        raise ValueError('%s has no %s ... %s section' % (DOC, BEGIN_MARK, END_MARK))
    doc = doc[:a + len(BEGIN_MARK)] + '\n' + text + '\n' + doc[b:]
    with open(DOC, 'w', encoding='utf-8', newline='\n') as f:
        f.write(doc)


def main(argv=None):
    sourcescan.console_safe()
    ap = argparse.ArgumentParser(description='Draw the coverage matrix (see the module docstring).')
    ap.add_argument('--root', default=None, help='plugin root (default: the checkout holding this script)')
    ap.add_argument('--config', default=DEFAULT_COVERAGE, help='coverage.json')
    ap.add_argument('--heuristic', action='store_true', help='guess untagged tests from their bodies')
    ap.add_argument('--out', default=None, help='write the markdown table here instead of printing it')
    ap.add_argument('--write-doc', action='store_true', help='replace the generated section of COVERAGE.md')
    ap.add_argument('--json', default=None, help='also write the cells, holes and guesses as JSON here')
    ap.add_argument('--fail-on-holes', action='store_true', help='exit 1 when any applicable cell is a hole')
    ns = ap.parse_args(argv)
    try:
        root = sourcescan.plugin_root(ns.root)
        data, inputs, configs = load_config(ns.config)
        result = compute(root, data, inputs, configs, ns.heuristic)
        text, holes = render(result, inputs, configs, ns.heuristic)
        if ns.out:
            with open(ns.out, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text + '\n')
        if ns.write_doc:
            write_doc(text)
        if not ns.out and not ns.write_doc:
            print(text)
        if ns.json:
            payload = {
                'heuristic': ns.heuristic,
                'holes': [list(h) for h in result['holes']],
                'guessedOnly': [list(h) for h in result['guessed_only']],
                'cells': dict(('%s|%s|%s' % k, v) for k, v in sorted(result['cells'].items())),
                'unclassified': [r['classes'][0] for r in result['rows'] if r['unclassified']],
            }
            with open(ns.json, 'w', encoding='utf-8', newline='\n') as f:
                json.dump(payload, f, indent=1)
    except (OSError, ValueError, KeyError) as e:
        sys.stderr.write('coverage_matrix: %s\n' % e)
        return 2
    sys.stderr.write('coverage: %d hole(s)%s\n' % (holes, ' (heuristic)' if ns.heuristic else ''))
    return 1 if (ns.fail_on_holes and holes) else 0


if __name__ == '__main__':
    sys.exit(main())
