# Text parity: DreamGUI, Slate and Chrome on one corpus

The corpus `Source/DreamGUITests/Resources/TextParity/corpus.json` is a list of text cases -- kerning, ligatures, a
size ladder and small text at device scales, letter spacing, zero-width characters, CJK and the fallback faces a
language picks, bidirectional text, Thai, Devanagari, colour emoji, combining marks, long words, punctuation, rich text,
justification, tab stops, layout, corners and gradient fills -- each with its font, size, box width, line height,
letter spacing, language, direction and alignment. Three engines draw every case:

- **DreamGUI**, in `DreamGUI.RHI.TextParity.<case>` (`Source/DreamGUITests/Private/RHI/DreamTextParityAutomationTests.cpp`),
  on the gallery's render-target stage, with distance-field fonts made from the case's font files: the first file the
  font, the rest its fallback entries (`FDreamUIFontFallback`, with the cultures, ranges and scale the fonts table
  gives them). The text's `Language` is the case's `lang`.
- **Slate**, in the same test, through an `FSlateTextLayout` (what `STextBlock` and `SRichTextBlock` lay out with)
  over an `FStandaloneCompositeFont` of the same files, drawn by `FWidgetRenderer`.
- **Chrome**, by `Make-ChromeReference.ps1` in this folder, ahead of the test. Its pictures and numbers are kept in
  `Source/DreamGUITests/Resources/TextParity/Chrome/` and committed with the corpus.

The test writes, to `<project>/Saved/DreamGUITextParity/`:

| File | What it is |
|---|---|
| `<case>_dream.png`, `<case>_slate.png` | each engine's picture, the case's canvas times its scale |
| `<case>_compare.png` | DreamGUI, Slate and Chrome side by side, grey between them; a missing picture is a light grey panel, an engine with no equivalent of the case (n/a) a hatched one |
| `<case>_mask.png` | corner cases: black where both DreamGUI and the reference are solid ink, blue only DreamGUI, red only the reference |
| `<case>_dream_mask.png` | fill cases: DreamGUI's solid mask of the text, the counterpart of Chrome's `<case>.mask.png` |
| `cases/<case>.json` | the case's numbers |
| `report.json`, `report.md` | every case on disk, one row each |
| `coverage.md` | from `DreamGUI.RHI.TextParity.FontCoverage`: which face of its font key draws each code point of each case, in the order the case's language tries them |

The numbers, positions in canvas (CSS) pixels from the top-left, pictures in device pixels, for each engine:

- **line starts**: the UTF-16 offsets of the text at which lines begin. DreamGUI's come from its caret lines, Slate's
  from its line views, Chrome's from the boxes of the characters (a character starts a line when it overlaps the
  line so far by less than 60 % of its own height).
- **carets**: the caret's x at every UTF-16 offset; reported as the largest and the mean distance from the reference's
  over the offsets both have.
- **first baseline** and **line pitch**, as signed differences from the reference.
- **ink**: the bounds of the pixels more than 25 % away from the paper, and the sum of the coverage of all of them
  in linear light (*linear ink*: each pixel and the paper decoded from sRGB before they are differenced).
- **crispness**, which small text is judged by (`MeasureCrispness`):
  - *edge width per stroke*: reading each row, and separately each column, every run of pixels above the paper
    (coverage above 0.1) whose peak p is at least 0.5 is a stroke; its edges are its pixels between 0.1 p and 0.9 p
    from where it starts to where it first reaches 0.9 p, and from where it last does to where it ends. The mean width
    of an edge, two per stroke, rows and columns apart. A hinted, pixel-aligned stem has edges of a pixel or less, a
    soft one several; a one-pixel line that peaks at 0.83 counts as well as a stem that reaches full ink;
  - *grey fraction*: of the pixels with any ink (coverage above 0.15), the share that is neither paper nor ink
    (below 0.85). DreamGUI's is held to **Slate's**, not the reference's, for dark text on light paper: both are plain
    grayscale coverage, while Chrome's DirectWrite masks are filtered across (the reference is made with
    `--disable-lcd-text`, and at no size does Chrome have an x-edge without grey pixels);
  - *gradient per ink*: the sum of |grad c| over the picture over the ink mass, higher for sharper edges.
- **mask difference** (cases flagged `corners`): pixels more than 50 % ink in one picture and not in the reference's.
- **gradient fills** (fill cases, below): the painted colours against Chrome's on the pixels both masks cover fully.

