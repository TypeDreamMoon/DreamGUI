"""Cheap checks over the plugin's C++, for mistakes that are expensive to meet in a compiler or an
editor, and for the rules the test suite holds itself to.

    python static_checks.py [--root <plugin>] [--allow <file>] [--rules a,b] [--list-rules]
                            [--fix-eol] [--no-git]

Rules (--list-rules prints them with their reasons):

  Shaped like UHT and MSVC errors, over Source/**/*.h (and the .cpp definitions for the last):
    reflected-name       a UCLASS/USTRUCT/UENUM/UINTERFACE or dynamic delegate name declared twice
    shadowed-property    a UPROPERTY named like a UPROPERTY of an ancestor class
    redeclared-function  a UFUNCTION redeclared under an ancestor's UFUNCTION of that name, or a
                         UPROPERTY named like one
    ufunction-param      a UFUNCTION parameter named like a UPROPERTY of its class (added lines only)
    param-hides-member   a definition's parameter named like a member variable, C4458 (added lines only)
  The tests:
    test-unreadable      a test declaration this checker (and so the runner) cannot read
    test-class-unique    two tests with one class name (a duplicate RunTest symbol)
    test-path-unique     two tests with one full name
    test-path-format     a name that is not DreamGUI.<Area>.<Sentence> or DreamTween.<...>
    test-flags           no EditorContext, or not exactly one filter flag the runner requests
    test-tags            tags filed under no test's name, malformed, unknown, or without a configuration
    hand-fed-hit         a test writing HitResult.Widget or HoverArray itself, outside Private/Driver
    pixels-need-rhi      a test reading pixels without NonNullRHI
    rig-needs-bindtest   a test building FDreamDriverRig::Headless without calling BindTest
  Words and files:
    plan-label           planning vocabulary in comments and string literals (every file of the test
                         module; the other modules' files this branch touched). Identifiers are
                         never read: BuildSettingsVersion.V7 is the engine's, not a label.
    eol                  (warning) a new file whose line endings differ from the rest of the repository

A finding can be allowed where it stands, with a comment on its line or the line above:

    // static-checks: allow(<rule>) <why>

or in static-checks-allow.json (rule, path glob, optional regex on the finding, reason). Allow
entries that matched nothing are listed at the end so that they can be taken out.

--fix-eol rewrites the line endings of the files this branch touched (tracked changes and untracked
files) to what .gitattributes asks for, or else to the repository's majority -- CRLF here, since
core.autocrlf=true keeps the index LF and checks out CRLF. It changes nothing else.

Exit code: 0 no errors (warnings do not fail), 1 errors, 2 the checks could not run.
"""

import argparse
import collections
import fnmatch
import glob
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import sourcescan  # noqa: E402
from sourcescan import matching  # noqa: E402

TEST_DIR = 'Source/DreamGUITests/'
DRIVER_DIR = 'Source/DreamGUITests/Private/Driver/'
DEFAULT_ALLOW = os.path.join(HERE, 'static-checks-allow.json')
DEFAULT_COVERAGE = os.path.join(HERE, 'coverage.json')
TEXT_EXTS = ('.h', '.hpp', '.inl', '.cpp', '.cs', '.json', '.md', '.ini', '.uplugin', '.uproject', '.usf', '.ush',
             '.py', '.ps1', '.psm1', '.sh', '.txt', '.dui', '.dss', '.xml', '.html', '.css', '.js',
             '.gitattributes', '.gitignore')

RULES = collections.OrderedDict([
    ('reflected-name', 'UHT refuses a second reflected type or delegate of the same name.'),
    ('shadowed-property', 'UHT refuses a UPROPERTY that hides an ancestor UPROPERTY.'),
    ('redeclared-function', 'UHT refuses UFUNCTION() on an override, and a UPROPERTY named like an ancestor UFUNCTION.'),
    ('ufunction-param', 'UHT refuses a UFUNCTION parameter named like a UPROPERTY of the class.'),
    ('param-hides-member', 'C4458 is an error in this build.'),
    ('test-unreadable', 'A test the runner cannot read cannot be selected, counted or expected.'),
    ('test-class-unique', 'Two IMPLEMENT_*_AUTOMATION_TEST with one class define RunTest twice.'),
    ('test-path-unique', 'The framework keys tests by their full name; the second one is lost.'),
    ('test-path-format', 'Presets, filters and the coverage matrix read the name: DreamGUI.<Area>.<Sentence>.'),
    ('test-flags', 'Without EditorContext a test never runs in the editor; the engine requests only Smoke, Engine, Product and Perf filters.'),
    ('test-tags', 'The framework files tags under the full name given, silently; the coverage matrix reads them.'),
    ('hand-fed-hit', 'A test that hands the event system its hit tests a pointer nobody can produce; drive the pointer instead.'),
    ('pixels-need-rhi', 'Under -nullrhi nothing is drawn; a pixel test without NonNullRHI runs there and fails or, worse, passes.'),
    ('rig-needs-bindtest', 'An unbound rig reports nothing to the test; its steps can fail without the test failing.'),
    ('plan-label', 'Planning labels do not belong in the code, the test names or the comments.'),
    ('eol', 'Mixed line endings make every later diff of the file noisy.'),
])
WARNING_RULES = frozenset(['eol'])


