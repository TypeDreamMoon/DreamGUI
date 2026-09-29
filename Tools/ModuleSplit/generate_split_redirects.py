"""CoreRedirects for the reflected types a module split moved out of the core.

    python generate_split_redirects.py <Module>                         # print the block and the retargets
    python generate_split_redirects.py <Module> --apply [--note "..."]  # append them to Config/DefaultDreamGUI.ini

Run after move_module_files.py. Reads every header module-owners.csv lists for <Module> -- now under
Source/<Module> -- and finds each UCLASS, UINTERFACE, USTRUCT and UENUM and every file-scope dynamic delegate
(a delegate declared inside a class moves with its class and needs nothing). Then:

- one entry per type from /Script/DreamGUI.<Name> to /Script/<Module>.<Name>, grouped by header: classes and
  interfaces without their U/A prefix, structs without F, enums with their E, and a global delegate's
  signature twice, as an object and as a function;
- every existing entry whose NewName is /Script/DreamGUI.<one of those names> is pointed at the new package,
  because a redirect does not chain: an LGUI entry that led to the old core path would lead nowhere.

It refuses when an OldName it would add is already an OldName in the file. Code under `#if 0` is skipped.
DreamGUI.Packaging.EveryTypeInASplitOffModuleAnswersToItsOldCorePath checks the result against the types
the running process holds.
"""
import argparse
import csv
import io
import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
INI = os.path.join(REPO, 'Config', 'DefaultDreamGUI.ini')
OWNERS = os.path.join(REPO, 'Tools', 'Tests', 'module-owners.csv')


def strip_comments(text):
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(''.join(c if c == '\n' else ' ' for c in text[i:j]))
            i = j
        elif text[i] == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == '\\' else 1
            out.append('"' + ' ' * (j - i - 1) + '"')
            i = j + 1
        else:
            out.append(text[i])
            i += 1
    return ''.join(out)


def strip_if0(text):
    """Blank every `#if 0` region up to its #else or #endif."""
    out, depth, dead_at = [], 0, None
    for line in text.split('\n'):
        s = line.strip()
        if re.match(r'#\s*if(def|ndef)?\b', s):
            depth += 1
            if dead_at is None and re.match(r'#\s*if\s+0\b', s):
                dead_at = depth
                out.append('')
                continue
        elif re.match(r'#\s*endif\b', s):
            if dead_at == depth:
                dead_at = None
                depth -= 1
                out.append('')
                continue
            depth -= 1
        elif re.match(r'#\s*(else|elif)\b', s) and dead_at == depth:
            dead_at = None
            out.append('')
            continue
        out.append('' if dead_at is not None else line)
    return '\n'.join(out)


def skip_parens(text, i):
    depth = 0
    while i < len(text):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError('unbalanced parentheses')


DECL = re.compile(r'\b(UCLASS|UINTERFACE|USTRUCT|UENUM)\s*\(')
AFTER = {
    'UCLASS': re.compile(r'\s*class\s+(?:\w+_API\s+)?([UA]\w+)'),
    'UINTERFACE': re.compile(r'\s*class\s+(?:\w+_API\s+)?(U\w+)'),
    'USTRUCT': re.compile(r'\s*struct\s+(?:\w+_API\s+)?(F\w+)'),
    'UENUM': re.compile(r'\s*(?:enum\s+class|enum|namespace)\s+(E\w+)'),
}
DELEGATE = re.compile(r'\bDECLARE_DYNAMIC_(?:MULTICAST_)?(?:SPARSE_)?DELEGATE(\w*)\s*\(\s*([^,()]+)(?:,\s*([^,()]+))?')


