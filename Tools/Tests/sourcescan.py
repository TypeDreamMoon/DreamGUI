"""What the plugin's C++ declares as automation tests, read the way the framework will register
them -- names, flags, tags, which file and line, and what each test body reaches -- without a
compiler, an editor or a running engine.

The static checks, the coverage matrix, the digest and the runner's filter resolution all read
tests through this one module, so the four of them cannot disagree about what a test is. Nothing
here writes a file or changes the working tree.

Command line (the runner uses the first form):

    python sourcescan.py filter --preset Quick [--presets presets.json] [--root <plugin>]
        prints the text that goes after "Automation RunTests" for that preset
    python sourcescan.py tests [--root <plugin>] [--json]
        lists every declared test: path, kind, flags, tags, file:line

Two facts about the engine shape everything below (both read from the 5.8 source):

  * AutomationCommandline.cpp splits the RunTests argument on '+'. A term "StartsWith:X" matches
    paths starting with "X." (a '.' is appended when missing); a term "^X$" is an exact match;
    any other term is a case-insensitive substring match. Terms are OR-ed; there is no way to
    exclude. Excluding is therefore done here: a preset's selection is compressed into the
    shortest list of terms that matches exactly the selected declared tests.
  * AutomationTest.cpp (GetValidTestNames) drops, before any filter is applied, tests flagged
    NonNullRHI when the command line has -nullrhi, tests flagged RequiresUser when unattended,
    Disabled tests, tests without the running application's context flag, and tests whose filter
    flag is not one of Smoke/Engine/Product/Perf (the controller's requested flags).
"""

import argparse
import bisect
import glob
import json
import os
import re
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_PLUGIN_ROOT = os.path.normpath(os.path.join(HERE, os.pardir, os.pardir))
DEFAULT_PRESETS = os.path.join(HERE, 'presets.json')
TEST_MODULE_DIR = 'Source/DreamGUITests'
SOURCE_EXTS = ('.h', '.hpp', '.inl', '.cpp')


# ------------------------------------------------------------------------------------------------
# small helpers
# ------------------------------------------------------------------------------------------------

def plugin_root(arg=None):
    """The plugin whose sources are read: the one given, or the checkout this script lives in."""
    return os.path.normpath(os.path.abspath(arg)) if arg else DEFAULT_PLUGIN_ROOT


def rel_path(root, path):
    return os.path.relpath(path, root).replace(os.sep, '/')


def read_text(path):
    """Source text with universal newlines, so that every position below maps to one line number."""
    with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
        return f.read()


def read_json(path):
    """JSON the engine and UBT write carry a UTF-8 byte order mark; utf-8-sig reads both kinds."""
    with open(path, 'r', encoding='utf-8-sig') as f:
        return json.load(f)


def console_safe():
    """A path or log line the console's code page cannot show must not end a report with a traceback."""
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='replace')
        except (AttributeError, ValueError):
            pass


# ------------------------------------------------------------------------------------------------
# lexing: comments and literals out of the way, positions kept
# ------------------------------------------------------------------------------------------------

# One pass finds whichever of these starts first, which is exactly how the compiler decides
# whether a '//' is a comment or part of a string. Raw strings are matched before ordinary ones
# because a raw string may hold quotes. A character literal must not follow an identifier or a
# digit, so that C++14 digit separators (1'000) are not taken for one.
_LEX = re.compile(
    r'(?P<line>//[^\n]*)'
    r'|(?P<block>/\*.*?\*/)'
    r'|(?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\\s"]{0,16})\((?P<rawbody>.*?)\)(?P=delim)")'
    r'|(?P<str>(?:u8|[uUL])?"(?:[^"\\\n]|\\.)*")'
    r'|(?P<chr>(?<![0-9A-Za-z_])\'(?:[^\'\\\n]|\\.){1,8}\')',
    re.S)
_NOT_NEWLINE = re.compile(r'[^\n]')


def _blank(text):
    return _NOT_NEWLINE.sub(' ', text)