class Finding(object):
    __slots__ = ('rule', 'rel', 'line', 'message', 'token')

    def __init__(self, rule, rel, line, message, token=None):
        self.rule = rule
        self.rel = rel
        self.line = line
        self.message = message
        self.token = token

    def severity(self):
        return 'warn' if self.rule in WARNING_RULES else 'error'

    def where(self):
        return '%s:%d' % (self.rel, self.line) if self.line else self.rel


# ------------------------------------------------------------------------------------------------
# git (read-only)
# ------------------------------------------------------------------------------------------------

class Git(object):
    def __init__(self, root, enabled=True):
        self.root = root
        self.ok = False
        if enabled:
            out = self.run('rev-parse', '--is-inside-work-tree')
            self.ok = out is not None and out.strip() == 'true'

    def run(self, *args):
        try:
            p = subprocess.run(['git', '-C', self.root, '-c', 'core.quotepath=off'] + list(args),
                               capture_output=True, text=True, encoding='utf-8', errors='replace')
        except OSError:
            return None
        return p.stdout if p.returncode == 0 else None

    def lines(self, *args):
        out = self.run(*args)
        return [l.strip() for l in (out or '').split('\n') if l.strip()]

    def touched(self):
        """Files with uncommitted changes against HEAD, and untracked files."""
        return sorted(set(self.lines('diff', '--name-only', 'HEAD')) | set(self.lines('ls-files', '--others', '--exclude-standard')))

    def new_files(self):
        return sorted(set(self.lines('ls-files', '--others', '--exclude-standard')) |
                      set(self.lines('diff', '--cached', '--name-only', '--diff-filter=A')))

    def added_lines(self, subdir='Source'):
        res = collections.defaultdict(set)
        diff = self.run('diff', '-U0', 'HEAD', '--', subdir) or ''
        cur = None
        for line in diff.split('\n'):
            if line.startswith('+++ b/'):
                cur = line[6:].strip()
            elif line.startswith('+++ '):
                cur = None
            elif line.startswith('@@') and cur:
                m = re.search(r'\+(\d+)(?:,(\d+))?', line)
                if m:
                    start, count = int(m.group(1)), int(m.group(2) or '1')
                    res[cur].update(range(start, start + count))
        for rel in self.lines('ls-files', '--others', '--exclude-standard', '--', subdir):
            path = os.path.join(self.root, rel)
            if os.path.isfile(path):
                with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                    n = sum(1 for _ in f)
                res[rel].update(range(1, n + 1))
        return res

    def eol_attributes(self, rels):
        """rel -> 'lf' / 'crlf' for the files .gitattributes gives an eol, or marks -text (then 'binary')."""
        out = {}
        for i in range(0, len(rels), 200):
            chunk = rels[i:i + 200]
            raw = self.run('check-attr', '-z', 'eol', 'text', '--', *chunk)
            if not raw:
                continue
            parts = raw.split('\0')
            for j in range(0, len(parts) - 2, 3):
                path, attr, value = parts[j], parts[j + 1], parts[j + 2]
                if attr == 'eol' and value in ('lf', 'crlf'):
                    out[path] = value
                elif attr == 'text' and value == 'unset':
                    out[path] = 'binary'
        return out


# ------------------------------------------------------------------------------------------------
# UHT-shaped rules (ported from the checks the earlier review rounds used)
# ------------------------------------------------------------------------------------------------

