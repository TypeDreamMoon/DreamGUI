"""Hold a packaged run of the DreamGUI text smoke test to the reference run, field by field.

    python compare_text_smoke.py <reference dir> <packaged dir> [--ref-log <file>] [--packaged-log <file>]
                                 [--allow-icu-differences] [--tolerance <units>]

Each directory is what the probe (Tools/TestHost/Template/Source/DreamGUITestHost/DreamGUIPackagedSmoke.cpp) wrote for
one run: TextSmoke.json and TextSmoke.png. The reference is the same build run on uncooked content (UnrealEditor.exe
<host> -game ...), the other a cooked, packaged game; Tools/TestHost/README.md has the commands. A log next to a
directory (<dir>.log, as -abslog=<dir>.log writes it) is searched too, unless another file is named.

What is checked:

  On each run by itself
    the probe settled (every glyph landed, the small text sharp) and wrote its picture;
    the text font answers 'A' from its own face, the Han ideograph and the kana from a fallback, the emoji from a
    colour face; the emoji items of the screen are colour glyphs and the picture shows saturated colour where they
    are; the 12 px line drew from coverage glyphs with none pending; every line of the justified paragraph but the
    last reaches both edges of its box (0.5 px); the zh-CN and ja texts draw their ideographs from different faces;
    the safe zone's child is inset by what the platform asks on that viewport (not in Shipping, whose console
    variables are off);
    no font failed to initialise, no ensure, no missing shader map in the log.

  Between the two runs
    every text's display list: lines (where each starts in the source, how far its glyphs reach), items (code
    point, kind, line, source index, face, glyph, colour, pen position, advance, size) -- positions within the
    tolerance; the small text's gate; the fonts' faces, fallbacks and code point answers; the ICU culture names
    zh-CN and ja expand to; the font atlases' shape and glyph counts in the memory report.

  Not compared, by design: the safe zone (an editor build fits it to the viewport, a cooked one answers in pixels of
  the primary display, as SSafeZone does), the timings (reported side by side), and the worlds' memory lines (other
  world names).

Exit code: 0 the runs agree, 1 they do not or a check failed, 2 the input could not be read.
"""

import argparse
import json
import os
import re
import sys

ERROR = 'error'
WARNING = 'warning'
INFO = 'info'

# What a font that did not initialise, an ensure, or a shader the cook left out says in the log.
LOG_FAILURES = [
    (re.compile(r'Font:.*not exist'), 'a font file that does not exist'),
    (re.compile(r'have no face'), 'a font with no face'),
    (re.compile(r'no usable font data'), 'a culture font with no usable data'),
    (re.compile(r"could not read the engine font's file"), 'an engine font cooked with no font data'),
    (re.compile(r'Ensure condition failed'), 'an ensure'),
    (re.compile(r'Missing cached shader map'), 'a shader map the cook did not make'),
    (re.compile(r'\bDreamGUI: Error:'), 'an error of DreamGUI'),
]

LATIN_A = 0x41
HAN = 0x6F22
KANA = 0x3042
EMOJI = 0x1F600


class Findings(object):
    def __init__(self):
        self.items = []

    def add(self, level, where, message):
        self.items.append((level, where, message))

    def count(self, level):
        return sum(1 for item in self.items if item[0] == level)


def load_run(directory):
    path = os.path.join(directory, 'TextSmoke.json')
    with open(path, encoding='utf-8-sig') as f:
        return json.load(f)


def by_name(entries, key):
    return dict((entry.get(key), entry) for entry in entries or [])


def near(a, b, tolerance):
    return abs(float(a) - float(b)) <= tolerance


def text_named(run, name):
    for text in run.get('texts', []):
        if text.get('name') == name:
            return text
    return None


def items_with(text, codepoint):
    return [item for item in (text or {}).get('items', []) if item.get('codepoint') == codepoint]