def scan(path):
    """(kind, name) of every reflected type the header declares, kind in Class, Struct, Enum, Delegate."""
    text = strip_if0(strip_comments(open(path, encoding='utf-8-sig', errors='replace').read()))
    found = []
    for m in DECL.finditer(text):
        end = skip_parens(text, m.end() - 1)
        a = AFTER[m.group(1)].match(text, end)
        if not a:
            continue
        name = a.group(1)
        if m.group(1) in ('UCLASS', 'UINTERFACE'):
            found.append(('Class', name[1:]))
        elif m.group(1) == 'USTRUCT':
            found.append(('Struct', name[1:]))
        else:
            found.append(('Enum', name))
    for m in DELEGATE.finditer(text):
        if 'SPARSE' in m.group(0):
            continue
        # A RetVal form puts the return type first and the delegate's name second.
        name = ((m.group(3) if 'RetVal' in m.group(1) else m.group(2)) or '').strip()
        if not re.match(r'F\w+$', name):
            continue
        namespaces = len(re.findall(r'\bnamespace\s+\w+\s*\{', text[:m.start()]))
        if text.count('{', 0, m.start()) - text.count('}', 0, m.start()) - namespaces == 0:
            found.append(('Delegate', name[1:] + '__DelegateSignature'))
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--note', default=None, help='the comment block above the new entries, lines starting with ;')
    ns = ap.parse_args()

    rows = list(csv.DictReader(io.StringIO(open(OWNERS, 'rb').read().decode('utf-8-sig'))))
    headers = sorted(r['path'] for r in rows if r['module'] == ns.module and r['path'].endswith('.h'))
    if not all(h.startswith('Source/%s/' % ns.module) for h in headers):
        sys.exit('move the files first (move_module_files.py)')

    order = ['Class', 'Struct', 'Enum', 'Delegate']
    moved = {k: set() for k in order}
    blocks = []
    for h in headers:
        types = scan(os.path.join(REPO, h))
        if not types:
            continue
        lines = ['; ' + h[len('Source/%s/' % ns.module):]]
        for kind, name in sorted(types, key=lambda t: (order.index(t[0]), t[1])):
            moved[kind].add(name)
            old, new = '/Script/DreamGUI.' + name, '/Script/%s.%s' % (ns.module, name)
            if kind == 'Delegate':
                lines.append('+ObjectRedirects=(OldName="%s",NewName="%s")' % (old, new))
                lines.append('+FunctionRedirects=(OldName="%s",NewName="%s")' % (old, new))
            else:
                lines.append('+%sRedirects=(OldName="%s",NewName="%s")' % (kind, old, new))
        blocks.append('\n'.join(lines))
    print('types: ' + ', '.join('%s %d' % (k, len(moved[k])) for k in order))

    raw = open(INI, 'rb').read()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8-sig').replace('\r\n', '\n')
    entry = re.compile(r'^\+(\w+)Redirects=\(OldName="([^"]+)",NewName="([^"]+)"(.*)\)\s*$', re.M)
    existing_old = set(m.group(2) for m in entry.finditer(text))
    clash = [n for b in blocks for n in re.findall(r'OldName="([^"]+)"', b) if n in existing_old]
    if clash:
        sys.exit('already an OldName in the file: %s' % clash)

    kind_of_entry = {'Class': 'Class', 'Struct': 'Struct', 'Enum': 'Enum', 'Object': 'Delegate', 'Function': 'Delegate'}
    retargets = []

    def retarget(m):
        kind, old, new, rest = m.group(1), m.group(2), m.group(3), m.group(4)
        k = kind_of_entry.get(kind)
        if k and new.startswith('/Script/DreamGUI.') and new[len('/Script/DreamGUI.'):] in moved[k]:
            nn = '/Script/%s.%s' % (ns.module, new[len('/Script/DreamGUI.'):])
            retargets.append((kind, old, nn))
            return '+%sRedirects=(OldName="%s",NewName="%s"%s)' % (kind, old, nn, rest)
        return m.group(0)

    text = entry.sub(retarget, text)
    print('existing entries pointed at the new package: %d' % len(retargets))
    for r in retargets:
        print('  %s %s -> %s' % r)
    block = '\n\n'.join(blocks)
    print(block)
    if not ns.apply:
        return
    note = ns.note or ('; %s: the reflected types declared in the files that moved into this module, each from its old\n'
                       '; /Script/DreamGUI path. Generated from the moved headers.' % ns.module)
    text = text.rstrip('\n') + '\n\n' + note.rstrip('\n') + '\n' + block + '\n'
    if crlf:
        text = text.replace('\n', '\r\n')
    open(INI, 'wb').write((b'\xef\xbb\xbf' if raw.startswith(b'\xef\xbb\xbf') else b'') + text.encode('utf-8'))
    print('written to Config/DefaultDreamGUI.ini')


if __name__ == '__main__':
    main()
