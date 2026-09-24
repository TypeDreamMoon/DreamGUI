"""Turns one editor run of the automation suite into a verdict, a one-page summary and a line per
test in the history.

    python digest.py tests <run dir> [--no-history]
    python digest.py build <build log>

A run directory is what Invoke-DreamGUITests.ps1 leaves per editor run:

    run-info.json   what was asked for (preset, filter, arguments, floor, commit, exit code, ...)
    index.json      the engine's report, written by -ReportExportPath
    run.log         the editor log, written by -abslog

The verdict comes from index.json: it is the engine's own account of every test, written by
FAutomationControllerManager::ProcessResults once the last test has finished (so a run that died
part-way leaves none). run.log is read only for what the report cannot say: ensures, crashes, a
plugin that failed to load, a filter that matched nothing, a queue that never emptied.

index.json fields, as serialized by FJsonObjectConverter::UStructToJsonObjectString from
FAutomatedTestPassResults / FAutomatedTestResult (AutomationControllerManager.h) and
FAutomationExecutionEntry / FAutomationEvent (NoExportTypes.h): StandardizeCase lowers the first
letter of every UPROPERTY name, and enums are written by name.

    succeeded, succeededWithWarnings, failed, notRun, inProcess, totalDuration, reportCreatedOn,
    devices[], tests[]: testDisplayName, fullTestPath, tags[], state (NotRun | InProcess | Fail |
    Success | Skipped), deviceInstance[], duration (seconds), dateTime, warnings, errors,
    entries[]: event {type (Info | Warning | Error), message, context, artifact}, filename,
    lineNumber, timestamp

Exit codes: 0 green (known issues aside), 1 tests red, 2 the run cannot be trusted -- a crash, an
ensure, a timeout, a missing report, fewer tests than the floor, or declared tests that should
have run and did not.
"""

import argparse
import collections
import csv
import datetime
import fnmatch
import json
import os
import re
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import sourcescan  # noqa: E402

# EAutomationState (Runtime/AutomationTest/Public/AutomationState.h), in declaration order, for a
# report that ever carries the numeric value instead of the name.
STATES = ('NotRun', 'InProcess', 'Fail', 'Success', 'Skipped')
EVENT_TYPES = ('Info', 'Warning', 'Error')
HISTORY_FIELDS = ['timestamp', 'run', 'commit', 'dirty', 'preset', 'test', 'result', 'duration_s']
SHOW = 25


def g(d, name, default=None):
    """A report field by its serialized (first letter lowered) name, tolerating the PascalCase one."""
    if not isinstance(d, dict):
        return default
    if name in d:
        return d[name]
    return d.get(name[:1].upper() + name[1:], default)


def state_of(test):
    s = g(test, 'state')
    if isinstance(s, int) and 0 <= s < len(STATES):
        return STATES[s]
    return str(s) if s is not None else 'NotRun'


def event_type(event):
    t = g(event, 'type')
    if isinstance(t, int) and 0 <= t < len(EVENT_TYPES):
        return EVENT_TYPES[t]
    return str(t) if t is not None else 'Info'


def short_path(path):
    """A source path from the report, cut to where the plugin (or the engine) starts."""
    p = (path or '').replace('\\', '/')
    for marker in ('/Plugins/DreamGUI/', '/Engine/Source/', '/Engine/Plugins/'):
        k = p.find(marker)
        if k >= 0:
            return p[k + 1:] if marker != '/Plugins/DreamGUI/' else p[k + len(marker):]
    return p


def hex_code(code):
    try:
        return '0x%08X' % (int(code) & 0xFFFFFFFF)
    except (TypeError, ValueError):
        return str(code)


# ------------------------------------------------------------------------------------------------
# run.log
# ------------------------------------------------------------------------------------------------