def check_run(findings, label, run, log_path):
    where = label
    build = run.get('build', {})
    shipping = build.get('configuration') == 'Shipping'
    if not run.get('settled'):
        findings.add(ERROR, where, 'the probe did not settle: %s' % run.get('failure', '?'))
    for text in run.get('texts', []):
        if text.get('hasPendingGlyphs'):
            findings.add(ERROR, where, '%s still had glyphs on the worker when it was written' % text.get('name'))

    fonts = run.get('fonts', [])
    text_font = None
    for font in fonts:
        if font.get('path', '').endswith('Font_Text'):
            text_font = font
    if text_font is None:
        findings.add(ERROR, where, 'the screen does not use /Game/DreamGUISmoke/Font_Text')
    else:
        answers = dict((probe.get('codepoint'), probe) for probe in text_font.get('codepoints', []))
        if text_font.get('faceCount', 0) < 4:
            findings.add(ERROR, where, 'Font_Text has %d face(s), not its own and three fallbacks' % text_font.get('faceCount', 0))
        a = answers.get(LATIN_A, {})
        if a.get('face') != 0:
            findings.add(ERROR, where, "'A' does not resolve to Font_Text's own face (face %s)" % a.get('face'))
        for codepoint, what in ((HAN, 'the Han ideograph'), (KANA, 'the kana')):
            answer = answers.get(codepoint, {})
            if not answer.get('resolved') or answer.get('face', 0) < 1:
                findings.add(ERROR, where, '%s does not resolve to a fallback face (face %s)' % (what, answer.get('face')))
        emoji = answers.get(EMOJI, {})
        if not emoji.get('colorFace'):
            findings.add(ERROR, where, 'the emoji does not resolve to a colour face (face %s)' % emoji.get('face'))

    emoji_text = text_named(run, 'Emoji')
    emoji_items = [item for item in (emoji_text or {}).get('items', []) if item.get('kind') == 'Glyph' and item.get('emit')]
    if not emoji_items:
        findings.add(ERROR, where, 'the Emoji text drew no glyph')
    elif not all(item.get('color') for item in emoji_items):
        findings.add(ERROR, where, 'some emoji are not colour glyphs: %s' % ', '.join(
            'U+%04X' % item.get('codepoint', 0) for item in emoji_items if not item.get('color')))
    mixed = text_named(run, 'Mixed')
    for codepoint, what in ((HAN, 'the Han ideograph'), (KANA, 'the kana')):
        items = items_with(mixed, codepoint)
        if not items or items[0].get('face', 0) < 1:
            findings.add(ERROR, where, 'Mixed draws %s from face %s, not a fallback' % (what, items[0].get('face') if items else '-'))
    if not any(item.get('color') for item in items_with(mixed, EMOJI)):
        findings.add(ERROR, where, 'Mixed does not draw its emoji in colour')

    chinese = items_with(text_named(run, 'Chinese'), HAN)
    japanese = items_with(text_named(run, 'Japanese'), HAN)
    if chinese and japanese and chinese[0].get('face') == japanese[0].get('face'):
        names = run.get('culture', {}).get('prioritizedZhCN', [])
        level = WARNING if 'zh-Hans' not in names else ERROR
        findings.add(level, where, 'the zh-CN and ja texts draw the ideograph from one face (%s); zh-CN expands to %s'
                     % (chinese[0].get('face'), ', '.join(names) or 'nothing'))

    small = (text_named(run, 'Small12') or {}).get('smallText', {})
    if small.get('gate') != 'Coverage' or small.get('coverageItems', 0) <= 0:
        findings.add(ERROR, where, 'the 12 px line did not draw from coverage glyphs (gate %s, %s item(s))'
                     % (small.get('gate'), small.get('coverageItems')))
    if small.get('pendingItems', 0) != 0:
        findings.add(ERROR, where, 'the 12 px line still waits for %s coverage glyph(s)' % small.get('pendingItems'))

    justified = text_named(run, 'Justified')
    if justified is not None:
        box = justified.get('contentBox', {})
        lines = [line for line in justified.get('lines', []) if line.get('hasGlyphs')]
        if len(lines) < 2:
            findings.add(ERROR, where, 'the justified paragraph has %d line(s); it has to wrap to show justification' % len(lines))
        for line in lines[:-1]:
            if not near(line.get('inkLeft'), box.get('left'), 0.5) or not near(line.get('inkRight'), box.get('right'), 0.5):
                findings.add(ERROR, where, 'justified line %d spans %.2f..%.2f, not the box %.2f..%.2f'
                             % (line.get('index'), line.get('inkLeft'), line.get('inkRight'), box.get('left'), box.get('right')))
    else:
        findings.add(ERROR, where, 'there is no Justified text')

    picture = run.get('picture', {})
    if not picture.get('saved'):
        findings.add(ERROR, where, 'the picture was not read back and saved')
    colour = picture.get('emoji', {})
    if picture.get('read') and (colour.get('glyphs', 0) < 1 or colour.get('saturatedFraction', 0.0) < 0.25):
        findings.add(ERROR, where, 'the emoji show little colour in the picture: %.0f%% of %d pixel(s) saturated'
                     % (100.0 * colour.get('saturatedFraction', 0.0), colour.get('pixels', 0)))

    safe = run.get('safeZone', {})
    ratio = safe.get('titleRatio', -1.0)
    if not safe.get('found'):
        findings.add(ERROR, where, 'the safe zone frame or its child is missing')
    elif shipping:
        findings.add(INFO, where, 'safe zone not checked: Shipping keeps r.DebugSafeZone.TitleRatio off')
    elif 0.0 < ratio < 1.0:
        inset = safe.get('inset', {})
        platform = safe.get('platformInset', {})
        for side in ('left', 'top', 'right', 'bottom'):
            if inset.get(side, 0.0) <= 0.5:
                findings.add(ERROR, where, 'the safe zone does not inset its %s side under TitleRatio %.2f' % (side, ratio))
            elif not near(inset.get(side, 0.0), platform.get(side, 0.0), 1.5):
                findings.add(ERROR, where, 'the safe zone insets its %s side by %.1f px; the platform asks %.1f'
                             % (side, inset.get(side, 0.0), platform.get(side, 0.0)))
    else:
        findings.add(INFO, where, 'safe zone not checked: run with -dpcvars=r.DebugSafeZone.TitleRatio=0.9')

    if 'memory' not in run:
        findings.add(WARNING, where, 'no memory report: %s' % run.get('memoryError', '?'))

    if log_path and os.path.isfile(log_path):
        with open(log_path, encoding='utf-8', errors='replace') as f:
            for number, line in enumerate(f, 1):
                for pattern, what in LOG_FAILURES:
                    if pattern.search(line):
                        findings.add(ERROR, '%s log line %d' % (label, number), '%s: %s' % (what, line.strip()[:200]))
    elif not shipping:
        findings.add(INFO, where, 'no log searched%s' % (' (%s is not there)' % log_path if log_path else ''))