def lex(text):
    """Returns (code, nocomments, pieces).

    code        -- text with comments blanked and every literal's contents blanked (quotes kept)
    nocomments  -- text with comments blanked and literals left alone
    pieces      -- [(start, end, kind, content, content_start)] for every comment ('comment') and
                   string literal ('string'); content_start is where content begins in text

    All three keep the length and the line breaks of text, so a position found in one is valid in
    the others and in text itself."""
    code, nocom, pieces = [], [], []
    last = 0
    for m in _LEX.finditer(text):
        a, b = m.span()
        code.append(text[last:a])
        nocom.append(text[last:a])
        whole = m.group(0)
        if m.group('line') is not None:
            blank = _blank(whole)
            code.append(blank)
            nocom.append(blank)
            pieces.append((a, b, 'comment', whole[2:], a + 2))
        elif m.group('block') is not None:
            blank = _blank(whole)
            code.append(blank)
            nocom.append(blank)
            pieces.append((a, b, 'comment', whole[2:-2], a + 2))
        elif m.group('raw') is not None or m.group('str') is not None:
            q = whole.index('"')
            if m.group('raw') is not None:
                content, content_start = m.group('rawbody'), m.start('rawbody')
            else:
                content, content_start = whole[q + 1:-1], a + q + 1
            code.append(whole[:q + 1] + _blank(whole[q + 1:-1]) + '"')
            nocom.append(whole)
            pieces.append((a, b, 'string', content, content_start))
        else:
            code.append("'" + _blank(whole[1:-1]) + "'")
            nocom.append(whole)
        last = b
    code.append(text[last:])
    nocom.append(text[last:])
    return ''.join(code), ''.join(nocom), pieces


_PP = re.compile(r'^[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b([^\n]*)$', re.M)


def dead_spans(code):
    """The #if 0 regions (up to their #else/#elif/#endif): they never reach the compiler, so they
    must not reach the checks either. Every other condition is taken as true -- this module only
    ever reads editor builds, where WITH_EDITOR and WITH_DEV_AUTOMATION_TESTS hold."""
    spans = []
    depth = 0
    dead_at = None          # (start position, depth) of the #if 0 being skipped
    for m in _PP.finditer(code):
        d = m.group(1)
        if d in ('if', 'ifdef', 'ifndef'):
            depth += 1
            if dead_at is None and d == 'if' and re.match(r'\s*0\s*$', m.group(2)):
                dead_at = (m.end(), depth)
        elif d in ('elif', 'else'):
            if dead_at is not None and dead_at[1] == depth:
                spans.append((dead_at[0], m.start()))
                dead_at = None
        elif d == 'endif':
            if dead_at is not None and dead_at[1] == depth:
                spans.append((dead_at[0], m.start()))
                dead_at = None
            depth = max(0, depth - 1)
    if dead_at is not None:
        spans.append((dead_at[0], len(code)))
    return spans


def _blank_spans(text, spans):
    if not spans:
        return text
    out, last = [], 0
    for a, b in spans:
        out.append(text[last:a])
        out.append(_blank(text[a:b]))
        last = b
    out.append(text[last:])
    return ''.join(out)


def matching(code, i, open_ch='(', close_ch=')'):
    """Index of the bracket that closes the one at code[i], or -1. Only safe on lexed code."""
    depth = 0
    n = len(code)
    j = i
    while j < n:
        c = code[j]
        if c == open_ch:
            depth += 1
        elif c == close_ch:
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return -1


def split_args(code, a, b):
    """Spans of the top-level, comma-separated arguments in code[a:b]."""
    spans, depth, start = [], 0, a
    for j in range(a, b):
        c = code[j]
        if c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
        elif c == ',' and depth == 0:
            spans.append((start, j))
            start = j + 1
    spans.append((start, b))
    return spans


class SourceFile(object):
    """One header or .cpp, lexed once."""

    def __init__(self, root, path):
        self.path = path
        self.rel = rel_path(root, path)
        self.text = read_text(path)
        code, nocom, pieces = lex(self.text)
        dead = dead_spans(code)
        self.code = _blank_spans(code, dead)
        self.nocomments = _blank_spans(nocom, dead)
        self.pieces = [p for p in pieces if not any(a <= p[0] < b for a, b in dead)]
        self._newlines = [m.start() for m in re.finditer('\n', self.text)]
        self._blocks = None
        self._reach = {}        # seed pattern -> (names reaching it, compiled alternation or None)

    def line_of(self, pos):
        return bisect.bisect_left(self._newlines, pos) + 1

    def line_text(self, line):
        """The raw text of a 1-based line."""
        start = self._newlines[line - 2] + 1 if line >= 2 else 0
        end = self._newlines[line - 1] if line - 1 < len(self._newlines) else len(self.text)
        return self.text[start:end]

    def is_preprocessor_line(self, pos):
        start = self.code.rfind('\n', 0, pos) + 1
        return self.code[start:pos].lstrip().startswith('#')

    def blocks(self):
        if self._blocks is None:
            self._blocks = find_blocks(self.code)
        return self._blocks