_STARTED = re.compile(r'Test Started\. Name=\{(.*?)\} Path=\{(.*?)\}')
_COMPLETED = re.compile(r'Test Completed\. Result=\{(\w+)\} Name=\{(.*?)\} Path=\{(.*?)\}')
_BEGIN_EVENTS = re.compile(r'LogAutomationController: BeginEvents: (.*)$')
_END_EVENTS = re.compile(r'LogAutomationController: EndEvents: ')
_EVENT_ERROR = re.compile(r'LogAutomationController: Error: (.*)$')
_FOUND = re.compile(r'Found (\d+) automation tests based on')
_PERFORMED = re.compile(r'Automation Test Queue Empty (\d+) tests performed')
_NO_MATCH = re.compile(r'No automation tests matched')
# The Windows crash handler's own lines (LaunchWindows / WindowsPlatformCrashContext): the first
# of them names the kind of death, the [Callstack] lines after it say where.
_CRASH = re.compile(r'=== Critical error: ===|Fatal error!|Unhandled Exception:|Assertion failed:')
_ENSURE = re.compile(r'Ensure condition failed:\s*(.*)$')
_PLUGIN_ERROR = re.compile(r'LogPluginManager: Error: (.*)$')
_EXIT_REASON = re.compile(r'Engine exit requested \(reason: (.*?)\)')
_TIMESTAMP = re.compile(r'^\[[^\]]*\]\[[^\]]*\]')


def read_log(path):
    facts = {
        'exists': os.path.isfile(path),
        'found': None, 'performed': None, 'queue_empty': False, 'no_match': False,
        'ensures': [], 'crash': None, 'plugin_errors': [], 'exit_reasons': [],
        'results': collections.OrderedDict(), 'messages': collections.defaultdict(list),
        'last_started': None,
    }
    if not facts['exists']:
        return facts
    current = None          # between Test Started and Test Completed
    owner = None            # between BeginEvents and EndEvents: whose errors the log is replaying
    crash_lines = None
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            if crash_lines is not None:
                if 'LogWindows: Error:' in line and len(crash_lines) < 16:
                    text = line.split('LogWindows: Error:', 1)[1].strip()
                    if text:
                        crash_lines.append(text)
                continue
            m = _STARTED.search(line)
            if m:
                current = m.group(2) or m.group(1)
                facts['last_started'] = current
                continue
            m = _COMPLETED.search(line)
            if m:
                facts['results'][m.group(3) or m.group(2)] = m.group(1)
                current = None
                continue
            m = _BEGIN_EVENTS.search(line)
            if m:
                owner = m.group(1).strip()
                continue
            if _END_EVENTS.search(line):
                owner = None
                continue
            m = _EVENT_ERROR.search(line)
            if m and owner:
                msg = m.group(1).strip()
                if msg not in facts['messages'][owner]:
                    facts['messages'][owner].append(msg)
                continue
            m = _FOUND.search(line)
            if m and 'LogAutomationCommandLine' in line:
                facts['found'] = int(m.group(1))
                continue
            m = _PERFORMED.search(line)
            if m:
                facts['performed'] = int(m.group(1))
                facts['queue_empty'] = True
                continue
            if _NO_MATCH.search(line) and 'LogAutomationCommandLine' in line:
                facts['no_match'] = True
                continue
            m = _ENSURE.search(line)
            if m:
                facts['ensures'].append({'test': current, 'message': m.group(1).strip()[:300]})
                continue
            m = _PLUGIN_ERROR.search(line)
            if m:
                facts['plugin_errors'].append(m.group(1).strip()[:400])
                continue
            m = _EXIT_REASON.search(line)
            if m and m.group(1) != 'Win RequestExit':
                facts['exit_reasons'].append(m.group(1))
                continue
            # Only the crash handler's category: a test may well log the words "Assertion failed:".
            if 'LogWindows' in line and _CRASH.search(line):
                crash_lines = []
                text = line.split('Error:', 1)[1].strip() if 'Error:' in line else line.strip()
                if text:
                    crash_lines.append(text)
                facts['crash'] = {'test': current, 'lines': crash_lines}
    return facts


# ------------------------------------------------------------------------------------------------
# index.json
# ------------------------------------------------------------------------------------------------

def read_index(run_dir):
    path = os.path.join(run_dir, 'index.json')
    if not os.path.isfile(path):
        return None, 'there is no index.json'
    try:
        return sourcescan.read_json(path), None
    except (OSError, ValueError) as e:
        return None, 'index.json cannot be read (%s)' % e