SPEC = r'\(((?:[^()]|\((?:[^()]|\([^()]*\))*\))*)\)'
DECL = re.compile(r'\b(class|struct)\s+(?:[A-Z0-9_]+_API\s+)?([UFAI][A-Za-z0-9_]+)\b(?:\s+final)?\s*(?::[^;{]*)?\{')
PROP = re.compile(r'UPROPERTY\s*' + SPEC + r'\s*([^;{}]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:=[^;]*|\{[^}]*\}|:\s*\d+)?\s*;')
FUNC = re.compile(r'UFUNCTION\s*' + SPEC + r'\s*((?:[^;{()]|\([^()]*\))*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\(')
REFLECTED = [
    ('type', re.compile(r'\bUENUM\s*' + SPEC + r'\s*enum\s+(?:class\s+)?([A-Za-z_0-9]+)')),
    ('type', re.compile(r'\bUSTRUCT\s*' + SPEC + r'\s*struct\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_0-9]+)')),
    ('type', re.compile(r'\bUCLASS\s*' + SPEC + r'\s*class\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_0-9]+)')),
    ('type', re.compile(r'\bUINTERFACE\s*' + SPEC + r'\s*class\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_0-9]+)')),
    ('delegate', re.compile(r'\bDECLARE_DYNAMIC(?:_MULTICAST)?_(?:SPARSE_)?DELEGATE[A-Za-z_]*\s*\(\s*(?:[A-Za-z_0-9:<>\*& ]+,\s*)??(F[A-Za-z_0-9]+)\s*[,)]')),
]
MEMBER = re.compile(r'(?:^|[;{}])\s*(?:mutable\s+|static\s+|const\s+)*[A-Za-z_][A-Za-z_0-9:<>,\*&\s]*?[\s\*&>]([A-Za-z_][A-Za-z_0-9]*)\s*(?::\s*\d+\s*)?(?:=[^;(){}]*|\{[^{}]*\})?;', re.M)
DEFN = re.compile(r'\b([UAF][A-Za-z_0-9]+)::(~?[A-Za-z_][A-Za-z_0-9]*)\s*\(')


def class_ranges(code):
    """Top-level class/struct bodies: a nested struct's members belong to the class around it."""
    ranges = []
    for m in DECL.finditer(code):
        if ranges and m.start() < ranges[-1][1]:
            continue
        end = matching(code, m.end() - 1, '{', '}')
        if end < 0:
            continue
        bm = re.search(r':\s*public\s+([A-Za-z_0-9]+)', m.group(0))
        ranges.append((m.start(), end + 1, m.group(2), bm.group(1) if bm else None))
    return ranges


def param_names(plist):
    names, depth, cur = [], 0, ''
    for ch in plist + ',':
        if ch in '<([{':
            depth += 1
        elif ch in '>)]}':
            depth -= 1
        if ch == ',' and depth == 0:
            p = cur.split('=')[0].strip()
            m = re.search(r'([A-Za-z_][A-Za-z_0-9]*)\s*(?:\[[^\]]*\])?$', p)
            if m and p and m.group(1) not in ('void', 'const'):
                names.append(m.group(1))
            cur = ''
        else:
            cur += ch
    return names