_FILE_CACHE = {}


def load(root, path):
    key = os.path.normcase(os.path.abspath(path))
    sf = _FILE_CACHE.get(key)
    if sf is None:
        sf = SourceFile(root, path)
        _FILE_CACHE[key] = sf
    return sf


def source_files(root, subdir='Source', exts=SOURCE_EXTS):
    base = os.path.join(root, subdir.replace('/', os.sep))
    found = []
    for path in glob.glob(os.path.join(base, '**', '*'), recursive=True):
        if path.lower().endswith(exts) and os.path.isfile(path):
            found.append(path)
    return sorted(found)


# ------------------------------------------------------------------------------------------------
# literals and constants inside macro arguments
# ------------------------------------------------------------------------------------------------

_STRING_LIT = re.compile(r'(?:u8|[uUL])?"((?:[^"\\\n]|\\.)*)"')
_IDENT = re.compile(r'^\s*([A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*$')


def _unescape(s):
    return re.sub(r'\\(.)', r'\1', s)


def literal_value(sf, a, b):
    """The string an argument spells when it is adjacent string literals, TEXT() allowed; else None."""
    raw = sf.nocomments[a:b]
    lits = _STRING_LIT.findall(raw)
    if not lits:
        return None
    rest = _STRING_LIT.sub('', raw)
    if re.sub(r'\bTEXT\b|[()\s]', '', rest):
        return None
    return ''.join(_unescape(x) for x in lits)


def constant_value(sf, ident):
    """The string a same-file constant or #define holds, for names written as `Name, Flags` with a variable."""
    name = re.escape(ident.split('::')[-1].strip())
    m = re.search(r'\b' + name + r'\s*(?:\[\s*\])?\s*=\s*(?:TEXT\s*\(\s*)?(?:u8|[uUL])?"((?:[^"\\\n]|\\.)*)"', sf.nocomments)
    if m:
        return _unescape(m.group(1))
    m = re.search(r'^[ \t]*#[ \t]*define[ \t]+' + name + r'[ \t]+(?:TEXT\s*\(\s*)?"((?:[^"\\\n]|\\.)*)"', sf.nocomments, re.M)
    if m:
        return _unescape(m.group(1))
    return None


def string_arg(sf, a, b):
    value = literal_value(sf, a, b)
    if value is not None:
        return value
    m = _IDENT.match(sf.code[a:b])
    if m:
        return constant_value(sf, m.group(1))
    return None


_FLAG = re.compile(r'\bEAutomationTestFlags\s*::\s*(\w+)')
FILTER_FLAGS = ('SmokeFilter', 'EngineFilter', 'ProductFilter', 'PerfFilter', 'StressFilter', 'NegativeFilter')
REQUESTED_FILTER_FLAGS = ('SmokeFilter', 'EngineFilter', 'ProductFilter', 'PerfFilter')


def flags_arg(sf, a, b):
    """The EAutomationTestFlags names an argument ORs together, or None when they cannot be read."""
    text = sf.code[a:b]
    found = _FLAG.findall(text)
    if found:
        return frozenset(found)
    m = _IDENT.match(text)
    if m:
        name = re.escape(m.group(1).split('::')[-1].strip())
        d = re.search(r'\b' + name + r'\s*=\s*([^;]+);', sf.code) or \
            re.search(r'^[ \t]*#[ \t]*define[ \t]+' + name + r'\b([^\n]*)$', sf.code, re.M)
        if d:
            found = _FLAG.findall(d.group(1))
            if found:
                return frozenset(found)
    return None


# ------------------------------------------------------------------------------------------------
# tests
# ------------------------------------------------------------------------------------------------

