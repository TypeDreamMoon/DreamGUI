# Scenes: pictures written as `.dui`

Every `.dui` file in this folder is a test of its own, `DreamGUI.RHI.Scene.<file name>`
(`Private/RHI/DreamSceneAutomationTests.cpp`). The test reads the file the way the compiler reads a widget
Blueprint's source -- `FDreamUISourceFile::Parse`, then `FDreamUITextBuilder::Build` -- copies the tree onto the
gallery's render-target stage (`Private/RHI/DreamGalleryStage.h`), and holds the picture to
`Resources/Golden/Scene_<file name>.png`. A file can ask for more pictures of itself (canvas scales, a variant); each
is a test and a golden of its own, named below.

The picture is taken once the scene has settled: something is drawn, every font the scene's texts use has no glyph
left on its worker (`UDreamUIFontData_FreeTypeRender::GetPendingAsyncGlyphCount`), and the picture has then been the
same three frames running. It is compared with the gallery's tolerance: at most 0.2 % of the pixels may differ by
more than 8 on a channel.

## Running

```
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -Filter "StartsWith:DreamGUI.RHI.Scene"
```

Every picture is written to `<project>/Saved/DreamGUITests/Captures/Scene_<name>.png`, and where it does not match,
`Scene_<name>.diff.png` marks the pixels that differ. A scene with **no golden image fails**, naming the file to write;
look at the capture, and once it is right, run again with `-DreamGUIWriteGoldens` on the editor's command line, which
writes every picture over its golden instead of comparing.

### Pending goldens

`Resources/Golden/pending.json` names the goldens that are due to be written for the first time or written again and
have not been looked at yet: a new scene, or a picture a change made on purpose redraws. Such a picture is still drawn,
saved and compared, but a missing golden or a difference is a warning that gives the reason, never a failure. To clear
an entry, write the golden with `-DreamGUIWriteGoldens`, look at it, and take the entry out; from then on the picture is
held to it again. The same list covers the gallery's goldens (`Gallery_Text`).

Round 4 put every scene there: small text under 20 px now draws from coverage glyphs by default
(`UDreamGUISettings::bSmallTextCoverage`), and every scene has headings and labels at 12 and 15 px. The three scenes
about small text -- `Text_SizeLadder`, `Extreme_TinyText` and `Extreme_MixedSizesLine` -- also run as
`<scene>_SmallTextCoverageOff`, with the switch off, held strictly to their goldens from before the switch
(`Scene_<scene>_SmallTextCoverageOff.png`, copies of the old ones): with the switch off, nothing may have moved.

## What a scene file may say beyond the language

Comment lines that start with `// @`, anywhere in the file. A directive the test does not know, or one it cannot read,
fails the scene.

- **The stage's size.** `// @stage <width>x<height>`, in units (pixels at canvas scale 1), up to 4096 on a side.
  Without one the stage is 512 x 512.
- **Canvas scales.** `// @canvasScale 1 1.25 1.333` draws the scene once per scale: the first under the scene's own
  name, each further one as `<scene>_Scale<percent>` (`Text_SmallCoverage_Scale125`, `_Scale133`). The picture is the
  stage times the scale in pixels and the root is laid out at the stage's size, as a DPI scale or the canvas scaler
  would have it, so everything keeps its units and is drawn at that scale.
- **The small-text switch off.** `// @variant SmallTextCoverageOff` draws the scene once more as
  `<scene>_SmallTextCoverageOff` with the project's small-text coverage switch off (at the first canvas scale): every
  size from the distance field, as before the switch.
- **Fill progress.** `// @fill <NodeId> <progress>` sets that text's lyric-style fill (0..1) once the scene is on the
  stage. Fill progress is runtime state the language cannot hold.
- **An alpha TextAnimation.** `// @textAnimationAlpha <NodeId> <alpha> <offset>` puts a TextAnimation on that text: a
  range selector at the offset and an alpha property, so the characters up to about the offset fade to the alpha, over
  a short ramp, and the rest stay as they are. A `.dui` cannot build one: the selector and properties are instanced
  objects. It changes vertex colours only, which keeps small text on coverage glyphs.