def check_reflection(root, git, findings, want):
    headers = [sourcescan.load(root, p) for p in sourcescan.source_files(root, 'Source', ('.h',))]
    declared = collections.defaultdict(list)
    classes = {}
    members = collections.defaultdict(set)
    uparams = []
    for sf in headers:
        code = sf.code
        for kind, rx in REFLECTED:
            for m in rx.finditer(code):
                declared[(kind, m.group(m.lastindex))].append((sf.rel, sf.line_of(m.start())))
        for a, b, name, base in class_ranges(code):
            props = {}
            funcs = {}
            for mm in PROP.finditer(code, a, b):
                props.setdefault(mm.group(3), sf.line_of(mm.start(3)))
            for mm in FUNC.finditer(code, a, b):
                funcs.setdefault(mm.group(3), sf.line_of(mm.start(3)))
                k = code.find('(', mm.end() - 1)
                e = matching(code, k)
                if e > 0:
                    uparams.append((name, mm.group(3), param_names(code[k + 1:e]), sf.rel, sf.line_of(mm.start())))
            if name[:1] in ('U', 'A'):
                classes[name] = {'base': base, 'props': props, 'funcs': funcs, 'rel': sf.rel}
            members[name].update(props)
            # depth-1 text only, so that the locals of inline bodies are not taken for members
            body = code[a:b]
            flat, depth = [], 0
            for ch in body:
                if ch == '{':
                    depth += 1
                    flat.append(ch if depth == 1 else ' ')
                elif ch == '}':
                    flat.append(ch if depth == 1 else ' ')
                    depth -= 1
                else:
                    flat.append(ch if depth == 1 or ch == '\n' else ' ')
            for mm in MEMBER.finditer(''.join(flat)):
                members[name].add(mm.group(1))

    if 'reflected-name' in want:
        for (kind, name), sites in sorted(declared.items()):
            if len(sites) > 1:
                others = ', '.join('%s:%d' % s for s in sites[1:])
                findings.append(Finding('reflected-name', sites[0][0], sites[0][1],
                                        'the %s name %s is declared again at %s' % (kind, name, others), name))

    def chain(name):
        seen = []
        while name and name in classes and name not in seen:
            seen.append(name)
            name = classes[name]['base']
        return seen

    if 'shadowed-property' in want or 'redeclared-function' in want:
        for name, info in sorted(classes.items()):
            for anc_name in chain(info['base'])[:]:
                anc = classes[anc_name]
                if 'shadowed-property' in want:
                    for p in sorted(set(info['props']) & set(anc['props'])):
                        findings.append(Finding('shadowed-property', info['rel'], info['props'][p],
                                                'UPROPERTY %s::%s hides %s::%s' % (name, p, anc_name, p), p))
                if 'redeclared-function' in want:
                    for f in sorted(set(info['funcs']) & set(anc['funcs'])):
                        findings.append(Finding('redeclared-function', info['rel'], info['funcs'][f],
                                                'UFUNCTION %s::%s redeclares %s::%s (an override takes no UFUNCTION())' % (name, f, anc_name, f), f))
                    for p in sorted(set(info['props']) & set(anc['funcs'])):
                        findings.append(Finding('redeclared-function', info['rel'], info['props'][p],
                                                'UPROPERTY %s::%s has the name of UFUNCTION %s::%s' % (name, p, anc_name, p), p))

    if not git.ok or not ('ufunction-param' in want or 'param-hides-member' in want):
        return
    added = git.added_lines('Source')
    if 'ufunction-param' in want:
        for cls, fn, params, rel, line in uparams:
            props = set()
            for c in chain(cls):
                props |= set(classes[c]['props'])
            hits = [p for p in params if p in props]
            if hits and any(l in added.get(rel, ()) for l in range(line, line + 6)):
                findings.append(Finding('ufunction-param', rel, line,
                                        'UFUNCTION %s::%s has parameter(s) %s named like a UPROPERTY; prefix them with In (In%s)' % (cls, fn, ', '.join(hits), hits[0]), hits[0]))
    if 'param-hides-member' in want:
        for path in sourcescan.source_files(root, 'Source', ('.cpp',)):
            rel = sourcescan.rel_path(root, path)
            if rel not in added:
                continue
            sf = sourcescan.load(root, path)
            code = sf.code
            for m in DEFN.finditer(code):
                cls = m.group(1)
                if cls not in classes:
                    continue
                k = m.end() - 1
                e = matching(code, k)
                if e < 0:
                    continue
                after = code[e + 1:e + 200].lstrip()
                if not (after.startswith('{') or after.startswith('const') or after.startswith(':')):
                    continue   # a call, not a definition
                line = sf.line_of(m.start())
                if line not in added[rel]:
                    continue
                mem = set()
                for c in chain(cls):
                    mem |= members[c]
                hits = [p for p in param_names(code[k + 1:e]) if p in mem]
                if hits:
                    findings.append(Finding('param-hides-member', rel, line,
                                            '%s::%s: parameter(s) %s hide a member (C4458)' % (cls, m.group(2), ', '.join(hits)), hits[0]))


# ------------------------------------------------------------------------------------------------
# the tests
# ------------------------------------------------------------------------------------------------

PATH_FORMAT = re.compile(r'^(?:DreamGUI(?:\.[A-Z][A-Za-z0-9_]*){2,}|DreamTween(?:\.[A-Z][A-Za-z0-9_]*)+)$')
HAND_FED = [
    re.compile(r'\bHitResult\s*(?:\.|->)\s*Widget\s*=(?!=)'),
    re.compile(r'\bHoverArray\s*(?:\.|->)\s*(?:Add|AddUnique|Emplace|EmplaceAt|Push|Insert|Append)\s*\('),
    re.compile(r'\bHoverArray\s*=(?!=)'),
]
PIXELS = re.compile(r'\bFDreamPixelProbe\b|\bReadPixels\s*\(')
HEADLESS = re.compile(r'\bFDreamDriverRig\s*::\s*Headless\s*\(')
BIND = re.compile(r'\bBindTest\s*\(')