def index_tests(data):
    out = []
    for t in g(data, 'tests', []) or []:
        first_error = None
        for entry in g(t, 'entries', []) or []:
            event = g(entry, 'event', {}) or {}
            if event_type(event) != 'Error':
                continue
            filename = g(entry, 'filename', '') or ''
            line = g(entry, 'lineNumber', -1)
            where = short_path(filename)
            if where and isinstance(line, int) and line > 0:
                where = '%s(%d)' % (where, line)
            first_error = ((g(event, 'message', '') or '').strip(), where)
            break
        try:
            duration = float(g(t, 'duration', 0.0) or 0.0)
        except (TypeError, ValueError):
            duration = 0.0
        out.append({
            'path': g(t, 'fullTestPath') or g(t, 'testDisplayName') or '?',
            'state': state_of(t),
            'duration': duration,
            'first_error': first_error,
            'warnings': g(t, 'warnings', 0) or 0,
        })
    return out


# ------------------------------------------------------------------------------------------------
# known issues, history
# ------------------------------------------------------------------------------------------------

def read_known(path):
    if not path or not os.path.isfile(path):
        return []
    data = sourcescan.read_json(path)
    items = data.get('issues', []) if isinstance(data, dict) else data
    out = []
    for item in items or []:
        if isinstance(item, dict) and item.get('test'):
            out.append(item)
    return out


def known_for(path, preset, known):
    for item in known:
        presets = item.get('presets')
        if presets and preset not in presets:
            continue
        if fnmatch.fnmatchcase(path, item['test']):
            return item
    return None


def append_history(path, rows):
    folder = os.path.dirname(path)
    if folder and not os.path.isdir(folder):
        os.makedirs(folder)
    new = not os.path.isfile(path)
    with open(path, 'a', encoding='utf-8', newline='') as f:
        w = csv.writer(f)
        if new:
            w.writerow(HISTORY_FIELDS)
        w.writerows(rows)


def flaky_at(path, commit, preset):
    """Tests that both passed and failed at this commit under this preset, in clean-tree runs only:
    a dirty tree is not the same code twice."""
    if not commit or not os.path.isfile(path):
        return {}
    seen = collections.defaultdict(set)
    with open(path, 'r', encoding='utf-8', newline='') as f:
        for row in csv.DictReader(f):
            if row.get('commit') != commit or row.get('dirty') != '0' or row.get('preset') != preset:
                continue
            if row.get('result') in ('Success', 'Fail'):
                seen[row.get('test', '')].add(row['result'])
    return dict((t, sorted(r)) for t, r in seen.items() if len(r) > 1)


# ------------------------------------------------------------------------------------------------
# the verdict
# ------------------------------------------------------------------------------------------------

def load_info(run_dir):
    path = os.path.join(run_dir, 'run-info.json')
    if not os.path.isfile(path):
        return None
    return sourcescan.read_json(path)