# macro -> (kind, index of the name argument, index of the flags argument). "complex" means the
# framework registers children under the declared name at run time (GetTests, specs, BDD), so
# only the prefix "<name>." is known from source.
TEST_MACROS = {
    'IMPLEMENT_SIMPLE_AUTOMATION_TEST': ('simple', 1, 2),
    'IMPLEMENT_COMPLEX_AUTOMATION_TEST': ('complex', 1, 2),
    'IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST': ('simple', 2, 3),
    'IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST': ('complex', 2, 3),
    'IMPLEMENT_NETWORKED_AUTOMATION_TEST': ('simple', 1, 2),
    'IMPLEMENT_BDD_AUTOMATION_TEST': ('complex', 1, 2),
    'BEGIN_DEFINE_SPEC': ('complex', 1, 2),
    'DEFINE_SPEC': ('complex', 1, 2),
}
_TEST_MACRO = re.compile(r'\b(' + '|'.join(sorted(TEST_MACROS, key=len, reverse=True)) + r')\s*\(')
_TAGS_MACRO = re.compile(r'\bREGISTER_SIMPLE_AUTOMATION_TEST_TAGS\s*\(')
_TAG = re.compile(r'\[([^\[\]]*)\]')


class Test(object):
    """One IMPLEMENT_*_AUTOMATION_TEST (or spec) as the framework will register it."""

    __slots__ = ('macro', 'kind', 'cls', 'path', 'flags', 'rel', 'line', 'sf', 'body', 'tags', 'tag_sites')

    def __init__(self, macro, kind, cls, path, flags, sf, line):
        self.macro = macro
        self.kind = kind
        self.cls = cls
        self.path = path
        self.flags = flags
        self.sf = sf
        self.rel = sf.rel
        self.line = line
        self.body = None        # (start, end) of the RunTest / Define body in sf.code
        self.tags = None        # list of tag names, when REGISTER_SIMPLE_AUTOMATION_TEST_TAGS names this class
        self.tag_sites = []     # every tag registration naming this class: TagSite

    def body_code(self):
        return self.sf.code[self.body[0]:self.body[1]] if self.body else ''


class TagSite(object):
    __slots__ = ('cls', 'path', 'raw', 'tags', 'rel', 'line')

    def __init__(self, cls, path, raw, tags, rel, line):
        self.cls = cls
        self.path = path        # the full name the registration files the tags under, or None
        self.raw = raw          # the tag string as written, or None when it cannot be read
        self.tags = tags        # parsed tag names, or None when raw is not a well-formed "[A][B]"
        self.rel = rel
        self.line = line


class Unreadable(object):
    __slots__ = ('macro', 'rel', 'line', 'why')

    def __init__(self, macro, rel, line, why):
        self.macro = macro
        self.rel = rel
        self.line = line
        self.why = why


def parse_tags(raw):
    """"[Pointer][Animated]" -> ['Pointer', 'Animated']; None unless the whole string is brackets."""
    if raw is None:
        return None
    s = raw.strip()
    if not s:
        return []
    if not re.fullmatch(r'(?:\s*\[[^\[\]]+\]\s*)+', s):
        return None
    return [t.strip() for t in _TAG.findall(s)]


def _find_body(sf, cls, method):
    """(start, end) of `cls::method(...) { ... }` in the file, skipping mere declarations."""
    head = re.compile(r'\b' + re.escape(cls) + r'\s*::\s*' + method + r'\s*\(')
    for m in head.finditer(sf.code):
        close = matching(sf.code, m.end() - 1)
        if close < 0:
            return None
        brace = sf.code.find('{', close)
        semi = sf.code.find(';', close)
        if brace >= 0 and (semi < 0 or brace < semi):
            end = matching(sf.code, brace, '{', '}')
            return (brace, end + 1) if end > 0 else None
    return None


