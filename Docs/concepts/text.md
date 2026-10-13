---
title: Text
description: The text system — the distance-field (MTSDF) default font, small text from hinted coverage glyphs, fallback faces and colour emoji, box-first overflow with Margin, LineHeightPercentage, WrapTextAt and Best Fit, rich text, gradient text, culture-aware line breaking, incremental layout, and the DreamGUI.Text.* console variables.
---

Text is drawn by the `UDreamText` visual (`Text` in a `.dui`). Its measurements were held against Chrome and Slate and
rebuilt where they differed: grapheme clusters and bidi levels, ligatures, CSS line boxes, fallback faces chosen by
Unicode range, culture and presentation, real bold and italic faces, colour emoji, justification, tab stops and a middle
ellipsis.

## Box-first

A UMG `TextBlock` cannot overflow, because its box is derived from the text; you control wrapping instead. Here the rect
is authored, the text is aligned in the box, and `OverflowType` says what happens when it does not fit:

| `OverflowType` | When it does not fit |
| --- | --- |
| `HorizontalOverflow` | Characters run out of the rect sideways |
| `VerticalOverflow` (default) | Lines wrap and run out of the rect downwards |
| `Truncate` | Characters that do not fit on the right are removed |
| `Ellipsis` | What does not fit is replaced with `...` |
| `MiddleEllipsis` | The middle of each line that does not fit becomes `...`, its start and end kept (Slate's MiddleEllipsis); with wrapping, an end ellipsis on the last visible line instead |

`bAutoWrapText` wraps under the other policies too (UMG's AutoWrapText): with `Ellipsis` it gives a wrapped paragraph whose
last visible line is elided, with `Truncate` a paragraph cut off at the bottom of the box. Alignment is `HAlign` (`Left`,
`Center`, `Right`, `Justify`) and `VAlign` (`Top`, `Middle`, `Bottom`).

Because the box is authored, you get controls UMG has no need for:

| Property | Does |
| --- | --- |
| `Margin` | Padding between the rect and the text, as UMG's text Margin. The text is wrapped, aligned and overflow-tested against the rect **minus** this, so a margin narrows the wrap width rather than just shifting the result; it is added to the text's preferred size too |
| `LineHeightPercentage` | Scales the distance from one line to the next, 1 being the font's own line height. Only the gap moves; the glyphs keep their size. `FontSpace.Y` is a flat distance added on top: one scales with the font, the other does not |
| `WrapTextAt` | Wrap at this width instead of the content box's; zero or less keeps the content box. The box is also where the text is **aligned**, so a narrower wrap width gives a narrow column of text that still centres over the whole widget. It does not truncate: `Truncate` and `Ellipsis` still measure against the box |
| `MinDesiredWidth` | A floor under the width this text reports to a content-sized parent, so a label that is momentarily short (a number counting down, a name not arrived yet) does not collapse its row |
| `bBestFit` / `BestFitMinSize` | Best Fit: shrink the font until the text fits the content box. `FontSize` becomes the size the text may **reach**, and `BestFitMinSize` how small it may go before it simply overflows. `GetRenderedFontSize` is the size actually drawn |

Best Fit is uGUI's; neither UMG nor Slate has it, because their text is content-sized and the box grows to the text. Here
the box is authored, which is exactly the case that needs it. Measuring a wrapping text inside a panel has one trap; see
the last section of [Layout](layout.md).

```dui
Widget Panel {
    Text Body {
        AnchorData.SizeDelta = (360, 120)
        Text = "The box is authored: the text is aligned in it, and what does not fit is handled by OverflowType."
        FontSize = 16
        OverflowType = Ellipsis
        bAutoWrapText = true
        Margin = (12, 8, 12, 8)
        LineHeightPercentage = 1.2
    }

    Text Price {
        AnchorData.SizeDelta = (120, 40)
        Text = "1,234,567"
        FontSize = 32
        bBestFit = true
        BestFitMinSize = 12
    }

    Text Score {
        bRichText = true
        Text = "Score <size=40><gradient=Gold>9999</gradient></size>"
    }
}
```

Other typesetting properties: `bUseKerning`, `bLigatures` (on by default), `FontSpace` (flat letter and line spacing),
`TabSize` (tab stops in spaces, 8 by default, CSS `tab-size`), `TextJustify` and `LastLineAlign` (where a justified line
gets its extra room, and how its last line aligns), `TextTransform` (the case drawn; the authored text is kept),
`FlowDirection` (`Auto` asks the bidi algorithm), and `bUnderline` and `bStrikethrough` (rich text's `<u>` and `<s>` nest
on top of them).

## Fonts

A font is an asset, and **four** types sit on one inheritance chain: the abstract base **`UDreamUIFontData_BaseObject`**
(the whole interface of what font data can do — glyphs, the shaping face, line height, fallback faces), the abstract
intermediate **`UDreamUIFontData_FreeTypeRender`** (reads a font file through FreeType), and its two subclasses — the
distance-field font **`UDreamUIFontData_DistanceField`** (the plugin's default font uses it) and the bitmap font
**`UDreamUIFontData_Bitmap`**. ⚠️ The source annotates `_Bitmap` as **no longer maintained**; new features are not
implemented for it. The project's default font is `UDreamGUISettings::DefaultFont`.

- **The default font is Slate's**: Roboto in real regular, bold, italic and bold-italic faces, with DroidSansFallback for
  CJK, on the outline multi-channel distance field (`/DreamGUI/DefaultFont_DistanceField`).
- **Distance fields.** `SdfSource` defaults to `OutlineMultiChannel` (MTSDF: sharper corners, a BGRA atlas). The outline,
  the underlay (drop shadow), the glow and the lyric-style fill (`TextStyle`, an `FDreamTextStyle`) are drawn by the
  built-in shader, so they need a distance-field font and *Use Built-in UI Shader* on; their lengths are in em, so a style
  reads the same at every size. The style is stored per widget in the canvas's widget property texture, so texts with
  different styles still batch into one draw call.
- **Bold and italic.** `BoldFont`, `ItalicFont` and `BoldItalicFont` name real faces; synthetic ones are used only without
  them.
- **Fallback faces.** Each `Fallbacks` entry (`FDreamUIFontFallback`) has a `Font`, `Ranges` (code points), `Cultures`
  (the languages it is for), a `Scale` and `bPreferOverPrimary`. The old `FallbackFontArray` turns into `Fallbacks` entries
  with the default settings as it loads. Chinese culture names get their script put back (`zh-Hans`, `zh-Hant`), so
  fallbacks written against those match in a packaged game too.
- **Colour emoji**: CBDT/CBLC, sbix and COLRv0; `bPreferColorEmoji` is on by default. The shadow, outline and long-shadow
  modifiers draw a colour emoji's copies as silhouettes.

Shipping font files, a colour emoji font and the ICU data with a game is covered in
[Fonts and packaging](../guides/fonts-and-packaging.md).

## Small text

Small screen text — up to `SmallTextMaxPixelSize` device pixels per em, 20 by default — is drawn from **hinted coverage
glyphs** placed on the device pixel grid, crisp as Slate's (`UDreamGUISettings::bSmallTextCoverage`, on by default).
Larger text draws from the distance field as before.

- Small text with an outline, a glow or an underlay draws its face from coverage glyphs (unhinted under an outline thinner
  than 2 device pixels) and those effects from the field, lined up with it (`SmallTextEffectFace` `Auto`; `Field` keeps such
  text wholly on the field).
- Small text inside a render layer draws from coverage once the layer has held still for 3 frames, and from the field while
  it moves.
- Face softness and dilation keep a text on the field.
- One text can stay on the field at every size with `SmallTextRaster = Off`; one font has its own `SmallTextCoverage`,
  `SmallTextMaxPixelSize` and `CoverageHinting`.
- `SmallTextContrast` (1 by default) sets the coverage glyphs' contrast; `bSmallTextCorrection` (on) darkens what still
  draws from the field at those sizes. A world repaints at most `SmallTextRepaintBudgetPerFrame` (512) texts a frame onto
  coverage glyphs.
- A font keeps the atlas cells of its coverage glyphs while any text draws from them. Past twice `MaxCoverageCells` (4 by
  default, in *Project Settings > Plugins > DreamUI*) new small-text glyphs wait, drawn from the field meanwhile, with one
  warning per font.
- A material of your own that does not shade through `MF_DreamUI_Shade` gets no coverage glyphs.

## Rich text

With `bRichText` on, tags in the text take effect; `RichTextTagFilterFlags` says which kinds are parsed (`Bold`,
`Italic`, `Underline`, `Strikethrough`, `Size`, `Color`, `Superscript`, `Subscript`, `CustomTag`, `Image`, `Hyperlink`,
`Language`, `Gradient`), and a tag filtered out shows as text (a `<lang>` with the `Language` flag off, for example).
Tags include `<b>`, `<u>`, `<s>`, `<size=40>`, `<color=…>`, `<sup>` / `<sub>`, inline images `<img=Tag/>` (optionally with a
size and an alignment word such as `baseline` or `top`), the clickable `<a=Id>` (`OnHyperlinkClickedBP`), `<lang=ja>`
(what it encloses takes that language's fallback faces and forms), and `<gradient=Name>` from the next section. A literal
`<` is written `&lt;`.

Custom tags and styles come from `RichTextCustomStyleData` (`UDreamUIRichTextCustomStyleData`), inline images from
`RichTextImageData`. The control library's [`UDreamRichTextBlock`](../Reference/DreamRichTextBlock.md) is the
counterpart of UMG's `RichTextBlock`.

## Gradient text

A text's face, its outline and an overlay over its face can each be painted with a gradient. The three layers are on
`FDreamTextStyle` — `FacePaint`, `OutlinePaint`, `OverlayPaint` — each an `FDreamTextPaint` (`bEnabled`, a shared
`Preset`, or a `Gradient` of its own).

- **Kinds**: `Linear`, `Radial`, `Conic`, `Diamond` and `Corners` (four-corner, as TextMeshPro has it), up to 16 stops;
  `Pad`, `Repeat` or `Reflect` beyond the ends; mixed in sRGB, linear light or Oklab.
- **Written as CSS writes them**: `linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`, in the details panel, in a
  `.dui`, or as a rich-text run. It is read as Chrome reads it but for two things: `to <corner>` takes a square's angle
  whatever the box's shape, and colour names are CSS's (`green` is #008000, where `<color=green>` is #00FF00).
- **Measured across**: `PaintBoxHorizontal` / `PaintBoxVertical` choose, per axis, `TextBlock`, `ContentBox`, `Line`,
  `Glyph` or `Run`.
- **A face paint takes the place of the text's own colour**, as CSS `background-clip: text` does; a `<color>` run inside
  it is solid. The overlay is mixed onto the face by `OverlayBlend` (`Normal`, `Add`).
- **Presets**: `UDreamGradientAsset` assets, or CSS strings by name in the project settings (`GradientPresets`). Texts that
  paint with the same gradient share its row on the GPU.
- **Rich text**: `<gradient=Name>…</gradient>`, the name looked up as a custom style entry, then a project preset, then
  read as CSS written without spaces. `Gradient` is a new filter flag: a text whose `RichTextTagFilterFlags` was saved as a
  mask without it shows the tag as text.

A paint's phase, angle, centre and scale animate **without laying the text out or painting it again** — each change writes
one pixel of the text's paint table. `FacePaintPhase`, `OutlinePaintPhase`, `OverlayPaintPhase` and `PaintAngleOffset` are
keyable in Sequencer, and [`UDreamTextPaintLibrary`](../Reference/DreamTextPaintLibrary.md) tweens them with
`PaintPhaseTo`, `PaintAngleTo` and `PlayShimmer`:

```cpp
#include "Core/Text/DreamTextPaintLibrary.h"

// Title is a UDreamText*
FDreamTextPaint Face;
Face.bEnabled = true;
FDreamGradient::ParseCss(TEXT("linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)"), Face.Gradient);
Title->SetFacePaint(Face);

// A band transparent at both ends, added onto the face: a shimmer
FDreamTextPaint Band;
Band.bEnabled = true;
FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, transparent 40%, white 50%, transparent 60%)"), Band.Gradient);
Title->SetOverlayPaint(Band);
Title->SetOverlayBlend(EDreamTextOverlayBlend::Add);

UDreamTextPaintLibrary::PlayShimmer(Title, 1.2f, 0.0f, -1);   // 1.2 s a pass, until stopped
```

A material that does not shade through `MF_DreamUI_Shade` paints gradients by vertex colour instead (and then an animation
repaints).

## Line breaking by culture

The line and word iterators are made for **the game's current culture**, and made again when it changes (2.0 followed the
operating system's language).

- A game with CJK text has to be packaged with the ICU data for it (the EFIGSCJK preset; All for Thai and its neighbours).
- In the editor, DreamGUI's own copy of ICU is pointed at the engine's ICU data (`Engine/Content/Internationalization`),
  so the editor breaks lines by the game's culture as a packaged game does; without the data the log says so once and the
  engine's default-culture iterators stand in.
- `PhraseWrap = CJKDictionary` keeps CJK words together when wrapping, using ICU's dictionary (CSS
  `word-break: auto-phrase`). It only matters with `VerticalOverflow` and needs the packaged ICU data to include the CJK
  dictionary; without it, it quietly falls back to per-character breaks.
- `Language` is the language the text is written in (`"ja"`, `"zh-Hans"`, `"en-US"`): the fallbacks meant for it are
  preferred, and HarfBuzz picks the forms a script draws differently per language (`locl`). Empty is the game's current
  language; a rich text's `<lang=xx>` overrides it. Line breaking follows the game's culture **whatever** this says.

## Incremental layout

A text that asks for it (a field being typed into), or a long one (256 elements or more) whose content changed in two
layouts in a row, keeps what its layout found and lays out again only what an edit touched: inside a long paragraph only a
window around the edit is shaped and measured again and spliced into the rest, and the display list is edited where it
stands rather than written again. A word shaped before — same face, size, script, direction, language and features — is
taken from the shape cache instead of being shaped again.

An incremental layout and a fresh one of the same text are the same, element for element; the switches below only trade
time. An edit near the start of a long paragraph that opens with punctuation, a digit or an emoji, or one just after an
inline image, is shaped as the whole paragraph would shape it.

## Console variables

| Variable | Default | Does |
| --- | --- | --- |
| `DreamGUI.Text.SmallTextCoverage` | `-1` | `-1` follows the project setting; `0` turns coverage off for every font, those set On included; `1` turns it on for every font that leaves it to the project. A change repaints every text |
| `DreamGUI.Text.SmallTextMaxPixelSize` | `0` | Above 0, replaces `SmallTextMaxPixelSize` for every font with no limit of its own — to force coverage onto larger labels for a measurement |
| `DreamGUI.Text.SmallTextOnMove` | `0` | What coverage-drawn small text does when it moves off its pixel grid: `0` repaints from coverage at every move; `1` keeps its coverage quads while it moves and repaints once it has held still for 3 frames; `2` repaints once from the field, and from coverage again after 3 still frames |
| `DreamGUI.Text.IncrementalLayout` | `1` | `0`: every layout starts from nothing, and nothing is kept |
| `DreamGUI.Text.IncrementalParse` | `1` | `0`: every element is read again |
| `DreamGUI.Text.IncrementalMeasure` | `1` | `0`: an edited paragraph is measured whole (needs `IncrementalParse`) |
| `DreamGUI.Text.InPlaceDisplayList` | `1` | `0`: the display list is written whole |
| `DreamGUI.Text.VerifyIncremental` | `0` | `1`: every layout built on a kept one is laid out again from nothing and compared; the first difference is logged and the fresh layout used. For finding a fault in incremental layout; not in Shipping builds |
| `DreamGUI.Text.ShapeCache` | `1` | `0`: every run is shaped whole, without the cache |
| `DreamGUI.Text.ShapeCacheKB` | `4096` | The shape cache's budget in KB; 4096 holds about 15,000 words, least recently used first out. A change empties the cache |
| `DreamGUI.Text.ShapeCacheFlush` | command | Empties the shape cache; its counters are kept |

Two more switches are off until measured: the project setting `bFieldTextCorrection` (field text above
`SmallTextMaxPixelSize` gets small text's contrast and linear-light blend, so its weight does not jump at the cut-off), and
the console variable `DreamGUI.Scroll.SnapToDevicePixels` (a scroll view on a 2D canvas moves its content in whole device
pixels). `DreamGUI.Memory` lists each font's glyph atlas — slices, GPU bytes and the CPU copy, cells, glyphs, face bytes.
