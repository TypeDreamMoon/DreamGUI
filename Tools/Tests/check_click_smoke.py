"""Say whether a run of the DreamGUI click smoke test clicked its button once, and nothing else.

    python check_click_smoke.py <run dir> [<run dir> ...] [--log <file>]

Each directory is what the probe (Tools/TestHost/Template/Source/DreamGUITestHost/DreamGUIClickSmoke.cpp) wrote for one
run: ClickSmoke.json. A run is a game -- the editor binary with -game on uncooked content, or a packaged build -- that put
a DreamGUI button on its player's screen and handed FSlateApplication a mouse move onto the button's middle, a left button
down and up there, then the same three well away from it. Tools/TestHost/README.md has the commands. A log next to a
directory (<dir>.log, as -abslog=<dir>.log writes it) is searched too, unless --log names one (with a single directory).

What is checked, on each run:
    the probe finished (the player came up, the button was drawn inside the viewport, every step was sent);
    each step reached the game: the viewport's cursor is on the step's pixel (one pixel either way), and the player's
    mouse with it;
    after the move onto the button it is hovered; after the press it is pressed once and not yet clicked; after the
    release on it, released once and clicked once -- in that order, hovered, pressed, released, clicked, as SButton
    announces them (OnMouseButtonUp: Release, then the click);
    the click away pressed nothing and clicked nothing, and the move away un-hovered the button;
    no ensure, no error of DreamGUI and no assertion in the log.

Exit code: 0 every run passed, 1 one did not, 2 a run could not be read.
"""

import argparse
import json
import os
import re
import sys

LOG_FAILURES = [
    (re.compile(r'Ensure condition failed'), 'an ensure'),
    (re.compile(r'Assertion failed'), 'an assertion'),
    (re.compile(r'\bDreamGUI: Error:'), 'an error of DreamGUI'),
    (re.compile(r'\bDreamGUIInput: Error:'), 'an error of DreamGUI\'s input'),
]

STEP_ONTO = 'move onto the button'
STEP_PRESS = 'press on the button'
STEP_RELEASE = 'release on the button'
STEP_AWAY = 'move away'
STEP_PRESS_AWAY = 'press away'
STEP_RELEASE_AWAY = 'release away'


def load_run(directory):
    path = os.path.join(directory, 'ClickSmoke.json')
    with open(path, encoding='utf-8-sig') as f:
        return json.load(f)


def counts_after(run, step_name):
    for step in run.get('steps', []):
        if step.get('name') == step_name:
            return step.get('counts', {}), step
    return None, None


def check_run(label, run, log_path):
    problems = []
    if not run.get('finished'):
        problems.append('the probe did not finish: %s' % (run.get('failure') or 'no reason given'))
    rect = run.get('button', {}).get('rect', {})
    if not rect.get('valid'):
        problems.append('the button was never drawn on the viewport')

    names = [step.get('name') for step in run.get('steps', [])]
    for wanted in (STEP_ONTO, STEP_PRESS, STEP_RELEASE, STEP_AWAY, STEP_PRESS_AWAY, STEP_RELEASE_AWAY):
        if wanted not in names:
            problems.append('no record of the step "%s"' % wanted)

    for step in run.get('steps', []):
        pixel = step.get('pixel', {})
        cursor = step.get('viewportCursor')
        if cursor is None or abs(cursor.get('x', -9) - pixel.get('x', 0)) > 1.0 or abs(cursor.get('y', -9) - pixel.get('y', 0)) > 1.0:
            problems.append('"%s": the viewport has its cursor at %s, the step aimed at %s' % (step.get('name'), cursor, pixel))
        mouse = step.get('playerMouse', {})
        if not step.get('playerMouseValid') or abs(mouse.get('x', -9) - pixel.get('x', 0)) > 1.0 or abs(mouse.get('y', -9) - pixel.get('y', 0)) > 1.0:
            problems.append('"%s": the player sees the mouse at %s (valid %s), the step aimed at %s'
                            % (step.get('name'), mouse, step.get('playerMouseValid'), pixel))

    def expect(step_name, key, wanted):
        counts, _ = counts_after(run, step_name)
        if counts is None:
            return
        if counts.get(key) != wanted:
            problems.append('after "%s" the button had %s %s time(s), not %s' % (step_name, key, counts.get(key), wanted))

    onto, onto_step = counts_after(run, STEP_ONTO)
    if onto is not None:
        if onto.get('hovered', 0) < 1 or not onto_step.get('buttonHovered'):
            problems.append('the move onto the button did not hover it (hovered %s, IsHovered %s)'
                            % (onto.get('hovered'), onto_step.get('buttonHovered')))
        expect(STEP_ONTO, 'pressed', 0)
    expect(STEP_PRESS, 'pressed', 1)
    expect(STEP_PRESS, 'clicked', 0)
    expect(STEP_RELEASE, 'released', 1)
    expect(STEP_RELEASE, 'clicked', 1)
    away, away_step = counts_after(run, STEP_AWAY)
    if away is not None and (away.get('unhovered', 0) < 1 or away_step.get('buttonHovered')):
        problems.append('the move away did not un-hover the button (unhovered %s, IsHovered %s)'
                        % (away.get('unhovered'), away_step.get('buttonHovered')))
    expect(STEP_PRESS_AWAY, 'pressed', 1)
    expect(STEP_RELEASE_AWAY, 'pressed', 1)
    expect(STEP_RELEASE_AWAY, 'released', 1)
    expect(STEP_RELEASE_AWAY, 'clicked', 1)

    # The four that make a click, in the order they came; the hover may come and go around them.
    order = [event for event in run.get('order', []) if event in ('Hovered', 'Pressed', 'Released', 'Clicked')]
    if order[:4] != ['Hovered', 'Pressed', 'Released', 'Clicked']:
        problems.append('the button announced %s, not hovered, pressed, released, clicked' % run.get('order'))

    if log_path and os.path.isfile(log_path):
        with open(log_path, encoding='utf-8-sig', errors='replace') as f:
            for number, line in enumerate(f, 1):
                for pattern, what in LOG_FAILURES:
                    if pattern.search(line):
                        problems.append('%s in the log, line %d: %s' % (what, number, line.strip()[:200]))

    build = run.get('build', {})
    print('%s: %s %s, cooked %s, engine %s; viewport %sx%s; button at %s; events %s'
          % (label, build.get('platform'), build.get('configuration'), build.get('cooked'), build.get('engine'),
             run.get('viewport', {}).get('width'), run.get('viewport', {}).get('height'),
             run.get('button', {}).get('middle'), run.get('order')))
    for problem in problems:
        print('  error: %s' % problem)
    return problems


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('runs', nargs='+', help='the directories the runs wrote')
    parser.add_argument('--log', help='the run\'s log, with a single directory (default: <dir>.log)')
    args = parser.parse_args(argv)
    if args.log and len(args.runs) != 1:
        print('--log names the log of a single run')
        return 2

    failed = 0
    for directory in args.runs:
        try:
            run = load_run(directory)
        except (OSError, ValueError) as e:
            print('cannot read %s: %s' % (directory, e))
            return 2
        log_path = args.log or directory.rstrip('/\\') + '.log'
        if check_run(directory, run, log_path):
            failed += 1
    print('%d of %d run(s) passed' % (len(args.runs) - failed, len(args.runs)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