- **The root fills the stage.** Whatever the root node says about its own size, the test anchors it to all four
  corners of the stage, the way adding a page to the viewport does. Lay the scene out under it with anchors or panels.
- **Fonts.** A text uses the default font (`/DreamGUI/DefaultFont_DistanceField`) unless its node id starts with
  `Font_<Key>_`: then it draws with the font key `<Key>` of the text parity corpus's fonts table
  (`Resources/TextParity/corpus.json`, `"fonts"`), the same faces the parity test and the Chrome reference use:

  | Key | Faces, in fallback order |
  |---|---|
  | `Latin` | Roboto (with its bold, italic and bold italic faces) |
  | `Synthetic` | Roboto alone: bold and italic are synthesized |
  | `CJK` | Roboto, Droid Sans Fallback |
  | `CJKLocale` | Roboto, GenEi Gothic for Japanese (`ja`), Droid Sans Fallback for Chinese (`zh-Hans`): the text's `Language` picks |
  | `CJKScaled` | Roboto, Droid Sans Fallback for CJK ranges only, at 120 % (size-adjust) |
  | `Arabic` | Roboto, Noto Naskh Arabic UI |
  | `Hebrew` | Roboto, Arial (`C:\Windows\Fonts\arial.ttf`: the engine has no Hebrew font) |
  | `Thai` | Roboto, Noto Sans Thai |
  | `Devanagari` | Roboto, Noto Sans Devanagari |
  | `Emoji` | Roboto, Noto Color Emoji (its glyphs are colour bitmaps in the font's atlas) |
  | `EmojiSegoe` | Roboto, Segoe UI Emoji (`C:\Windows\Fonts\seguiemj.ttf`; optional, for the parity corpus) |
  | `World` | Roboto, Noto Naskh Arabic UI, Arial, Droid Sans Fallback, Noto Sans Devanagari, Noto Sans Thai |
  | `Bitmap` | Roboto, as a bitmap font rather than a distance field |

  For example `Text Font_World_Greeting { Text = "שלום" }`. The font is a transient font made from the engine's own
  font files (`Engine/Content/Slate/Fonts`, `Engine/Content/Editor/Slate/Fonts`), distance field (outline
  multi-channel) unless the key says `"kind": "bitmap"`, so the picture is the same on every machine. A key's `bold`,
  `italic` and `boldItalic` files become the font's true style faces (`Latin`, `CJK` and `World` have Roboto's); a style
  a key has no file for is synthesized. A fallback face's `lang`, `unicodeRange` and `scale` become its fallback entry's
  cultures, ranges and scale. The font is put on the built tree before the tree is copied, through the text's `Font`
  property, as `Font = /Path` would be. A key the table does not have fails the test. Adding a key to the table adds it
  to both the scenes and the parity test.
- **Characters that cannot be seen** are written in the file as they are (the language's strings unescape only `\"`,
  `\\`, `\n`, `\t` and `\r`), or as character references (`&#x200B;`) in a text with `bRichText = true`. A file with a
  decomposed letter should use references: an editor that normalises text would compose it without a trace.

## Sizes in the layout scenes

An `Image` is measured as its sprite -- the default white sprite is a few pixels -- and not as `Brush.ImageSize`, which
counts only for a brush without a sprite (`UDreamImage::GetPreferredWidth`). A box in a layout scene therefore gets its
size from a `+ SizeBox` with its overrides set. Its label places itself (`bIgnoreLayout = true`), so the box is
measured as if it were empty and keeps the overrides, as an empty SizeBox does in UMG; a SizeBox holds one child, so a
box has one label at most. A panel that has to be able to measure nothing (the empty panel in
`Layout_CollapsedChildren`) is a plain `Widget`, since an Image's own size would count, with its colour on a child that
places itself.