def scan_file_tests(sf):
    """(tests, tag sites, unreadable declarations) of one file."""
    tests, sites, bad = [], [], []
    for m in _TEST_MACRO.finditer(sf.code):
        if sf.is_preprocessor_line(m.start()):
            continue
        macro = m.group(1)
        kind, name_i, flags_i = TEST_MACROS[macro]
        line = sf.line_of(m.start())
        close = matching(sf.code, m.end() - 1)
        if close < 0:
            bad.append(Unreadable(macro, sf.rel, line, 'unbalanced parentheses'))
            continue
        args = split_args(sf.code, m.end(), close)
        if len(args) <= max(name_i, flags_i):
            bad.append(Unreadable(macro, sf.rel, line, 'expected at least %d arguments' % (max(name_i, flags_i) + 1)))
            continue
        cls = sf.code[args[0][0]:args[0][1]].strip()
        path = string_arg(sf, *args[name_i])
        flags = flags_arg(sf, *args[flags_i])
        if not re.fullmatch(r'[A-Za-z_]\w*', cls or ''):
            bad.append(Unreadable(macro, sf.rel, line, 'the class argument is not a plain name'))
            continue
        if path is None:
            bad.append(Unreadable(macro, sf.rel, line, 'the test name is neither a string literal nor a same-file constant'))
        if flags is None:
            bad.append(Unreadable(macro, sf.rel, line, 'the flags do not spell EAutomationTestFlags::... in this file'))
        t = Test(macro, kind, cls, path, flags, sf, line)
        t.body = _find_body(sf, cls, 'Define' if 'SPEC' in macro else 'RunTest')
        tests.append(t)
    for m in _TAGS_MACRO.finditer(sf.code):
        if sf.is_preprocessor_line(m.start()):
            continue
        line = sf.line_of(m.start())
        close = matching(sf.code, m.end() - 1)
        args = split_args(sf.code, m.end(), close) if close > 0 else []
        if len(args) < 3:
            sites.append(TagSite(None, None, None, None, sf.rel, line))
            continue
        cls = sf.code[args[0][0]:args[0][1]].strip()
        path = string_arg(sf, *args[1])
        raw = string_arg(sf, *args[2])
        sites.append(TagSite(cls, path, raw, parse_tags(raw), sf.rel, line))
    return tests, sites, bad


class Scan(object):
    """Every test the plugin declares, with its tag registrations attached."""

    def __init__(self, root):
        self.root = root
        self.tests = []
        self.tag_sites = []
        self.unreadable = []
        for path in source_files(root, 'Source', ('.cpp', '.h')):
            # Lexing is the expensive part; most of the runtime module declares no test at all.
            try:
                with open(path, 'r', encoding='utf-8-sig', errors='replace') as f:
                    raw = f.read()
            except OSError:
                continue
            if 'AUTOMATION_TEST' not in raw and 'DEFINE_SPEC' not in raw:
                continue
            sf = load(root, path)
            tests, sites, bad = scan_file_tests(sf)
            self.tests.extend(tests)
            self.tag_sites.extend(sites)
            self.unreadable.extend(bad)
        by_cls = {}
        for t in self.tests:
            by_cls.setdefault(t.cls, []).append(t)
        for site in self.tag_sites:
            for t in by_cls.get(site.cls, []):
                t.tag_sites.append(site)
                if t.tags is None and site.tags is not None:
                    t.tags = list(site.tags)

    def named(self):
        return [t for t in self.tests if t.path]


_SCANS = {}


def scan(root=None):
    root = plugin_root(root)
    key = os.path.normcase(root)
    if key not in _SCANS:
        _SCANS[key] = Scan(root)
    return _SCANS[key]


# ------------------------------------------------------------------------------------------------
# what a body reaches, one file deep
# ------------------------------------------------------------------------------------------------

_DEF_HEAD = re.compile(r'(?:\b([A-Za-z_]\w*)\s*::\s*)?(~?[A-Za-z_]\w*)\s*\(')
_TYPE_HEAD = re.compile(r'\b(?:class|struct)\s+(?:[A-Z0-9_]+_API\s+)?([A-Za-z_]\w*)\s*(?:final\s*)?(?::[^;{}()]*)?\{')
_NOT_FUNCTIONS = frozenset((
    'if', 'for', 'while', 'switch', 'catch', 'return', 'sizeof', 'decltype', 'alignof', 'alignas',
    'static_assert', 'defined', 'noexcept', 'TEXT', 'new', 'delete', 'operator', 'throw'))