def digest_run(run_dir, write_history=True):
    info = load_info(run_dir) or {}
    preset = info.get('preset') or '?'
    args = info.get('args') or []
    min_tests = int(info.get('minTests') or 0)
    root = info.get('pluginRoot') or sourcescan.DEFAULT_PLUGIN_ROOT
    presets_file = info.get('presetsFile') or sourcescan.DEFAULT_PRESETS
    known_file = info.get('knownIssuesFile') or os.path.join(HERE, 'known-issues.json')
    report_root = info.get('reportRoot') or os.path.dirname(os.path.abspath(run_dir))
    history_file = info.get('historyFile') or os.path.join(report_root, 'history.csv')
    commit = info.get('commit') or ''
    dirty = bool(info.get('dirty'))
    exit_code = info.get('editorExitCode')
    timed_out = bool(info.get('timedOut'))

    log = read_log(os.path.join(run_dir, 'run.log'))
    data, index_problem = read_index(run_dir)
    tests = index_tests(data) if data is not None else []
    notes = []

    infra = []
    if not info:
        infra.append('run-info.json is missing: this is not a directory the runner made')
    if timed_out:
        infra.append('the editor was still running after %s minutes and was killed; the last test it started was %s'
                     % (info.get('timeoutMinutes'), log['last_started'] or 'none'))
    elif exit_code not in (None, 0):
        infra.append('the editor exited with code %s (%s)' % (exit_code, hex_code(exit_code)))
    if log['no_match']:
        infra.append('the filter matched no test ("No automation tests matched")')
    for e in log['plugin_errors']:
        infra.append('a plugin failed to load: %s' % e)
    for r in log['exit_reasons']:
        infra.append('the engine asked to exit early: %s' % r)
    if log['crash']:
        where = log['crash']['test'] or 'start-up (no test was running)'
        lines = log['crash']['lines']
        # The line that names the death beats the banner lines around it.
        named = [l for l in lines if re.search(r'Unhandled Exception|Assertion failed|Fatal error:', l)]
        first = (named or lines or ['see run.log'])[0]
        infra.append('the editor crashed during %s: %s' % (where, first[:300]))
    if log['ensures']:
        e = log['ensures'][0]
        infra.append('%d ensure(s) fired; the first during %s: %s' % (len(log['ensures']), e['test'] or 'no test', e['message']))
    if not log['exists']:
        infra.append('run.log is missing')
    elif not log['queue_empty'] and not timed_out and not log['crash']:
        # -TestExit ends the editor with a FORCED exit (FPlatformMisc::RequestExit(1, ...) straight after
        # the "Queue Empty" line), which does not wait for the log's writer: now and then the last few
        # lines never reach the file, and the log stops at "Report can be opened ...". The engine's
        # report is written before that line and only once the last test is over, so a complete report
        # and an exit code of 0 are the run having finished -- the missing tail is noted, not failed.
        finished_by_report = (data is not None and exit_code == 0 and tests
                              and not any(t['state'] in ('InProcess', 'NotRun') for t in tests))
        if finished_by_report:
            notes.append('the log stops short of "Automation Test Queue Empty", but the engine\'s report is complete '
                         'and the editor exited with 0: the forced exit did not wait for the last log lines')
        else:
            infra.append('the log never reached "Automation Test Queue Empty": the run did not finish')
    if data is None:
        infra.append('%s: the engine writes it only after the last test, so the run did not finish' % index_problem)

    counts = collections.Counter(t['state'] for t in tests)
    ran = counts['Success'] + counts['Fail']
    missing = []
    extra = 0
    complex_empty = []
    if data is not None:
        if ran < min_tests:
            infra.append('only %d tests ran; the %s preset expects at least %d' % (ran, preset, min_tests))
        if counts['InProcess']:
            infra.append('%d test(s) were still in progress when the report was written' % counts['InProcess'])
        if counts['NotRun']:
            infra.append('%d selected test(s) never ran' % counts['NotRun'])
        try:
            exp = sourcescan.expected(root, info.get('filter') or '', args) if os.path.isdir(root) else None
        except (OSError, ValueError) as e:
            exp = None
            notes.append('could not work out the expected tests from source: %s' % e)
        if exp is None:
            notes.append('the expected-tests cross-check was skipped (a Group: filter, or no source to read)')
        else:
            simple, complex_ = exp
            listed = set(t['path'] for t in tests)
            missing = sorted(p for p in simple if p not in listed)
            extra = len([p for p in listed if p not in simple and not any(p.startswith(c + '.') for c in complex_)])
            complex_empty = sorted(c for c in complex_ if not any(p.startswith(c + '.') for p in listed))
            if missing:
                infra.append('%d test(s) are declared in source and selected by the filter but did not run '
                             '(binaries older than the source, or a module that did not load)' % len(missing))

    known = read_known(known_file)
    failures, known_red, known_green = [], [], []
    for t in tests:
        if t['state'] == 'Fail':
            k = known_for(t['path'], preset, known)
            (known_red if k else failures).append((t, k))
        elif t['state'] == 'Success':
            k = known_for(t['path'], preset, known)
            if k:
                known_green.append((t, k))

    budgets = {}
    interaction = set()
    try:
        pdata, presets = sourcescan.load_presets(presets_file)
        budgets = pdata.get('budgets') or {}
        name = budgets.get('interactionPreset')
        if name and os.path.isdir(root):
            _, spec = sourcescan.preset_spec(presets, name)
            _, selected = sourcescan.resolve(root, spec)
            interaction = set(t.path for t in (selected or []))
    except (OSError, ValueError, KeyError) as e:
        notes.append('slow-test budgets unavailable: %s' % e)
    slow_ms = float(budgets.get('slowMs', 1000))
    inter_ms = float(budgets.get('interactionSlowMs', 100))
    slow_any = sorted((t for t in tests if t['duration'] * 1000.0 > slow_ms), key=lambda t: -t['duration'])
    slow_inter = sorted((t for t in tests if t['path'] in interaction and t['duration'] * 1000.0 > inter_ms),
                        key=lambda t: -t['duration'])

    run_name = info.get('runName') or os.path.basename(os.path.abspath(run_dir))
    if write_history and tests:
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
        rows = [[stamp, run_name, commit, '1' if dirty else '0', preset, t['path'], t['state'], '%.4f' % t['duration']]
                for t in tests]
        try:
            append_history(history_file, rows)
        except OSError as e:
            notes.append('history.csv was not updated: %s' % e)
    flaky = flaky_at(history_file, commit, preset)

    if infra:
        code, verdict = 2, 'INFRASTRUCTURE'
    elif failures:
        code, verdict = 1, 'RED'
    else:
        code, verdict = 0, 'GREEN'

    result = {
        'exitCode': code, 'verdict': verdict, 'preset': preset, 'runName': run_name,
        'commit': commit, 'dirty': dirty, 'filter': info.get('filter'), 'args': args,
        'counts': {'ran': ran, 'passed': counts['Success'], 'failed': counts['Fail'], 'skipped': counts['Skipped'],
                   'notRun': counts['NotRun'], 'inProcess': counts['InProcess'], 'minTests': min_tests,
                   'found': log['found'], 'performed': log['performed'], 'knownRed': len(known_red),
                   'withWarnings': len([t for t in tests if t['warnings']])},
        'totalDuration': g(data, 'totalDuration') if data is not None else None,
        'elapsedSeconds': info.get('elapsedSeconds'),
        'infrastructure': infra, 'notes': notes,
        'failures': [{'test': t['path'], 'message': (t['first_error'] or ('', ''))[0], 'where': (t['first_error'] or ('', ''))[1]}
                     for t, _ in failures],
        'knownRed': [{'test': t['path'], 'reason': k.get('reason', ''), 'decision': k.get('decision', '')} for t, k in known_red],
        'knownNowGreen': [{'test': t['path'], 'entry': k.get('test')} for t, k in known_green],
        'missingExpected': missing, 'complexWithoutChildren': complex_empty, 'unexpectedExtra': extra,
        'ensures': log['ensures'][:50], 'crash': log['crash'],
        'slow': [{'test': t['path'], 'ms': round(t['duration'] * 1000.0, 1)} for t in slow_any],
        'slowInteraction': [{'test': t['path'], 'ms': round(t['duration'] * 1000.0, 1)} for t in slow_inter],
        'flaky': flaky,
        'logResults': dict(collections.Counter(log['results'].values())) if data is None else None,
        'logFailures': [p for p, r in log['results'].items() if r != 'Success'] if data is None else None,
    }
    result['_messages'] = log['messages']
    return result


