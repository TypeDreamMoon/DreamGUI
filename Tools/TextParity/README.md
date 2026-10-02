# Text parity: DreamGUI, Slate and Chrome on one corpus

The corpus `Source/DreamGUITests/Resources/TextParity/corpus.json` is a list of text cases -- kerning, ligatures, a
size ladder, letter spacing, zero-width characters, CJK, bidirectional text, Thai, Devanagari, emoji, combining marks,
long words, punctuation, rich text, layout and corners -- each with its font, size, box width, line height, letter
spacing, language, direction and alignment. Three engines draw every case:

- **DreamGUI**, in `DreamGUI.RHI.TextParity.<case>` (`Source/DreamGUITests/Private/RHI/DreamTextParityAutomationTests.cpp`),
  on the gallery's render-target stage at 1:1, with distance-field fonts made from the case's font files.
- **Slate**, in the same test, through an `FSlateTextLayout` (what `STextBlock` and `SRichTextBlock` lay out with)
  over an `FStandaloneCompositeFont` of the same files, drawn by `FWidgetRenderer`.
- **Chrome**, by `Make-ChromeReference.ps1` in this folder, ahead of the test. Its pictures and numbers are kept in
  `Source/DreamGUITests/Resources/TextParity/Chrome/` and committed with the corpus.

The test writes, to `<project>/Saved/DreamGUITextParity/`:

| File | What it is |
|---|---|
| `<case>_dream.png`, `<case>_slate.png` | each engine's picture, the canvas size of the case |
| `<case>_compare.png` | DreamGUI, Slate and Chrome side by side, grey between them; a missing picture is a light grey panel |
| `<case>_mask.png` | corner cases: black where both DreamGUI and Chrome are solid ink, blue only DreamGUI, red only Chrome |
| `cases/<case>.json` | the case's numbers |
| `report.json`, `report.md` | every case on disk, one row each |
| `coverage.md` | from `DreamGUI.RHI.TextParity.FontCoverage`: which face of its font key has each code point of each case |

The numbers, all in canvas pixels from the top-left, for each engine:

- **line starts**: the UTF-16 offsets of the text at which lines begin. DreamGUI's come from its caret lines, Slate's
  from its line views, Chrome's from the boxes of the characters (a character starts a line when it overlaps the
  line so far by less than 60 % of its own height).
- **carets**: the caret's x at every UTF-16 offset; reported as the largest and the mean distance from Chrome's over
  the offsets both have.
- **first baseline** and **line pitch**, as signed differences from Chrome.
- **ink**: the bounds of the pixels more than 25 % away from the paper, and the sum of the coverage of all of them.
- **mask difference** (cases flagged `corners`): pixels more than 50 % ink in one picture and not in Chrome's.

One thing is asserted: for a case flagged `breaks`, DreamGUI's line starts equal Chrome's. Everything else is reported
only, to be tightened into assertions once the numbers have been looked at. Without a Chrome reference the test says
so and compares nothing; it never fails for the want of one.

## Making the Chrome reference

Requirements: PowerShell 7.2 or later (`pwsh`), Chrome or Edge, the engine's font files where the corpus says they are
(`$(EngineDir)` is `-EngineDir`, `C:\Program Files\Epic Games\UE_5.8\Engine` by default; Hebrew uses
`C:\Windows\Fonts\arial.ttf`, since the engine has no Hebrew font).

```
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Case 'Cjk_*','Corners_*'      # or -Case Cjk_*,Corners_*
pwsh -NoProfile -File Tools\TextParity\Make-ChromeReference.ps1 -Browser "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"
```

For each case (variants included, by the rules the test applies) the script writes an HTML page under
`Saved/TextParity/pages` -- one `@font-face` per font file of the key, as a `file:///` URL; the text at
(padding, padding) in a box of the case's width; `white-space: pre-wrap` (`pre` when the box does not wrap);
`overflow-wrap: anywhere` (or `normal`, as the case asks); the case's line height, letter spacing, `lang`, `dir` and
`text-align`; black on white, or white on black for an `_Inverse` variant. Rich cases are DreamGUI markup turned into
HTML (`<size=N>` a font-size span, `<a=id>` a link, `<b> <i> <u> <s> <sup> <sub>` as they are). An outline is a
`-webkit-text-stroke` twice as wide, painted under the fill, since DreamGUI's outline lies wholly outside the face.

The browser runs headless twice per page, with a profile of its own under `Saved/TextParity/profile`:

```
--headless=new --disable-gpu --disable-lcd-text --force-device-scale-factor=1 --force-color-profile=srgb
--hide-scrollbars --allow-file-access-from-files --virtual-time-budget=5000 --window-size=W,H
--screenshot=<case>.png <page>          (first run)
--dump-dom <page>                       (second run)
```

`--allow-file-access-from-files` is what lets a `file:///` page load `file:///` fonts. The page's own script waits for
`document.fonts.ready` and writes into the document, as JSON: whether every font loaded, the caret x and y at every
UTF-16 offset (collapsed ranges), each character's box, the line starts, the first baseline (a zero-size inline-block
kept on one line with the text's first grapheme, at the start of a hidden copy of the paragraph), the line pitch, and
the primary font's ascent and descent (canvas `measureText`). For a clamped case the lines the clamp hides are left
out: their starts, boxes and carets. The script takes that out of the dumped DOM and saves it as `<case>.json`, with the
browser's version in front (`"chrome"`), which the test copies into its report. A case's old picture and numbers are
deleted before it is drawn, so the two on disk are always from the same run.

It prints one line per case and ends with a count. A case with a problem -- a missing font file, a font that did not
load, no screenshot, a screenshot of another size, no measurements, a browser run killed after `-TimeoutSeconds`
(90 by default) -- is printed in yellow, the run goes on to the next case, and the exit code is 1.

Re-run it whenever the corpus changes, and commit `Chrome/` with the corpus: a reference made from an older corpus
compares the wrong text.

## The corpus

`corpus.json`'s `about` says what every field of a case means and what the defaults are. Two things to know when
adding a case:

- Characters that cannot be seen or that combine with the one before are written as `\u` escapes, everything else
  literally, so that the file can be read.
- `variants: ["narrow"]` adds `<id>_1px`, the case in a box 1 pixel wide with `wrap: normal`, so that every break
  opportunity becomes a line break; give it to every case whose breaks matter, with the `breaks` flag.
  `variants: ["inverse"]` adds `<id>_Inverse`, white on black. Both sides make the variants by the same two rules
  (`DreamTextParityCorpus.cpp`, `Expand-Cases` in the script), and the sizes of their canvases follow from the case.