# Names too common to be followed from one block to another: every file has a RunTest, and a
# helper called Get or Run says nothing about what it does.
_TOO_GENERIC = frozenset((
    'RunTest', 'GetTests', 'Define', 'Get', 'Set', 'Run', 'Make', 'Init', 'Tick', 'Update', 'Reset',
    'Execute', 'Check', 'Test', 'IsValid', 'Begin', 'End', 'Start', 'Stop', 'Step', 'Main'))


def find_blocks(code):
    """[(names, start, end)]: function definitions (named after the function and its class) and
    class/struct definitions (named after the type), as spans of code. Imprecise on purpose -- a
    call that happens to be followed by a brace adds a harmless extra block -- because all it
    feeds is "which names lead to which calls"."""
    blocks = []
    for m in _DEF_HEAD.finditer(code):
        name = m.group(2)
        if name in _NOT_FUNCTIONS or re.fullmatch(r'[A-Z0-9_]+', name):
            continue
        close = matching(code, m.end() - 1)
        if close < 0:
            continue
        k = close + 1
        tail = re.match(r'\s*(?:(?:const|override|final|noexcept|volatile|mutable)\b\s*|->\s*[\w:<>,\s\*&]+?(?=\{)|&&?\s*)*', code[k:k + 200])
        k += tail.end() if tail else 0
        if k < len(code) and code[k] == ':' and code[k:k + 2] != '::':
            nxt = code.find('{', k)
            semi = code.find(';', k)
            if nxt < 0 or (0 <= semi < nxt):
                continue
            k = nxt
        if k >= len(code) or code[k] != '{':
            continue
        end = matching(code, k, '{', '}')
        if end < 0:
            continue
        names = set([name.lstrip('~')])
        if m.group(1):
            names.add(m.group(1))
        blocks.append((frozenset(names), k, end + 1))
    for m in _TYPE_HEAD.finditer(code):
        brace = m.end() - 1
        end = matching(code, brace, '{', '}')
        if end > 0:
            blocks.append((frozenset([m.group(1)]), brace, end + 1))
    return blocks


def _usable(name):
    return len(name) >= 4 and name not in _TOO_GENERIC


def names_reaching(sf, seed):
    """Names of the file's blocks from which a match of seed can be reached through other blocks
    of the same file. Headers are not followed: a helper that must be seen belongs in the file."""
    cached = sf._reach.get(seed.pattern)
    if cached is not None:
        return cached[0]
    marked = _names_reaching(sf, seed)
    pattern = re.compile(r'\b(?:' + '|'.join(re.escape(n) for n in sorted(marked)) + r')\b') if marked else None
    sf._reach[seed.pattern] = (marked, pattern)
    return marked


def _names_reaching(sf, seed):
    blocks = sf.blocks()
    marked = set()
    pending = []
    for names, a, b in blocks:
        if seed.search(sf.code, a, b):
            marked.update(n for n in names if _usable(n))
        else:
            pending.append((names, a, b))
    changed = True
    while changed and marked and pending:
        changed = False
        pattern = re.compile(r'\b(?:' + '|'.join(re.escape(n) for n in sorted(marked)) + r')\b')
        rest = []
        for names, a, b in pending:
            if pattern.search(sf.code, a, b):
                new = set(n for n in names if _usable(n)) - marked
                if new:
                    marked.update(new)
                    changed = True
            else:
                rest.append((names, a, b))
        pending = rest
    return marked


def body_reaches(test, seed):
    """Whether the test's body matches seed itself or names a same-file block that leads to it."""
    if not test.body:
        return False
    a, b = test.body
    if seed.search(test.sf.code, a, b):
        return True
    names_reaching(test.sf, seed)
    pattern = test.sf._reach[seed.pattern][1]
    return pattern is not None and pattern.search(test.sf.code, a, b) is not None


# ------------------------------------------------------------------------------------------------
# presets -> engine filter, and the engine filter -> expected tests
# ------------------------------------------------------------------------------------------------

def _prefix_match(path, prefix):
    p, x = path.lower(), prefix.lower()
    if x.endswith('.'):
        return p.startswith(x) or p == x[:-1]
    return p == x or p.startswith(x + '.')