def tag_vocabulary(path):
    data = sourcescan.read_json(path)
    inputs = [i['tag'] for i in data.get('inputs', []) if i.get('tag')]
    configs = [c['tag'] for c in data.get('configs', []) if c.get('tag')]
    extra = list(data.get('extraTags', []) or [])
    return inputs, configs, extra


def check_tests(root, findings, want, coverage_file):
    s = sourcescan.scan(root)
    if 'test-unreadable' in want:
        for u in s.unreadable:
            findings.append(Finding('test-unreadable', u.rel, u.line, '%s: %s' % (u.macro, u.why)))
    if 'test-class-unique' in want:
        by_cls = collections.defaultdict(list)
        for t in s.tests:
            by_cls[t.cls].append(t)
        for cls, ts in sorted(by_cls.items()):
            if len(ts) > 1:
                findings.append(Finding('test-class-unique', ts[1].rel, ts[1].line,
                                        'test class %s is also declared at %s:%d' % (cls, ts[0].rel, ts[0].line), cls))
    if 'test-path-unique' in want:
        by_path = collections.defaultdict(list)
        for t in s.named():
            by_path[t.path].append(t)
        for path, ts in sorted(by_path.items()):
            if len(ts) > 1:
                findings.append(Finding('test-path-unique', ts[1].rel, ts[1].line,
                                        'test name %s is also declared at %s:%d' % (path, ts[0].rel, ts[0].line), path))
    for t in s.named():
        if 'test-path-format' in want and not PATH_FORMAT.match(t.path):
            findings.append(Finding('test-path-format', t.rel, t.line,
                                    '%s is not DreamGUI.<Area>.<Sentence> (PascalCase segments, at least three) or DreamTween.<...>' % t.path, t.path))
        if 'test-flags' in want and t.flags is not None:
            if 'EditorContext' not in t.flags:
                findings.append(Finding('test-flags', t.rel, t.line, '%s lacks EAutomationTestFlags::EditorContext' % t.path, t.path))
            filters = [f for f in t.flags if f in sourcescan.FILTER_FLAGS]
            if len(filters) != 1:
                findings.append(Finding('test-flags', t.rel, t.line,
                                        '%s must carry exactly one filter flag (it has %s)' % (t.path, ', '.join(filters) or 'none'), t.path))
            elif filters[0] not in sourcescan.REQUESTED_FILTER_FLAGS:
                findings.append(Finding('test-flags', t.rel, t.line,
                                        '%s carries %s, which Automation RunTests never requests: it would silently never run' % (t.path, filters[0]), t.path))

    if 'test-tags' in want:
        try:
            inputs, configs, extra = tag_vocabulary(coverage_file)
        except (OSError, ValueError) as e:
            inputs, configs, extra = None, None, None
            findings.append(Finding('test-tags', sourcescan.rel_path(root, coverage_file), 0, 'cannot read the tag vocabulary: %s' % e))
        known = set((inputs or []) + (configs or []) + (extra or []))
        by_cls = collections.defaultdict(list)
        for t in s.tests:
            by_cls[t.cls].append(t)
        per_path = collections.Counter()
        for site in s.tag_sites:
            owners = by_cls.get(site.cls) or []
            if not owners:
                findings.append(Finding('test-tags', site.rel, site.line,
                                        'tags registered for %s, which is no test class' % (site.cls or '(unreadable)')))
                continue
            t = owners[0]
            if site.path is None:
                findings.append(Finding('test-tags', site.rel, site.line, 'the name tags are filed under cannot be read'))
            elif site.path != t.path:
                findings.append(Finding('test-tags', site.rel, site.line,
                                        'tags are filed under %s but the test is %s; the framework keys tags by the exact full name' % (site.path, t.path), site.path))
            else:
                per_path[site.path] += 1
            if site.tags is None:
                findings.append(Finding('test-tags', site.rel, site.line,
                                        'the tag string %r is not of the form "[Tag][Tag]"' % (site.raw,)))
                continue
            if inputs is None:
                continue
            for tag in site.tags:
                if tag not in known:
                    findings.append(Finding('test-tags', site.rel, site.line,
                                            'unknown tag [%s]; the vocabulary is in coverage.json (inputs, configs, extraTags)' % tag, tag))
            if any(tag in inputs for tag in site.tags) and not any(tag in configs for tag in site.tags):
                findings.append(Finding('test-tags', site.rel, site.line,
                                        'an input tag needs a configuration tag too ([%s]); the default configuration is [%s]'
                                        % ('][' .join(configs), configs[0] if configs else '?')))
        for path, n in sorted(per_path.items()):
            if n > 1:
                t = [x for x in s.named() if x.path == path][0]
                findings.append(Finding('test-tags', t.rel, t.line, 'tags are registered %d times for %s; the framework keeps the first' % (n, path), path))

    for t in s.named():
        if not t.rel.startswith(TEST_DIR) or not t.body:
            continue
        if 'pixels-need-rhi' in want and (t.flags is None or 'NonNullRHI' not in t.flags):
            if sourcescan.body_reaches(t, PIXELS):
                findings.append(Finding('pixels-need-rhi', t.rel, t.line,
                                        '%s reads pixels (FDreamPixelProbe / ReadPixels, directly or through a helper in this file) but is not flagged NonNullRHI' % t.path, t.path))
        if 'rig-needs-bindtest' in want and sourcescan.body_reaches(t, HEADLESS) and not sourcescan.body_reaches(t, BIND):
            findings.append(Finding('rig-needs-bindtest', t.rel, t.line,
                                    '%s builds FDreamDriverRig::Headless but never calls BindTest (in its body or a helper in this file)' % t.path, t.path))

    if 'hand-fed-hit' in want:
        for path in sourcescan.source_files(root, TEST_DIR.rstrip('/')):
            rel = sourcescan.rel_path(root, path)
            if rel.startswith(DRIVER_DIR):
                continue
            sf = sourcescan.load(root, path)
            for rx in HAND_FED:
                for m in rx.finditer(sf.code):
                    findings.append(Finding('hand-fed-hit', rel, sf.line_of(m.start()),
                                            'writes %s itself; drive the pointer through FDreamDriverRig (Private/Driver) so that a raycaster finds the hit' % m.group(0).split('=')[0].strip(), m.group(0)))