# ------------------------------------------------------------------------------------------------
# output
# ------------------------------------------------------------------------------------------------

def _md_escape(text):
    return (text or '').replace('|', '\\|').replace('\n', ' ')


def write_summary(run_dir, r):
    c = r['counts']
    L = []
    L.append('# DreamGUI tests: %s -- %s' % (r['preset'], r['verdict']))
    L.append('')
    L.append('| | |')
    L.append('|---|---|')
    L.append('| Run | `%s` |' % r['runName'])
    L.append('| Commit | `%s`%s |' % (r['commit'] or 'unknown', ' (uncommitted changes in the tree)' if r['dirty'] else ''))
    L.append('| Exit code | %d |' % r['exitCode'])
    L.append('| Ran | %d (passed %d, failed %d, known red %d; floor %d) |' % (c['ran'], c['passed'], c['failed'], c['knownRed'], c['minTests']))
    L.append('| Not run / skipped / passed with warnings | %d / %d / %d |' % (c['notRun'], c['skipped'], c['withWarnings']))
    if c['found'] is not None:
        L.append('| Engine found / performed | %s / %s |' % (c['found'], c['performed']))
    if r.get('totalDuration') is not None:
        L.append('| Test time | %.1f s (editor run %.0f s) |' % (float(r['totalDuration'] or 0), float(r.get('elapsedSeconds') or 0)))
    L.append('| Arguments | `%s` |' % ' '.join(r['args'] or []))
    L.append('')
    if r['infrastructure']:
        L.append('## The run cannot be trusted')
        L.append('')
        for x in r['infrastructure']:
            L.append('- ' + x)
        L.append('')
    if r['crash'] and r['crash'].get('lines'):
        L.append('## Crash')
        L.append('')
        L.append('```')
        L.extend(r['crash']['lines'])
        L.append('```')
        L.append('')
    if r['ensures']:
        L.append('## Ensures')
        L.append('')
        for e in r['ensures'][:SHOW]:
            L.append('- during `%s`: %s' % (e['test'] or 'no test', _md_escape(e['message'])))
        L.append('')
    if r['missingExpected']:
        L.append('## Declared, selected, not run')
        L.append('')
        for p in r['missingExpected'][:SHOW]:
            L.append('- `%s`' % p)
        if len(r['missingExpected']) > SHOW:
            L.append('- ... and %d more' % (len(r['missingExpected']) - SHOW))
        L.append('')
    if r['failures']:
        L.append('## Failures')
        L.append('')
        L.append('| Test | First error | Where |')
        L.append('|---|---|---|')
        for f in r['failures']:
            L.append('| `%s` | %s | %s |' % (f['test'], _md_escape(f['message'])[:300], _md_escape(f['where'])))
        L.append('')
    if r['logFailures']:
        L.append('## Failures seen in run.log before it stopped')
        L.append('')
        for p in r['logFailures'][:SHOW]:
            msgs = r['_messages'].get(p) or []
            L.append('- `%s`%s' % (p, (': ' + _md_escape(msgs[0])[:300]) if msgs else ''))
        L.append('')
    if r['knownRed']:
        L.append('## Known issues (red, not counted)')
        L.append('')
        for k in r['knownRed']:
            L.append('- `%s` -- %s (%s)' % (k['test'], _md_escape(k['reason']), _md_escape(k['decision'])))
        L.append('')
    if r['knownNowGreen']:
        L.append('## Known issues that now pass -- take them out of known-issues.json')
        L.append('')
        for k in r['knownNowGreen']:
            L.append('- `%s` (entry `%s`)' % (k['test'], k['entry']))
        L.append('')
    if r['flaky']:
        L.append('## Flaky at this commit (passed and failed in clean-tree runs)')
        L.append('')
        for t in sorted(r['flaky'])[:SHOW]:
            L.append('- `%s`' % t)
        L.append('')
    if r['slowInteraction'] or r['slow']:
        L.append('## Slow tests (reported, not failed)')
        L.append('')
        if r['slowInteraction']:
            L.append('Interaction tests over budget:')
            L.append('')
            for s in r['slowInteraction'][:SHOW]:
                L.append('- %8.1f ms  `%s`' % (s['ms'], s['test']))
            L.append('')
        if r['slow']:
            L.append('Any test over budget:')
            L.append('')
            for s in r['slow'][:SHOW]:
                L.append('- %8.1f ms  `%s`' % (s['ms'], s['test']))
            L.append('')
    if r['complexWithoutChildren']:
        L.append('## Parameterised tests that produced no case')
        L.append('')
        for p in r['complexWithoutChildren']:
            L.append('- `%s`' % p)
        L.append('')
    if r['notes']:
        L.append('## Notes')
        L.append('')
        for n in r['notes']:
            L.append('- ' + n)
        L.append('')
    L.append('Files: `run.log`, `index.json` (the engine report), `run-info.json`, `digest.json`.')
    L.append('')
    with open(os.path.join(run_dir, 'summary.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(L))
    public = dict((k, v) for k, v in r.items() if not k.startswith('_'))
    with open(os.path.join(run_dir, 'digest.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(public, f, indent=1)


def print_console(r, run_dir):
    c = r['counts']
    print('RESULT %s  preset=%s  ran=%d passed=%d failed=%d known-red=%d not-run=%d skipped=%d floor=%d  (exit %d)'
          % (r['verdict'], r['preset'], c['ran'], c['passed'], c['failed'], c['knownRed'], c['notRun'], c['skipped'],
             c['minTests'], r['exitCode']))
    for x in r['infrastructure']:
        print('  !! ' + x)
    for f in r['failures'][:SHOW]:
        print('  FAIL %s' % f['test'])
        if f['message']:
            print('       %s  %s' % (f['message'][:240], f['where']))
    if len(r['failures']) > SHOW:
        print('  ... %d more failures in summary.md' % (len(r['failures']) - SHOW))
    for k in r['knownRed'][:SHOW]:
        print('  known red: %s' % k['test'])
    for k in r['knownNowGreen']:
        print('  known issue now passes, remove it from known-issues.json: %s' % k['test'])
    if r['flaky']:
        print('  flaky at this commit: %d test(s), see summary.md' % len(r['flaky']))
    if r['slowInteraction'] or r['slow']:
        print('  slow: %d interaction test(s) over budget, %d test(s) over the general budget'
              % (len(r['slowInteraction']), len(r['slow'])))
    print('  summary: %s' % os.path.join(run_dir, 'summary.md'))


def cmd_tests(ns):
    run_dir = os.path.abspath(ns.run_dir)
    if not os.path.isdir(run_dir):
        sys.stderr.write('digest: %s is not a directory\n' % run_dir)
        return 2
    r = digest_run(run_dir, write_history=not ns.no_history)
    write_summary(run_dir, r)
    print_console(r, run_dir)
    return r['exitCode']


# ------------------------------------------------------------------------------------------------
# build logs
# ------------------------------------------------------------------------------------------------

_BUILD_ERR = re.compile(r'^(?P<file>[A-Za-z]:[^()\n]*?)\((?P<line>\d+)(?:,\d+)?\)\s*:\s*(?:fatal\s+)?[Ee]rror\s*(?P<code>[A-Z]+\d+)?\s*:?\s*(?P<msg>.*)$')


def cmd_build(ns, limit_files=40, per_file=6):
    """A build log, reduced to what has to be fixed: unique errors grouped by file."""
    if not os.path.isfile(ns.log):
        print('no build log at %s' % ns.log)
        return 0
    with open(ns.log, 'r', encoding='utf-8-sig', errors='replace') as f:
        lines = f.read().replace('\r', '').split('\n')
    by_file = collections.OrderedDict()
    seen = set()
    other = []
    locked = False
    for ln in lines:
        s = ln.strip()
        if 'LNK1104' in s:
            locked = True
        m = _BUILD_ERR.match(s)
        if m:
            f = short_path(m.group('file'))
            key = (f, m.group('line'), m.group('code'), m.group('msg')[:160])
            if key in seen:
                continue
            seen.add(key)
            by_file.setdefault(f, []).append((int(m.group('line')), m.group('code') or 'UHT', m.group('msg').strip()))
        elif re.search(r'\berror\b', s, re.I) and not re.search(r'warning|0 error|Warning/Error Summary|error\(s\)', s, re.I):
            if ('LNK' in s or 'fatal' in s.lower() or 'ERROR:' in s or 'Error:' in s or 'unresolved' in s) and s not in seen:
                seen.add(s)
                other.append(s)
    total = sum(len(v) for v in by_file.values())
    print('build: %d file(s) with errors, %d unique error(s), %d other error line(s)' % (len(by_file), total, len(other)))
    for i, (f, errs) in enumerate(by_file.items()):
        if i >= limit_files:
            print('... %d more files' % (len(by_file) - limit_files))
            break
        print('%s  [%d]' % (f, len(errs)))
        for line, code, msg in sorted(errs)[:per_file]:
            print('   %5d %-6s %s' % (line, code, msg[:230]))
        if len(errs) > per_file:
            print('   ... +%d' % (len(errs) - per_file))
    for s in other[:12]:
        print('!! ' + s[:260])
    if locked:
        print('hint: LNK1104 means a DLL the link has to replace is loaded -- an editor has this project open. '
              'Close it, or run against the test host project instead.')
    tail = [l for l in lines if re.search(r'Result:|Total execution time|BUILD SUCCESSFUL|Build succeeded|Target is up to date', l)]
    for l in tail[-4:]:
        print('>> ' + l.strip()[:200])
    return 0


def main(argv=None):
    sourcescan.console_safe()
    ap = argparse.ArgumentParser(description='Digest one automation run (or a build log).')
    sub = ap.add_subparsers(dest='cmd')
    t = sub.add_parser('tests', help='judge one editor run directory')
    t.add_argument('run_dir')
    t.add_argument('--no-history', action='store_true', help='do not append to history.csv')
    b = sub.add_parser('build', help='reduce a build log to its errors')
    b.add_argument('log')
    ns = ap.parse_args(argv)
    try:
        if ns.cmd == 'tests':
            return cmd_tests(ns)
        if ns.cmd == 'build':
            return cmd_build(ns)
    except Exception:  # noqa: BLE001 -- an uncaught exception exits 1, which would read as "tests red"
        import traceback
        traceback.print_exc()
        sys.stderr.write('digest: failed while reading the run; treat it as unjudged\n')
        return 2
    ap.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