def compare_value(findings, where, name, a, b, tolerance=None):
    if tolerance is not None and isinstance(a, (int, float)) and isinstance(b, (int, float)):
        if not near(a, b, tolerance):
            findings.add(ERROR, where, '%s %s vs %s' % (name, a, b))
    elif a != b:
        findings.add(ERROR, where, '%s %r vs %r' % (name, a, b))


def compare_texts(findings, ref, other, tolerance):
    ref_texts = by_name(ref.get('texts'), 'name')
    other_texts = by_name(other.get('texts'), 'name')
    for name in sorted(set(ref_texts) | set(other_texts)):
        where = 'text %s' % name
        a = ref_texts.get(name)
        b = other_texts.get(name)
        if a is None or b is None:
            findings.add(ERROR, where, 'only in the %s run' % ('packaged' if a is None else 'reference'))
            continue
        for key in ('text', 'font', 'fontSize', 'renderedFontSize', 'language', 'truncated', 'visibleChars', 'itemCount'):
            compare_value(findings, where, key, a.get(key), b.get(key), tolerance if key in ('fontSize', 'renderedFontSize') else None)
        compare_value(findings, where, 'small text gate', a.get('smallText', {}).get('gate'), b.get('smallText', {}).get('gate'))
        if a.get('smallText', {}).get('coverageItems') != b.get('smallText', {}).get('coverageItems'):
            findings.add(WARNING, where, 'coverage items %s vs %s' % (a.get('smallText', {}).get('coverageItems'), b.get('smallText', {}).get('coverageItems')))
        for key in ('preferredWidth', 'preferredHeight'):
            compare_value(findings, where, key, a.get(key), b.get(key), tolerance)
        for box in ('contentBox', 'textBlockBox'):
            for side in ('left', 'right', 'bottom', 'top'):
                compare_value(findings, where, '%s.%s' % (box, side), a.get(box, {}).get(side), b.get(box, {}).get(side), tolerance)
        a_lines = a.get('lines', [])
        b_lines = b.get('lines', [])
        if len(a_lines) != len(b_lines):
            findings.add(ERROR, where, '%d line(s) vs %d: the line breaks differ' % (len(a_lines), len(b_lines)))
        for line_a, line_b in zip(a_lines, b_lines):
            line_where = '%s line %d' % (where, line_a.get('index'))
            compare_value(findings, line_where, 'starts at source index', line_a.get('firstSource'), line_b.get('firstSource'))
            compare_value(findings, line_where, 'has glyphs', line_a.get('hasGlyphs'), line_b.get('hasGlyphs'))
            for key in ('inkLeft', 'inkRight'):
                compare_value(findings, line_where, key, line_a.get(key), line_b.get(key), tolerance)
        a_items = a.get('items', [])
        b_items = b.get('items', [])
        for index, (item_a, item_b) in enumerate(zip(a_items, b_items)):
            item_where = '%s item %d (U+%04X)' % (where, index, item_a.get('codepoint', 0))
            for key in ('codepoint', 'kind', 'line', 'source', 'face', 'glyph', 'color', 'emit', 'pending'):
                compare_value(findings, item_where, key, item_a.get(key), item_b.get(key))
            for key in ('penX', 'penY', 'advance', 'glyphSize', 'width', 'height'):
                compare_value(findings, item_where, key, item_a.get(key), item_b.get(key), tolerance)