# ------------------------------------------------------------------------------------------------
# words
# ------------------------------------------------------------------------------------------------

# Case-insensitive: numbered rounds and stages of work, in English and in Chinese, and section marks.
LABELS = re.compile(
    r'\bbatch\s*\d+|\bbatch (?:one|two|three)\b|\b(?:first|second|third) batch\b|parity wave|\bwave \d+|\bphase \d+'
    r'|第\s*[一二三四五六七八九十\d]+\s*[批波轮]|阶段\s*\d+|§\s*\d|\bthe plan\b|control-parity', re.I)
# Case-sensitive: work-item and decision codes, one of the capitals F C E V P D M and one or two digits.
CODES = re.compile(r'\b[FCEVPDM]\d{1,2}\b')
# The same vocabulary the other modules' touched files were held to before, plus the Chinese words.
OLD_WORDS = re.compile(
    r'parity wave|\bwave \d|\bbatch \d|\bbatch one|\bbatch two|first batch|second batch|§\s*\d|\bthe plan\b|control-parity'
    r'|\bphase \d|第\s*[一二三四五六七八九十\d]+\s*[批波轮]|阶段\s*\d+', re.I)
OLD_CODES = re.compile(r'\bM\d\b(?!\w)')


def _is_key_name(content, start, end, token):
    """F1..F24 used as the key: after '+' (Shift+F10) or EKeys::, before 'key', or a whole string."""
    if not token.startswith('F'):
        return False
    before = content[:start]
    if before.endswith('+') or before.endswith('EKeys::') or before.rstrip().endswith('EKeys::'):
        return True
    if re.match(r'\s*(?:key|keys|button)\b', content[end:], re.I):
        return True
    return content.strip() == token