def select(tests, spec):
    """The declared tests a preset selects. Each criterion present narrows the selection:
    include (any of these name prefixes), sources (declared in a file under any of these
    directories, relative to Source/DreamGUITests/), flags (carries every one of these
    EAutomationTestFlags); exclude (name prefixes) then removes."""
    include = spec.get('include') or []
    sources = [(TEST_MODULE_DIR + '/' + s.strip('/') + '/').lower() for s in (spec.get('sources') or [])]
    flags = spec.get('flags') or []
    exclude = spec.get('exclude') or []
    if not (include or sources or flags):
        raise ValueError('a preset must select with at least one of include, sources or flags')
    out = []
    for t in tests:
        if not t.path:
            continue
        if include and not any(_prefix_match(t.path, p) for p in include):
            continue
        if sources and not any(t.rel.lower().startswith(s) for s in sources):
            continue
        if flags and (t.flags is None or not all(f in t.flags for f in flags)):
            continue
        if any(_prefix_match(t.path, p) for p in exclude):
            continue
        out.append(t)
    return out


class _Node(object):
    __slots__ = ('children', 'leaf', 'leaf_selected')

    def __init__(self):
        self.children = {}
        self.leaf = None            # 'simple' / 'complex' when a declared test ends here
        self.leaf_selected = False


def compress(selected, declared):
    """The shortest RunTests term list that matches exactly the selected tests among the declared
    ones: a name-tree node whose declared tests are all selected becomes one StartsWith: term."""
    chosen = set(id(t) for t in selected)
    root = _Node()
    for t in declared:
        if not t.path:
            continue
        node = root
        for seg in t.path.split('.'):
            node = node.children.setdefault(seg, _Node())
        if node.leaf != 'complex':
            node.leaf = t.kind
        node.leaf_selected = node.leaf_selected or id(t) in chosen

    memo = {}

    def state(node):
        key = id(node)
        if key not in memo:
            any_sel = node.leaf is not None and node.leaf_selected
            all_sel = node.leaf is None or node.leaf_selected
            for child in node.children.values():
                a, s = state(child)
                any_sel = any_sel or a
                all_sel = all_sel and s
            memo[key] = (any_sel, all_sel)
        return memo[key]

    def emit(node, path):
        any_sel, all_sel = state(node)
        if not any_sel:
            return []
        if all_sel and node.children:
            terms = ['StartsWith:' + path]
            if node.leaf == 'simple':
                terms.append('^' + path + '$')
            return terms
        terms = []
        if node.leaf is not None and node.leaf_selected:
            terms.append(('StartsWith:' + path) if node.leaf == 'complex' else ('^' + path + '$'))
        for seg in sorted(node.children):
            terms.extend(emit(node.children[seg], path + '.' + seg))
        return terms

    terms = []
    for seg in sorted(root.children):
        terms.extend(emit(root.children[seg], seg))
    return terms


def load_presets(path=None):
    data = read_json(path or DEFAULT_PRESETS)
    presets = data.get('presets') if isinstance(data, dict) else None
    if not isinstance(presets, dict) or not presets:
        raise ValueError('%s has no "presets" object' % (path or DEFAULT_PRESETS))
    return data, presets


def preset_spec(presets, name):
    for key, spec in presets.items():
        if key.lower() == name.lower():
            return key, spec
    raise KeyError('no preset named %s (have: %s)' % (name, ', '.join(sorted(presets))))


def resolve(root, spec):
    """(filter text, selected tests) for a preset spec; a spec with "filter" is taken verbatim."""
    s = scan(root)
    if spec.get('filter'):
        return spec['filter'], None
    selected = select(s.named(), spec)
    terms = compress(selected, s.named())
    return '+'.join(terms), selected


def parse_filter(text):
    """The RunTests argument as AutomationCommandline.cpp reads it, or None for a Group: term (its
    meaning lives in an ini this module does not read)."""
    terms = []
    for raw in text.split('+'):
        a = raw.strip()
        if not a:
            continue
        # FString::StartsWith ignores case by default, so the engine takes "startswith:" as well.
        if a.lower().startswith('startswith:'):
            name = a[len('StartsWith:'):].strip()
            if not name.endswith('.'):
                name += '.'
            terms.append(('start', name.lower(), False, False))
        elif a.lower().startswith('group:'):
            return None
        else:
            start, end = a.startswith('^'), a.endswith('$')
            if start:
                a = a[1:]
            if end:
                a = a[:-1]
            terms.append(('match', a.lower(), start, end))
    return terms


