"""Point every "/Script/DreamGUI.<Name>" a split moved at the type's new package, in the plugin's own text.

    python retarget_script_paths.py <Module>            # list what would change
    python retarget_script_paths.py <Module> --apply

Run after move_module_files.py: the moved names come from moved_types.py, which reads them off the moved
headers. Every tracked text file under Source/, Docs/ (but Docs/Reference, which is regenerated) and the README
is rewritten; metadata strings such as AllowedClasses and MustImplement need this most, because the engine
reads them as they are.

Left alone, and listed for a look: the test module, where an old path is often the point of a test, and the
old-asset snapshot, which records paths as they were when it was taken.
"""
import argparse
import os
import re
import subprocess

import moved_types

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
TEXT_EXTS = ('.h', '.cpp', '.inl', '.cs', '.md', '.ini', '.dui', '.json', '.py', '.ps1', '.txt')
SKIP = ('Tools/TestHost/Template/Content/DreamGUIFixtures/Snapshot.txt',)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--apply', action='store_true')
    ns = ap.parse_args()

    found, _ = moved_types.moved(ns.module)
    names = set().union(*found.values())
    if not names:
        raise SystemExit('the headers that moved into %s declare no reflected types' % ns.module)
    pattern = re.compile(r'/Script/DreamGUI\.(' + '|'.join(sorted(map(re.escape, names), key=len, reverse=True)) + r')(?![A-Za-z0-9_])')
    files = subprocess.run(['git', '-C', REPO, 'ls-files'], capture_output=True, text=True, check=True).stdout.split('\n')
    changed, review = 0, []
    for rel in files:
        if not rel.endswith(TEXT_EXTS) or rel in SKIP:
            continue
        path = os.path.join(REPO, rel)
        if not os.path.isfile(path):  # deleted, not yet committed
            continue
        raw = open(path, 'rb').read()
        text = raw.decode('utf-8', errors='surrogateescape')
        hits = pattern.findall(text)
        if not hits:
            continue
        if rel.startswith('Source/DreamGUITests/') or rel.startswith('Docs/Reference/'):
            review.append('%s: %s' % (rel, ', '.join(sorted(set(hits)))))
            continue
        changed += 1
        print('%s: %s' % (rel, ', '.join(sorted(set(hits)))))
        if ns.apply:
            text = pattern.sub(lambda m: '/Script/%s.%s' % (ns.module, m.group(1)), text)
            open(path, 'wb').write(text.encode('utf-8', errors='surrogateescape'))
    print('%d files %s' % (changed, 'rewritten' if ns.apply else 'to rewrite'))
    if review:
        print('left as they are (tests keep old paths on purpose; Docs/Reference is regenerated):')
        for r in review:
            print('  ' + r)


if __name__ == '__main__':
    main()