def check_words(root, git, findings, want):
    if 'plan-label' not in want:
        return
    for path in sourcescan.source_files(root, TEST_DIR.rstrip('/')):
        sf = sourcescan.load(root, path)
        for start, end, kind, content, cstart in sf.pieces:
            for rx in (LABELS, CODES):
                for m in rx.finditer(content):
                    token = m.group(0)
                    if rx is CODES and _is_key_name(content, m.start(), m.end(), token):
                        continue
                    findings.append(Finding('plan-label', sf.rel, sf.line_of(cstart + m.start()),
                                            '"%s" in a %s' % (token, kind), token))
    if not git.ok:
        return
    # The other modules: only the files this branch touched (their older comments met the earlier
    # review rounds' vocabulary already), and only comments and strings -- an identifier such as
    # BuildSettingsVersion.V7 is the engine's, not a label.
    for rel in git.touched():
        if not rel.startswith('Source/') or rel.startswith(TEST_DIR):
            continue
        path = os.path.join(root, rel)
        if not os.path.isfile(path) or not rel.lower().endswith(sourcescan.SOURCE_EXTS + ('.cs',)):
            continue
        sf = sourcescan.load(root, path)
        for start, end, kind, content, cstart in sf.pieces:
            for rx in (OLD_WORDS, OLD_CODES):
                for m in rx.finditer(content):
                    findings.append(Finding('plan-label', rel, sf.line_of(cstart + m.start()),
                                            '"%s" in a %s of a changed file' % (m.group(0), kind), m.group(0)))


# ------------------------------------------------------------------------------------------------
# line endings
# ------------------------------------------------------------------------------------------------

def eol_style(data):
    crlf = data.count(b'\r\n')
    lf = data.count(b'\n') - crlf
    if crlf == 0 and lf == 0:
        return None
    if lf == 0:
        return 'crlf'
    if crlf == 0:
        return 'lf'
    return 'mixed'


def is_text(rel, data):
    if rel.lower().endswith(TEXT_EXTS):
        return True
    return '.' not in os.path.basename(rel) and b'\0' not in data[:8192]


def majority_eol(root, git):
    counts = collections.Counter()
    for rel in git.lines('ls-files'):
        if not rel.lower().endswith(TEXT_EXTS):
            continue
        path = os.path.join(root, rel)
        try:
            with open(path, 'rb') as f:
                style = eol_style(f.read())
        except OSError:
            continue
        if style in ('lf', 'crlf'):
            counts[style] += 1
    return counts.most_common(1)[0][0] if counts else 'crlf'


def check_eol(root, git, findings, want):
    if 'eol' not in want or not git.ok:
        return
    rels = [r for r in git.new_files() if os.path.isfile(os.path.join(root, r))]
    if not rels:
        return
    majority = majority_eol(root, git)
    attrs = git.eol_attributes(rels)
    for rel in rels:
        with open(os.path.join(root, rel), 'rb') as f:
            data = f.read()
        if attrs.get(rel) == 'binary' or not is_text(rel, data):
            continue
        style = eol_style(data)
        want_style = attrs.get(rel, majority)
        if style is not None and style != want_style:
            findings.append(Finding('eol', rel, 0, 'new file with %s line endings; the repository uses %s%s'
                                    % (style.upper(), want_style.upper(), ' (.gitattributes)' if rel in attrs else ''), style))


def fix_eol(root, git):
    """Rewrite the touched text files to the line endings .gitattributes or the majority asks for."""
    if not git.ok:
        print('--fix-eol needs git; nothing done')
        return 0
    majority = majority_eol(root, git)
    rels = [r for r in git.touched() if os.path.isfile(os.path.join(root, r))]
    attrs = git.eol_attributes(rels) if rels else {}
    fixed = 0
    for rel in rels:
        path = os.path.join(root, rel)
        with open(path, 'rb') as f:
            data = f.read()
        if attrs.get(rel) == 'binary' or not is_text(rel, data):
            continue
        want_style = attrs.get(rel, majority)
        style = eol_style(data)
        if style is None or style == want_style:
            continue
        normalized = data.replace(b'\r\n', b'\n')
        out = normalized.replace(b'\n', b'\r\n') if want_style == 'crlf' else normalized
        with open(path, 'wb') as f:
            f.write(out)
        fixed += 1
        print('fixed %-5s -> %-4s %s' % (style, want_style, rel))
    print('line endings rewritten in %d file(s)' % fixed)
    return fixed


# ------------------------------------------------------------------------------------------------
# allowing
# ------------------------------------------------------------------------------------------------

MARKER = re.compile(r'static-checks:\s*allow\(\s*([a-z\-, *]+)\s*\)')


def load_allow(path):
    if not path or not os.path.isfile(path):
        return []
    data = sourcescan.read_json(path)
    entries = data.get('allow', []) if isinstance(data, dict) else data
    out = []
    for e in entries or []:
        if isinstance(e, dict) and e.get('rule') and e.get('path') and e.get('reason'):
            e = dict(e)
            e['_rx'] = re.compile(e['match']) if e.get('match') else None
            e['_used'] = 0
            out.append(e)
    return out


