---
name: dream-gui-diagnose
description: Resolve a DreamGUI .dui diagnostic or a layout problem — look the DUInnnn code up, explain the cause, fix the line — and confirm the fix with check and layout. Use when a .dui fails to check or compile, when a DUInnnn error or warning needs explaining, or when a screen compiles but looks wrong: off the screen, text cut or missing, buttons overlapping, things not lining up.
---

# dream-gui-diagnose `<code, message or symptom>`

Every diagnostic has a code `DUInnnn`. The first digit is the stage that refused, and says what kind of fix it wants:

| Range | Stage | The fix |
| --- | --- | --- |
| DUI1xxx | lexer | a character or a literal that does not read: a quote, a number with a unit glued on (`24px`), a colour with the wrong digit count |
| DUI2xxx | parser | the grammar: a `{` or `(` not closed, a node with no id, a property with no value |
| DUI3xxx | meaning | a name the compiler does not know (look it up), an id used twice, a style that does not exist |
| DUI4xxx | values | a value of the wrong kind for its property: look the property's type up |
| DUI5xxx | building the tree | the property cannot be written from text, or the object it is on does not exist |
| DUI6xxx | compiling | `props`, `events`, view models, handlers: what only a compile into the Blueprint sees |
| DUI7xxx | write-back | the designer could not write an edit into the file (not yours to fix in the file) |
| DUI8xxx | layout | the screen compiled but does not fit: see below |

## Steps

1. **Get the diagnostics with their lines.** With the `dream_gui` tool: `check { "file": "..." }`, then
   `diagnostics { "file": "..." }` for what the last compile said. Without it:
   `pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check <file>`.
2. **Look the code up.** Each diagnostic carries a `doc` link to its page; the same table is in the reference page
   `diagnostics` (`Plugins/DreamGUI/.skill/reference/diagnostics.md`). Read the message: it names the offending text.
3. **For a name the compiler does not know** (DUI3xxx, DUI4xxx): `lookup { "name": "<part of the name>" }` lists what
   exists; `lookup { "name": "<Type>.<Property>" }` gives a property's type and default. Use what `lookup` gives, never a
   name from another UI system.
4. **Fix the line** the diagnostic names, then `check` again. Fix errors before warnings: one error can cause others.
5. **Then lay it out**: `layout { "file": "...", "sizes": "1280x720, 2560x1440" }`, and fix the layout problems below.

## Layout problems (DUI8xxx)

`layout` lists every node with its rect; read the node the diagnostic names and its parent.

| Code | Cause, most often | Fix |
| --- | --- | --- |
| DUI8001 off the screen | a fixed `AnchoredPosition` measured from the centre, or a box with more children than room | anchor the node to the edge it belongs to (`AnchorMin`/`AnchorMax`/`Pivot` 0 or 1) and keep its position small; or give the box room |
| DUI8002 text does not fit | a fixed `SizeDelta` too small for the words; a text in a box that gives it no width | a bigger box, `@slot SizeRule = Auto`, `WrapTextAt = <the width you want>`, or a smaller `FontSize` |
| DUI8003 zero size / too small | a node with no `SizeDelta` and point anchors; a button sized by its content only | `AnchorData.SizeDelta = (w, h)`, anchors that stretch, `MinDesiredSize`; buttons at least 24 x 24 px (48 px tall reads well) |
| DUI8004 controls overlap | positions written by hand | put them in a `+ VerticalBox`/`+ HorizontalBox` with `Spacing` |
| DUI8005 nearly lined up (info) | two hand-written positions or sizes a pixel or two apart | give them the same value, or let one box align them |
| DUI8006 breaks at another size | fixed positions from the centre, fixed widths wider than the smaller screen | anchors to edges, boxes that fill (`@slot SizeRule = Fill`), sizes that stretch |
| DUI8007 low contrast (preview) | text colour close to what is behind it | a lighter text on a dark backdrop or the other way round, until the ratio is at least 4.5 to 1 |

When nothing is reported and the screen still looks wrong, `preview` draws it (if you can see images), and the tree in
`layout` says where each node really is: compare it with where you meant it to be. Common surprises:

- a box sizes itself to its content unless it is told to fill: a card written `SizeDelta = (420, 220)` inside an
  `Overlay` slot that centres it can come out the size of its content;
- an `Image`'s height comes from its texture (4 px for the default white one) unless something sizes it;
- a `RectBlock`'s `CornerRadius` is a fraction of its size by default: `(24, 24, 24, 24)` makes a pill.