def compare_fonts(findings, ref, other):
    ref_fonts = by_name(ref.get('fonts'), 'path')
    other_fonts = by_name(other.get('fonts'), 'path')
    for path in sorted(set(ref_fonts) | set(other_fonts)):
        where = 'font %s' % path
        a = ref_fonts.get(path)
        b = other_fonts.get(path)
        if a is None or b is None:
            findings.add(ERROR, where, 'only in the %s run' % ('packaged' if a is None else 'reference'))
            continue
        compare_value(findings, where, 'face count', a.get('faceCount'), b.get('faceCount'))
        compare_value(findings, where, 'colour faces', [face.get('color') for face in a.get('faces', [])], [face.get('color') for face in b.get('faces', [])])
        compare_value(findings, where, 'fallbacks', a.get('fallbacks'), b.get('fallbacks'))
        a_answers = by_name(a.get('codepoints'), 'codepoint')
        b_answers = by_name(b.get('codepoints'), 'codepoint')
        for codepoint in sorted(set(a_answers) | set(b_answers)):
            answer_a = a_answers.get(codepoint, {})
            answer_b = b_answers.get(codepoint, {})
            for key in ('resolved', 'face', 'glyph', 'colorFace', 'facesHaving'):
                compare_value(findings, '%s U+%04X' % (where, codepoint), key, answer_a.get(key), answer_b.get(key))


def compare_memory(findings, ref, other):
    a_fonts = by_name(ref.get('memory', {}).get('fonts'), 'path')
    b_fonts = by_name(other.get('memory', {}).get('fonts'), 'path')
    for path in sorted(set(a_fonts) & set(b_fonts)):
        where = 'memory, font %s' % path
        a = a_fonts[path]
        b = b_fonts[path]
        for key in ('atlasSliceSize', 'atlasBytesPerTexel'):
            compare_value(findings, where, key, a.get(key), b.get(key))
        for key in ('atlasSlices', 'fieldGlyphs', 'colorGlyphs', 'coverageGlyphs'):
            if a.get(key) != b.get(key):
                findings.add(WARNING, where, '%s %s vs %s' % (key, a.get(key), b.get(key)))


def main(argv=None):
    # The findings quote the texts (CJK, emoji); a console code page that cannot show them must not end the report with
    # a traceback, as sourcescan.py's console_safe says.
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors='replace')
        except (AttributeError, ValueError):
            pass
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('reference', help='the directory the reference run (-game on uncooked content) wrote')
    parser.add_argument('packaged', help='the directory the packaged run wrote')
    parser.add_argument('--ref-log', help='the reference run\'s log (default: <reference>.log)')
    parser.add_argument('--packaged-log', help='the packaged run\'s log (default: <packaged>.log)')
    parser.add_argument('--allow-icu-differences', action='store_true',
                        help='report a difference in what zh-CN and ja expand to as a warning: a run packaged with another ICU preset')
    parser.add_argument('--tolerance', type=float, default=0.01, help='how far positions and sizes may differ, in units (default 0.01)')
    args = parser.parse_args(argv)

    try:
        ref = load_run(args.reference)
        other = load_run(args.packaged)
    except (OSError, ValueError) as e:
        print('cannot read the runs: %s' % e)
        return 2

    findings = Findings()
    check_run(findings, 'reference', ref, args.ref_log or args.reference.rstrip('/\\') + '.log')
    check_run(findings, 'packaged', other, args.packaged_log or args.packaged.rstrip('/\\') + '.log')
    compare_texts(findings, ref, other, args.tolerance)
    compare_fonts(findings, ref, other)
    compare_memory(findings, ref, other)
    for key in ('prioritizedZhCN', 'prioritizedJa'):
        a = ref.get('culture', {}).get(key)
        b = other.get('culture', {}).get(key)
        if a != b:
            findings.add(WARNING if args.allow_icu_differences else ERROR, 'culture',
                         '%s expands to %s in the reference and %s packaged' % (key[len('prioritized'):], a, b))
    compare_value(findings, 'picture', 'size', [ref.get('picture', {}).get('width'), ref.get('picture', {}).get('height')],
                  [other.get('picture', {}).get('width'), other.get('picture', {}).get('height')])

    for label, run in (('reference', ref), ('packaged', other)):
        build = run.get('build', {})
        timing = run.get('timing', {})
        print('%-9s %s %s, cooked %s, engine %s; settled in %.2f s; first frame with the text %.1f ms (frame %s after placing), slowest %.1f ms'
              % (label, build.get('platform'), build.get('configuration'), build.get('cooked'), build.get('engine'),
                 timing.get('secondsToSettle', -1.0), timing.get('firstTextFrameMs', -1.0), timing.get('firstTextFrame'),
                 timing.get('slowestFrameMs', -1.0)))
    for level, where, message in findings.items:
        print('%-7s %s: %s' % (level, where, message))
    errors = findings.count(ERROR)
    print('%d error(s), %d warning(s)' % (errors, findings.count(WARNING)))
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