def filter_matches(terms, path):
    p = path.lower()
    for kind, s, start, end in terms:
        if kind == 'start':
            if p.startswith(s):
                return True
        elif start or end:
            ok = True
            if start:
                ok = p.startswith(s)
            if end and ok:
                ok = p.endswith(s)
            if ok:
                return True
        elif s in p:
            return True
    return False


def runnable(test, args):
    """Whether GetValidTestNames lists the test in an unattended editor started with args; None
    when its flags could not be read."""
    f = test.flags
    if f is None:
        return None
    lowered = [a.lower() for a in (args or [])]
    if 'Disabled' in f or 'RequiresUser' in f:
        return False
    if 'EditorContext' not in f:
        return False
    if not any(x in f for x in REQUESTED_FILTER_FLAGS):
        return False
    if 'NonNullRHI' in f and '-nullrhi' in lowered:
        return False
    return True


def expected(root, filter_text, args):
    """(simple test paths, complex prefixes) the engine should run for this filter and these
    arguments, from the declared tests; None when the filter cannot be interpreted here."""
    terms = parse_filter(filter_text or '')
    if terms is None:
        return None
    simple, complex_ = set(), set()
    for t in scan(root).named():
        if runnable(t, args) is not True:
            continue
        if t.kind == 'simple':
            if filter_matches(terms, t.path):
                simple.add(t.path)
        elif filter_matches(terms, t.path + '.x'):
            # Its children are "<path>.<something>"; which ones exist is only known at run time.
            complex_.add(t.path)
    return simple, complex_


# ------------------------------------------------------------------------------------------------
# command line
# ------------------------------------------------------------------------------------------------

def _cmd_filter(ns):
    _, presets = load_presets(ns.presets)
    _, spec = preset_spec(presets, ns.preset)
    text, selected = resolve(plugin_root(ns.root), spec)
    if not text:
        sys.stderr.write('preset %s selects no declared test under %s\n' % (ns.preset, plugin_root(ns.root)))
        return 2
    if selected is not None:
        sys.stderr.write('preset %s: %d declared tests, %d filter terms\n' % (ns.preset, len(selected), text.count('+') + 1))
    sys.stdout.write(text + '\n')
    return 0


def _cmd_tests(ns):
    s = scan(plugin_root(ns.root))
    rows = []
    for t in s.tests:
        rows.append({'path': t.path, 'kind': t.kind, 'class': t.cls, 'flags': sorted(t.flags) if t.flags else None,
                     'tags': t.tags, 'file': t.rel, 'line': t.line})
    if ns.json:
        json.dump({'tests': rows, 'unreadable': [{'macro': u.macro, 'file': u.rel, 'line': u.line, 'why': u.why} for u in s.unreadable]},
                  sys.stdout, indent=1)
        sys.stdout.write('\n')
    else:
        for r in rows:
            sys.stdout.write('%s\t%s\t%s\t%s:%d\n' % (r['path'], r['kind'], ','.join(r['flags'] or []), r['file'], r['line']))
        sys.stderr.write('%d tests, %d unreadable declarations\n' % (len(rows), len(s.unreadable)))
    return 0


def main(argv=None):
    console_safe()
    ap = argparse.ArgumentParser(description='Read the automation tests the plugin declares.')
    sub = ap.add_subparsers(dest='cmd')
    f = sub.add_parser('filter', help='print the RunTests filter a preset resolves to')
    f.add_argument('--preset', required=True)
    f.add_argument('--presets', default=None, help='presets file (default: presets.json beside this script)')
    f.add_argument('--root', default=None, help='plugin root (default: the checkout holding this script)')
    t = sub.add_parser('tests', help='list the declared tests')
    t.add_argument('--root', default=None)
    t.add_argument('--json', action='store_true')
    ns = ap.parse_args(argv)
    try:
        if ns.cmd == 'filter':
            return _cmd_filter(ns)
        if ns.cmd == 'tests':
            return _cmd_tests(ns)
    except (OSError, ValueError, KeyError) as e:
        sys.stderr.write('sourcescan: %s\n' % e)
        return 2
    ap.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
