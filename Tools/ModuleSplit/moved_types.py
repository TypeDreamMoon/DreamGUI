"""The reflected types a module split moved out of the core, and the CoreRedirects old assets would need.

    python moved_types.py <Module>               # list them, old path -> new path
    python moved_types.py <Module> --redirects   # print a [CoreRedirects] block for them instead

Run after move_module_files.py. Reads every header module-owners.csv lists for <Module> -- now under
Source/<Module> -- and finds each UCLASS, UINTERFACE, USTRUCT and UENUM and every file-scope dynamic delegate
(a delegate declared inside a class moves with its class and needs nothing). Code under `#if 0` is skipped.

The plugin ships no CoreRedirects (DreamGUI.Packaging.ThePluginShipsNoCoreRedirects), so nothing here writes
into its config. The block --redirects prints has one entry per type from /Script/DreamGUI.<Name> to
/Script/<Module>.<Name>, grouped by header: classes and interfaces without their U/A prefix, structs without F,
enums with their E, and a global delegate's signature twice, as an object and as a function. It is for a
project's own Config/DefaultEngine.ini, for as long as it takes to resave the assets that name those types --
the plugin's own included -- and for the CHANGELOG to hand on. retarget_script_paths.py reads the names from
here. DreamGUI.Packaging.EveryTypeInASplitOffModuleAnswersToItsOldCorePath checks, against the types the
running process holds, that each one still answers to its old path where the plugin itself resolves one.
"""
import argparse
import csv
import io
import os
import re
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OWNERS = os.path.join(REPO, 'Tools', 'Tests', 'module-owners.csv')
ORDER = ['Class', 'Struct', 'Enum', 'Delegate']


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


def moved(module):
    """{kind: set of names} for the types the headers module-owners.csv gives <module> declare, and the blocks
    of redirect lines for them, one per header."""
    rows = list(csv.DictReader(io.StringIO(open(OWNERS, 'rb').read().decode('utf-8-sig'))))
    headers = sorted(r['path'] for r in rows if r['module'] == module and r['path'].endswith('.h'))
    if not headers:
        sys.exit('module-owners.csv gives %s no headers' % module)
    if not all(h.startswith('Source/%s/' % module) for h in headers):
        sys.exit('move the files first (move_module_files.py)')

    found = {k: set() for k in ORDER}
    blocks = []
    for h in headers:
        types = scan(os.path.join(REPO, h))
        if not types:
            continue
        lines = ['; ' + h[len('Source/%s/' % module):]]
        for kind, name in sorted(types, key=lambda t: (ORDER.index(t[0]), t[1])):
            found[kind].add(name)
            old, new = '/Script/DreamGUI.' + name, '/Script/%s.%s' % (module, name)
            if kind == 'Delegate':
                lines.append('+ObjectRedirects=(OldName="%s",NewName="%s")' % (old, new))
                lines.append('+FunctionRedirects=(OldName="%s",NewName="%s")' % (old, new))
            else:
                lines.append('+%sRedirects=(OldName="%s",NewName="%s")' % (kind, old, new))
        blocks.append('\n'.join(lines))
    return found, blocks


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--redirects', action='store_true', help='print a [CoreRedirects] block instead of the list')
    ns = ap.parse_args()

    found, blocks = moved(ns.module)
    if ns.redirects:
        print('[CoreRedirects]')
        print('; %s: the reflected types that moved into this module, each from its old /Script/DreamGUI path.' % ns.module)
        print('\n\n'.join(blocks))
        return
    for kind in ORDER:
        for name in sorted(found[kind]):
            print('%-8s /Script/DreamGUI.%s -> /Script/%s.%s' % (kind, name, ns.module, name))
    print('types: ' + ', '.join('%s %d' % (k, len(found[k])) for k in ORDER))


if __name__ == '__main__':
    main()
