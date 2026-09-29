"""Point every "/Script/DreamGUI.<Name>" a split moved at the type's new package, in the plugin's own text.

    python retarget_script_paths.py <Module>            # list what would change
    python retarget_script_paths.py <Module> --apply

Run after generate_split_redirects.py: the moved names are read back from the entries it wrote to
Config/DefaultDreamGUI.ini (OldName /Script/DreamGUI.X, NewName /Script/<Module>.X). Every tracked text file
under Source/, Docs/ (but Docs/Reference, which is regenerated) and the README is rewritten; metadata strings
such as AllowedClasses and MustImplement need this most, because the engine reads them without redirects.

Left alone, and listed for a look: the test module, where an old path is often the point of a test, the
redirect file itself, and the old-asset snapshot, which records paths as they were when it was taken.
"""
import argparse
import os
import re
import subprocess

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
INI = os.path.join(REPO, 'Config', 'DefaultDreamGUI.ini')
TEXT_EXTS = ('.h', '.cpp', '.inl', '.cs', '.md', '.ini', '.dui', '.json', '.py', '.ps1', '.txt')
SKIP = ('Config/DefaultDreamGUI.ini', 'Tools/TestHost/Template/Content/DreamGUIFixtures/Snapshot.txt')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('module')
    ap.add_argument('--apply', action='store_true')
    ns = ap.parse_args()

    ini = open(INI, encoding='utf-8-sig').read()
    names = set(re.findall(r'OldName="/Script/DreamGUI\.([A-Za-z0-9_]+)",NewName="/Script/%s\.\1"' % re.escape(ns.module), ini))
    if not names:
        raise SystemExit('no /Script/DreamGUI -> /Script/%s entries in the redirect file' % ns.module)
    pattern = re.compile(r'/Script/DreamGUI\.(' + '|'.join(sorted(map(re.escape, names), key=len, reverse=True)) + r')(?![A-Za-z0-9_])')
    files = subprocess.run(['git', '-C', REPO, 'ls-files'], capture_output=True, text=True, check=True).stdout.split('\n')
    changed, review = 0, []
    for rel in files:
        if not rel.endswith(TEXT_EXTS) or rel in SKIP:
            continue
        path = os.path.join(REPO, rel)
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