The measures before this round are reported alongside the new ones for one round, marked *old* in `report.md`: ink
summed in sRGB-encoded coverage, which the encoding inflates when coverage spreads over more pixels (light-on-dark text
read 5-9 % short where its linear ink is within 3 %); edge width per transition, the pixels strictly between paper (at
most 0.1) and ink (at least 0.9) wherever a row or a column goes from one to the other, which misses every stroke that
never reaches 0.9 -- DreamGUI's one-pixel strokes peak at 0.80-0.85 -- and is left with curve crossings; and the grey
fraction against the reference.

The **reference** is Chrome, or Slate for a case whose `reference` is `slate`: Chrome has no middle ellipsis, so
`Ellipsis_Middle` is measured against Slate and has no Chrome reference.

Asserted, unless the case is flagged `reportOnly`:

- for a case flagged `breaks`, DreamGUI's line starts equal the reference's;
- every target the case names (`targets`, or a set of `targetSets` by name) holds: `strokeEdge` (edge width per
  stroke, pixels wider than the reference's, rows and columns), `inkLinear` (linear ink, relative, either way),
  `greyVsSlate` (the grey fraction against Slate's, relative, either way; measured on dark text on light paper only),
  `inkMass` (sRGB-encoded ink, relative, either way), `inkBounds` (pixels, each edge of the ink's bounds), `caretMax`
  (pixels), `baseline` and `pitch` (pixels, either way), `edgeWidth` (edge width per transition, pixels wider than the
  reference's, rows and columns), `greyFraction` (against the reference, relative, either way).

`reportOnly` means known not to match yet: its breaks and targets are reported, nothing is asserted. Small text is
asserted: from 10 to 16 px in both colour schemes (`Size_10` to `Size_16` and their `_Inverse`) and the scaled cases
(`Size_12_Scale125`, `Size_14_Scale133`) hold `smallText` -- stroke edges at most 0.2 px wider than Chrome's on both
axes, linear ink within 8 % of Chrome's, grey fraction within 10 % of Slate's (dark on light only) -- and CJK at 12 and
14 px holds `smallTextCjk`, the same edges and linear ink within 10 %, its grey reported only. `Size_14` states the edges
and the ink alone: its grey was 8 % more than Slate's on the pictures from before the multisampled target took the sRGB
flag, and that fix moves its darkest edge pixels across the 0.85 line, which puts it about on the 10 % limit; it is
reported until a run after the fix has been looked at. The `i` run
(`Phases_iRun`), every `_Field` variant (always report-only: the distance field is the other way DreamGUI draws small
text, kept to compare the two), colour emoji (ink within 10 % of Chrome's, its bounds within 2 px, carets within 1 px),
Segoe UI Emoji (carets, baseline and pitch within 1 px) and the gradient fills are reported only. Everything else is
reported, to be tightened into assertions once the numbers have been looked at. Without a Chrome reference the test
says so and compares nothing; it never fails for the want of one.

Slate has no equivalent of some cases, and does not draw them: its panel is hatched, its columns say n/a. Those are
justified cases (Slate cannot justify), cases that state a `tabSize` (Slate's tab is a width of its own), cases whose
fallback faces are chosen by the text's language (a font key with `lang` faces, or `<lang=xx>` in rich text; Slate
picks a composite font's sub-fonts by the game's culture), and gradient fills (Slate cannot fill text with a gradient).

## Gradient fills

A case with a `fill` -- a CSS gradient, which DreamGUI reads with `FDreamGradient::ParseCss` -- has its face filled
with it: DreamGUI's text paints it as its `FacePaint`, measured across the text as a block (the paint boxes' default),
and Chrome's page paints the paragraph with `background-image: <fill>; background-clip: text; color: transparent`, the
paragraph's box being exactly the text block. A rich case can paint a run instead, `<gradient=css-written-without-spaces>`
(DreamGUI reads the tag's name as CSS; the page makes it a span painted the same way), measured across the run on both
sides. An outline or a shadow goes, on Chrome's page, on a copy of the paragraph underneath in the outline's colour:
the paint shows through the text's own shapes under its foreground, where a stroke's inner half would cover the
painted face's edge, and DreamGUI's outline lies wholly outside the face.

Each fill case is also drawn as a solid mask, on both sides: the same text in the ink colour, nothing painted,
outlined or shadowed (DreamGUI turns the paint off and takes the `<gradient>` tags out; Chrome's page is drawn a second
time, `<case>.mask.png`). On the pixels both masks cover fully (coverage at least 0.99), the test compares the two
painted pictures and reports, in 8-bit codes:

- the mean and the 95th percentile of each pixel's largest channel difference;
- the largest channel difference between the two average colours of a band, the pixels cut into 32 bands of equal
  width along the gradient's axis (a linear fill's angle; horizontal for the other kinds and for `<gradient>` runs).
  Noise along the glyphs' edges averages out in a band; a gradient placed, turned or mixed differently does not.

They are reported only, in `report.md`'s "Gradient fills" table and each case's `fill` object, to be asserted once the
numbers have been looked at. The cases: 90 and 135 degrees on a wrapped paragraph, gold top to bottom, hard stops,
repeating, radial ellipse and circle, conic, `in oklab` and `in srgb-linear`, transparent stops, small text at 12 and 14
px (with `_Field` variants), a rich-text run, an outline with a fill, and the bitmap font. Write angles rather than
`to <corner>` (CSS turns a corner to the actual box's corner, `ParseCss` to the square's: a fixed angle cannot depend
on the box) and hex colours rather than names (CSS's `green` is #008000, a rich text's `<color=green>` #00FF00).

## Making the Chrome reference

Requirements: PowerShell 7.2 or later (`pwsh`), Chrome or Edge, the engine's font files where the corpus says they are
(`$(EngineDir)` is `-EngineDir`, `C:\Program Files\Epic Games\UE_5.8\Engine` by default; Hebrew uses
`C:\Windows\Fonts\arial.ttf`, since the engine has no Hebrew font; `EmojiSegoe` uses `C:\Windows\Fonts\seguiemj.ttf`).

```
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Case 'Cjk_*','Corners_*'      # or -Case Cjk_*,Corners_*
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Case 'Fill_*'                  # the gradient fills and their masks
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Browser "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
```

For each case (variants included, by the rules the test applies) the script writes an HTML page under
`Saved/TextParity/pages`:

- one `@font-face` per font file of the key, as a `file:///` URL; a fallback face's `unicodeRange` and `scale` become
  its `unicode-range` and `size-adjust`;
- the family list in the order DreamGUI's face resolver tries the faces (the primary, the fallbacks meant for the
  text's language, those meant for any, the rest), and for every language a face is meant for a `:lang()` rule with that
  language's order, which applies to the paragraph and to any `<lang=xx>` span in it;
- the text at (padding, padding) in a box of the case's width; `white-space: pre-wrap` (`pre` when the box does not
  wrap); `overflow-wrap: anywhere` (or `normal`, as the case asks); the case's line height, letter spacing, `tab-size`,
  `lang`, `dir`, `text-align` (with `text-justify` and `text-align-last` for a justified case) and `text-shadow`; black
  on white, or white on black for an `_Inverse` variant.

Rich cases are DreamGUI markup turned into HTML (`<size=N>` a font-size span, `<a=id>` a link, `<lang=xx>` a span with
that `lang`, `<gradient=css>` a span painted with the gradient, `<b> <i> <u> <s> <sup> <sub>` as they are). An outline
is a `-webkit-text-stroke` twice as wide, painted under the fill, since DreamGUI's outline lies wholly outside the face.
A fill case's page paints the paragraph with its fill through `background-clip: text`, puts its outline and shadow on a
copy of the paragraph underneath, and is written a second time as its mask (`<case>.mask.html`: the text solid in the
ink colour), whose screenshot is `<case>.mask.png` -- see "Gradient fills" above.

The browser runs headless twice per page, with a profile of its own under `Saved/TextParity/profile`:

```
--headless=new --disable-gpu --disable-lcd-text --force-device-scale-factor=<scale> --force-color-profile=srgb
--hide-scrollbars --allow-file-access-from-files --virtual-time-budget=5000 --window-size=W,H
--screenshot=<case>.png <page>          (first run)
--screenshot=<case>.mask.png <mask>     (a fill case's mask)
--dump-dom <page>                       (second run)
```

The window is the case's canvas in CSS pixels; at the case's `scale` the screenshot is the canvas times the scale in
device pixels, which the script checks. `--allow-file-access-from-files` is what lets a `file:///` page load
`file:///` fonts. The page's own script waits for `document.fonts.ready` and writes into the document, as JSON: whether
every font loaded, the caret x and y at every UTF-16 offset (collapsed ranges), each character's box, the line starts,
the first baseline (a zero-size inline-block kept on one line with the text's first grapheme, at the start of a hidden
copy of the paragraph), the line pitch, and the primary font's ascent and descent (canvas `measureText`), all in CSS
pixels. For a clamped case the lines the clamp hides are left out: their starts, boxes and carets. The script takes that
out of the dumped DOM and saves it as `<case>.json`, with the browser's version in front (`"chrome"`), which the test
copies into its report. A case's old picture, mask and numbers are deleted before it is drawn, so the files on disk are
always from the same run.

It prints one line per case and ends with a count. A case held to Slate is skipped, and so is a case whose font key is
`optional` and names a file this machine does not have (the test skips it too). A case with a problem -- a missing font
file, a font that did not load, no screenshot or mask screenshot, one of another size, no measurements, a browser run
killed after `-TimeoutSeconds` (90 by default) -- is printed in yellow, the run goes on to the next case, and the exit
code is 1.

Re-run it whenever the corpus changes, and commit `Chrome/` with the corpus: a reference made from an older corpus
compares the wrong text. When the page itself changes -- as it did when fallback faces, `:lang()` lists, `tab-size`,
justification, shadows and the device scale were added -- re-run it for every case.

## The corpus

`corpus.json`'s `about` says what every field of a case means and what the defaults are. Things to know when adding a
case:

- Characters that cannot be seen or that combine with the one before are written as `\u` escapes, everything else
  literally, so that the file can be read.
- `variants: ["narrow"]` adds `<id>_1px`, the case in a box 1 pixel wide with `wrap: normal`, so that every break
  opportunity becomes a line break; give it to every case whose breaks matter, with the `breaks` flag.
  `variants: ["inverse"]` adds `<id>_Inverse`, white on black. `variants: ["field"]` adds `<id>_Field`, the same
  picture asked of DreamGUI's distance field (`smallTextRaster: "off"`), to see the two ways DreamGUI draws small text
  against one reference; it is always report-only. Both sides make the variants by the same rules
  (`DreamTextParityCorpus.cpp`, `Expand-Cases` in the script), and the sizes of their canvases follow from the case.
- `fill` is a CSS gradient over the whole text, and `<gradient=css-written-without-spaces>` one over a rich text's run
  (see "Gradient fills"). Re-run the script for a fill case whenever its text or its fill changes: its mask comes from
  the same run.
- `scale` draws the case at a device scale: DreamGUI's stage is the canvas times the scale in pixels with its root laid
  out at the canvas's size (the canvas scale), Slate draws at that DPI scale, Chrome with that device scale factor.
  Pick a canvas the scale takes to whole pixels (`[480, 63]` at 4/3).
- A fallback face can be `{"file", "lang", "unicodeRange", "scale"}`; the `CJKLocale` key (Roboto, GenEi Gothic for
  `ja`, Droid Sans Fallback for `zh-Hans`) is what `Han_Ja`, `Han_Zh` and `Han_LangTags` pick faces from by language,
  `CJKScaled` a Droid limited to CJK ranges at 120 %.

### Colour emoji

All three engines draw the engine's own `Editor/Slate/Fonts/NotoColorEmoji.ttf` (CBDT bitmaps, Emoji 11): Chrome
through `@font-face`, Slate as a sub-font, DreamGUI as a fallback whose glyphs are colour bitmaps in the font's atlas.
The same bitmaps on all three sides make ink comparable. `Emoji_Segoe` uses Windows' `seguiemj.ttf` and is held to
carets and metrics only: Chrome paints its COLRv1 gradients, FreeType (DreamGUI and Slate) its COLRv0 layers; it is
skipped where the font is not installed.

There is no COLRv0-only test font in the corpus. The plan was a small one made with fontTools (two or three glyphs and
one GSUB ligature), committed under `Resources/TextParity`; fontTools is not installed on the machine this round was
written on and nothing could be downloaded, so it was not made. COLRv0 is covered by the rasterizer's headless tests
(Segoe's v0 layers) and by `Emoji_Segoe`'s carets; a font made with fontTools later would go in as a key of its own
and a case like `Emoji`.