## The scenes

| Scene | What it shows |
|---|---|
| `Layout_SpacerFillsAVerticalBox` | A Spacer in a Fill slot between two Auto bars fills what they leave |
| `Layout_WrapBoxInAHorizontalBox` | A WrapBox in a HorizontalBox's Fill slot wraps at the width it is given, and the row is as tall as its lines |
| `Layout_AspectRatioInAVerticalBox` | Width-controls-height at ratio 2 in a 300-wide VerticalBox: 300 x 150 |
| `Layout_CollapsedChildren` | A panel of collapsed children, and a SizeBox over a collapsed child, take no room |
| `Layout_GridSpanningColumn` | Column widths with a child spanning two columns (UMG's rule) |
| `Layout_OverlayCentreWithPadding` | A centred child with asymmetric padding in an Overlay: centred in the whole width, then moved by the padding (Slate's rule) |
| `Layout_WrapBoxFillEmptySpace` | Only the last slot of a WrapBox line fills the rest of it |
| `Layout_CanvasDockedToItsCorner` | A CanvasPanel measures a child docked to its corner once, plus the inset |
| `Layout_ScaleBoxDownOnly` | A DownOnly ScaleBox never enlarges, in its measurement as in its arrangement |
| `Text_SizeLadder` | The default font from 8 to 48 px; `_SmallTextCoverageOff` the same from the distance field |
| `Text_SmallCoverage` | Small text from coverage glyphs: 10 to 20 px, a rich-text mix, sup/sub, underline and strike, synthetic bold and italic, CJK at 12 and 14 px, a half-filled lyric and an alpha TextAnimation; at canvas scales 1, 1.25 (`_Scale125`) and 1.333 (`_Scale133`), and from the field (`_SmallTextCoverageOff`) |
| `Text_Emoji` | Colour emoji at 12 to 48 px, inside a `<color>` tag (their own colours kept), with an underlay, filled half way, italic |
| `Text_JustifyTabs` | Justified Latin (default and inter-character), a justified last line, Japanese and right-to-left Hebrew justified, tabs at 8 and 4 spaces and in a wrapped paragraph, a middle ellipsis |
| `Text_Corners` | `AVMWkxz4@` at 256 px, plain and with a 0.08 em outline |
| `Text_RichText` | Mixed sizes, superscript and subscript, underline across sizes, strike, bold and italic, a link |
| `Text_Bidi` | Hebrew and Arabic among English in left-to-right and right-to-left paragraphs |
| `Text_CjkPunctuation` | Japanese and Chinese wrapped in narrow boxes, punctuation kept off the wrong end of a line |
| `Text_CombiningMarks` | A decomposed letter against its precomposed form, overlay marks on every letter |
| `Extreme_LetterSpacing` | Letter spacing -5 to +3 px, plain, outlined, underlined, half alpha, bitmap with shadow and outline |
| `Extreme_LineHeight50` | A paragraph at half the font's line height |
| `Extreme_OnePixelBox` | Paragraphs in a box 1 pixel wide |
| `Extreme_HugeGlyphs` | Glyphs at 512 and 256 px |
| `Extreme_TinyText` | Text at 4, 6 and 8 px; `_SmallTextCoverageOff` the same from the distance field |
| `Extreme_CombiningStack` | Thirty combining marks on one letter |
| `Extreme_MixedSizesLine` | One line from 8 to 72 px; `_SmallTextCoverageOff` the same from the distance field |
| `Extreme_LongWords` | A URL and a long word in a 120 px box |
| `Extreme_InvisibleOnly` | A text of spaces and zero-width characters only |
| `Extreme_OverlappingTexts` | Two translucent texts over each other |
| `Extreme_WidestOutline` | An outline wider than the distance field reaches |

Nested custom tags with a style are not in `Text_RichText`: a custom tag's look comes from a rich-text style asset
(`UDreamUIRichTextCustomStyleData`), the plugin ships none, and a `.dui` cannot make one.