def inline_allowed(root, f):
    if not f.line:
        return False
    path = os.path.join(root, f.rel)
    if not os.path.isfile(path):
        return False
    try:
        sf = sourcescan.load(root, path)
        lines = [sf.line_text(f.line)]
        if f.line > 1:
            lines.append(sf.line_text(f.line - 1))
    except (OSError, IndexError):
        return False
    for text in lines:
        for m in MARKER.finditer(text):
            rules = [r.strip() for r in m.group(1).split(',')]
            if f.rule in rules or '*' in rules:
                return True
    return False


def filter_allowed(root, findings, allow):
    kept, allowed = [], 0
    for f in findings:
        hit = None
        for e in allow:
            if e['rule'] not in (f.rule, '*'):
                continue
            if not fnmatch.fnmatchcase(f.rel, e['path']):
                continue
            if e['_rx'] is not None and not e['_rx'].search(f.token if f.token is not None else f.message):
                continue
            hit = e
            break
        if hit is not None:
            hit['_used'] += 1
            allowed += 1
        elif inline_allowed(root, f):
            allowed += 1
        else:
            kept.append(f)
    return kept, allowed


# ------------------------------------------------------------------------------------------------

def main(argv=None):
    sourcescan.console_safe()
    ap = argparse.ArgumentParser(description='Static checks over the plugin source (see the module docstring).')
    ap.add_argument('--root', default=None, help='plugin root (default: the checkout holding this script)')
    ap.add_argument('--allow', default=DEFAULT_ALLOW, help='allow list (default: static-checks-allow.json beside this script)')
    ap.add_argument('--coverage', default=DEFAULT_COVERAGE, help='tag vocabulary (default: coverage.json beside this script)')
    ap.add_argument('--rules', default=None, help='comma-separated subset of rules to run')
    ap.add_argument('--list-rules', action='store_true')
    ap.add_argument('--fix-eol', action='store_true', help='rewrite the line endings of the touched files, then check')
    ap.add_argument('--no-git', action='store_true', help='skip everything that needs git (touched files, added lines, eol)')
    ns = ap.parse_args(argv)

    if ns.list_rules:
        for rule, why in RULES.items():
            print('%-20s %-5s %s' % (rule, 'warn' if rule in WARNING_RULES else 'error', why))
        return 0
    root = sourcescan.plugin_root(ns.root)
    if not os.path.isdir(os.path.join(root, 'Source')):
        sys.stderr.write('static_checks: %s has no Source directory\n' % root)
        return 2
    want = set(RULES)
    if ns.rules:
        want = set(r.strip() for r in ns.rules.split(',') if r.strip())
        unknown = want - set(RULES)
        if unknown:
            sys.stderr.write('static_checks: unknown rule(s): %s\n' % ', '.join(sorted(unknown)))
            return 2
    git = Git(root, enabled=not ns.no_git)
    if not git.ok:
        print('note: git is not available here; touched-file words, added-line checks and eol are skipped')

    try:
        if ns.fix_eol:
            fix_eol(root, git)
        findings = []
        check_reflection(root, git, findings, want)
        check_tests(root, findings, want, ns.coverage)
        check_words(root, git, findings, want)
        check_eol(root, git, findings, want)
        allow = load_allow(ns.allow)
    except Exception:  # noqa: BLE001 -- a crash here must read as "could not run", not as findings
        import traceback
        traceback.print_exc()
        return 2

    kept, allowed = filter_allowed(root, findings, allow)
    kept.sort(key=lambda f: (f.severity() != 'error', f.rule, f.rel, f.line))
    for f in kept:
        print('%-5s %-20s %s  %s' % (f.severity(), f.rule, f.where(), f.message))
    errors = sum(1 for f in kept if f.severity() == 'error')
    warnings = len(kept) - errors
    by_rule = collections.Counter(f.rule for f in kept)
    print('static checks: %d error(s), %d warning(s), %d allowed%s'
          % (errors, warnings, allowed, ('  [' + ', '.join('%s %d' % kv for kv in sorted(by_rule.items())) + ']') if by_rule else ''))
    stale = [e for e in allow if not e['_used'] and (e['rule'] in want or e['rule'] == '*')]
    for e in stale:
        print('note: allow entry matched nothing, consider removing it: %s %s %s' % (e['rule'], e['path'], e.get('match') or ''))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
