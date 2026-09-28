"""Move the files Tools/Tests/module-owners.csv assigns to a module out of Source/DreamGUI into Source/<Module>.

    python move_module_files.py <Module>            # say what would happen
    python move_module_files.py <Module> --apply    # do it: edits, git mv, module-owners.csv

What it does:
- keeps each file's path under its module (Public/..., Private/...), so every include written module-relative
  -- "Controls/DreamButton.h" -- keeps working once the new module is a dependency;
- rewrites the includes that worked only because the two files sat in the same module: a same-directory or
  "../" include, and one that names the core's directory ("DreamGUI/Public/..."), into the module-relative
  form;
- replaces DREAMGUI_API with the new module's API macro in the moved files;
- `git mv`s the files, which stages the renames: check `git diff --cached --stat` before committing
  anything else;
- rewrites module-owners.csv's paths in place.

It refuses (unless --force) when a moved file includes a core Private header, or a file that stays in the core
includes a moved one: an edge that has to be cut before the move. Line endings and BOMs are kept.
"""
import argparse
import csv
import io
import os
import re
import subprocess
import sys

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
OWNERS = os.path.join(REPO, 'Tools', 'Tests', 'module-owners.csv')
INCLUDE = re.compile(rb'^([ \t]*#[ \t]*include[ \t]*")([^"]+)(")', re.M)


def git(*args):
    return subprocess.run(['git', '-C', REPO] + list(args), capture_output=True, text=True, check=True).stdout


def exists(rel):
    return os.path.isfile(os.path.join(REPO, rel))


def norm(p):
    return os.path.normpath(p).replace(os.sep, '/')


def module_of(rel):
    return rel.split('/')[1]


def module_relative(rel):
    """("Controls/DreamButton.h", "Public") for Source/<M>/Public/Controls/DreamButton.h."""
    parts = rel.split('/')
    return '/'.join(parts[3:]), parts[2]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--force', action='store_true', help='apply although edges to cut were found')
    ns = ap.parse_args()
    api = ns.module.upper() + '_API'

    raw = open(OWNERS, 'rb').read()
    rows = list(csv.DictReader(io.StringIO(raw.decode('utf-8-sig'))))
    moving = {}
    for r in rows:
        if r['module'] == ns.module and r['path'].startswith('Source/DreamGUI/'):
            moving[r['path']] = 'Source/%s/' % ns.module + r['path'][len('Source/DreamGUI/'):]
    missing = [p for p in moving if not exists(p)]
    if missing:
        sys.exit('listed but not on disk: %s' % missing)
    print('%d files move to %s' % (len(moving), ns.module))

    source_modules = [d for d in os.listdir(os.path.join(REPO, 'Source')) if os.path.isdir(os.path.join(REPO, 'Source', d))]

    def resolve(includer, inc):
        """How an include resolves today: ('relative' | 'module' | 'sourcedir', repository path), or None."""
        inc = inc.replace('\\', '/')
        rel = norm(os.path.join(os.path.dirname(includer), inc))
        if exists(rel):
            return 'relative', rel
        for m in [module_of(includer)] + source_modules:
            for sub in ('Public', 'Private', 'Classes'):
                cand = 'Source/%s/%s/%s' % (m, sub, inc)
                if exists(cand):
                    return 'module', cand
        if exists('Source/' + inc):
            return 'sourcedir', 'Source/' + inc
        return None

    problems = []
    edits = {}
    all_files = []
    for root, _, names in os.walk(os.path.join(REPO, 'Source')):
        for n in names:
            if n.endswith(('.h', '.cpp', '.inl')):
                all_files.append(norm(os.path.relpath(os.path.join(root, n), REPO)))

    for f in all_files:
        data = open(os.path.join(REPO, f), 'rb').read()
        f_moves = f in moving
        changed = [False]

        def fix(m):
            how = resolve(f, m.group(2).decode('utf-8'))
            if how is None:
                return m.group(0)
            kind, target = how
            t_moves = target in moving
            if kind == 'sourcedir' and (f_moves or t_moves):
                sub_path, sub = module_relative(target)
                if sub != 'Public' and module_of(target) != module_of(f):
                    problems.append('%s includes the private %s' % (f, target))
                    return m.group(0)
                changed[0] = True
                return m.group(1) + sub_path.encode('utf-8') + m.group(3)
            if f_moves and not t_moves and module_of(target) == 'DreamGUI':
                sub_path, sub = module_relative(target)
                if sub != 'Public':
                    problems.append('%s includes the core private %s' % (f, target))
                    return m.group(0)
                if kind == 'relative':
                    changed[0] = True
                    return m.group(1) + sub_path.encode('utf-8') + m.group(3)
            if not f_moves and t_moves:
                if module_of(f) == 'DreamGUI':
                    problems.append('%s, which stays in the core, includes %s' % (f, target))
                elif kind == 'relative':
                    sub_path, sub = module_relative(target)
                    changed[0] = True
                    return m.group(1) + sub_path.encode('utf-8') + m.group(3)
            return m.group(0)

        new = INCLUDE.sub(fix, data)
        if f_moves:
            renamed = new.replace(b'DREAMGUI_API', api.encode('utf-8'))
            if renamed != new:
                changed[0] = True
                new = renamed
        if changed[0]:
            edits[f] = new

    print('%d files edited' % len(edits))
    for f in sorted(edits):
        before = set(x[1] for x in INCLUDE.findall(open(os.path.join(REPO, f), 'rb').read()))
        after = set(x[1] for x in INCLUDE.findall(edits[f]))
        for x in sorted(before - after):
            print('  %s: -%s' % (f, x.decode()))
        for x in sorted(after - before):
            print('  %s: +%s' % (f, x.decode()))
    if problems:
        print('edges to cut first:')
        for p in problems:
            print('  ' + p)

    if not ns.apply:
        return
    if problems and not ns.force:
        sys.exit('not applied: cut the edges above first, or pass --force when they move in the same step')
    for f, data in edits.items():
        open(os.path.join(REPO, f), 'wb').write(data)
    for old, new in sorted(moving.items()):
        os.makedirs(os.path.join(REPO, os.path.dirname(new)), exist_ok=True)
        git('mv', old, new)
    for r in rows:
        if r['path'] in moving:
            r['path'] = moving[r['path']]
    buf = io.StringIO(newline='')
    writer = csv.DictWriter(buf, fieldnames=['path', 'module', 'reason'],
                            lineterminator='\r\n' if b'\r\n' in raw else '\n')
    writer.writeheader()
    writer.writerows(rows)
    open(OWNERS, 'w', encoding='utf-8-sig' if raw.startswith(b'\xef\xbb\xbf') else 'utf-8', newline='').write(buf.getvalue())
    print('moved; the renames are staged')


if __name__ == '__main__':
    main()
