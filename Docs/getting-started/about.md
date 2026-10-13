---
title: How it differs
description: The structural differences from UMG (text overflow above all), why the names are UMG's and the parity tables, which upstream commit the fork starts from and what it rebuilt, and credits and license.
---

## How it differs from UMG

Worth knowing before you commit to either, because the difference is structural rather than cosmetic.

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| Widget | `SWidget`, retained-mode Slate | `UObject` in a component-like tree |
| Sizing | Content-sized: a widget's size **is** its desired size | Box-first: you author a rect, content is arranged inside it |
| Text | The box grows to the text | The text is aligned in the box, and may overflow it |
| Placement | Slot-relative | Anchors + pivot, resolution-independent |
| Reuse | Widget Blueprint subclassing | Widget Blueprint subclassing, plus named slots for content |
| In-world | `WidgetComponent`, a rendered quad | A first-class render mode |

The text difference is the one that surprises people. In UMG a `TextBlock` cannot overflow, because its box is derived
from the text; you control wrapping instead. Here the rect is authored, so text **can** overflow it, and you get
controls UMG has no need for: `Margin`, `LineHeightPercentage`, `WrapTextAt` and **Best Fit** (`bBestFit`: shrink the
font until it fits, down to `BestFitMinSize`), which neither UMG nor Slate offers.

### Coming from UMG: the names are the same

The structure differs; the vocabulary deliberately does not. A control here answers to the name its UMG counterpart
uses, with the meaning it has there: `ScrollWidgetIntoView` takes a destination and a padding, a scroll box has
`bFrontPadScrolling` and `WheelScrollMultiplier`, a spin box has `MinSliderValue` and `ClearMaxValue`, a panel slot has
`Nudge` and `bForceNewLine`, every widget has `SetIsEnabled`, `SetRenderShear` and a `FlowDirectionPreference`. Every
knob the details panel can turn is one a Blueprint can turn at runtime, through a setter that pushes the change instead
of writing a field nothing reads again.

Where a name could not be kept, that is written down rather than left to be discovered. `Resources/UMGParity` holds one
table per UMG class — 58 of them, some 960 Blueprint-facing members — and each row says one of three things:

| Status | Means |
| --- | --- |
| *adopt* | Same name, same meaning |
| *map* | Here under another name, or on another type, and which |
| *reject* | Deliberately absent, with the reason: there is no immediate-mode paint context to draw into, a rect block has no per-instance material, and so on |

The automation suite holds those tables against UMG's own reflection, in both directions: a member the engine gains
in an upgrade turns up as a row that does not exist, and a row naming something this plugin has since renamed turns up
as a name that does not resolve. The class-by-class comparison is in [UMG parity](../controls/umg-parity.md).

Two differences are worth knowing in advance:

- **A new knob defaults to what the control already did**, not to UMG's default, so that existing content does not
  move: `ScrollWhenFocusChanges` is `AnimatedScroll` here and `NoScroll` there, and the tables record each such case.
- **Appearance lives in a control's style struct, behaviour on the control**, so a few UMG properties are one level
  down: `EntrySpacing` is `Style.RowSpacing`.

`Docs/Reference` is the property and function reference, one page per class, each ending with that class's UMG
comparison. It is printed from reflection rather than written, so it cannot fall behind the headers:

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

This site's [API reference](../Reference/index.md) is those pages.

## How it differs from upstream

DreamGUI is a fork of [LGUI / LexUI](https://github.com/liufei2008/LGUI) by Lex Liu, and **not** a drop-in replacement
for it.

**The baseline.** Forked from upstream `LexUI/5.7` at `765efeaf1` (2026-07-13), with the upstream commits up to
`97d281376` (2026-07-21) rebased in afterwards: `97d281376` is the upstream this code starts from, and its content is
this repository's `5b48c42a`. Diff against that, not against `765efeaf1` or a merge base, or the rebased commits count
as this fork's changes. Upstream fixes after it are ported by hand.

Upstream is actively developed, but the two lines can no longer be merged cheaply:

| | Upstream | Here |
| --- | --- | --- |
| Engine | UE 5.7 — no 5.8 branch on the LexUI line | **UE 5.8** |
| Layout | FlexBox + Grid family, still being developed | **Family deleted**; UMG-shaped panels only |
| Editor | — | Designer largely rebuilt |

The layout split is the sharpest of these. Upstream's 2026-08-08 fix for an infinite loop in
`ULexLayoutContainerFlexBox` has no meaning here, because that class no longer exists.

### What was rebuilt

- **Layout**, along the lines Blink and Yoga use. Measurement is `const` and separated from application; panels arrange
  into an immutable fragment that is committed in one write; desired size is memoised for the duration of a pass;
  invalidation carries a reason, so moving a widget no longer re-measures the whole ancestor chain. The legacy Lex
  layout family (`ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
  `ULexLayoutSelfGrid`, and the `ELexUILayoutMode` switch) was deleted.
- **The designer**, reviewed against UMG's widget designer. Viewport picking is by widget *rect* rather than by rendered
  triangles — layout-only panels have no mesh, so a raycast could never hit them, which made panels unclickable and
  undroppable. Added since: hover feedback, per-axis resize handles, an anchor medallion, marquee selection,
  drag-to-reparent on the canvas, Content Browser drops onto the design surface, palette favourites, type-aware search,
  and undo coverage for create, paste and drop.
- **Text**, with the four controls listed above.
- **Perspective**, per widget and inherited by its subtree. It requires a screen-space canvas with a perspective
  projection, and is inert otherwise.
- **Render transform**, widened to three dimensions, so a widget can be animated inside a layout without the layout
  fighting it.
- **Input**, per player. Each local player has its own pointers, focus and text target, so a split screen's second
  player hovers, focuses and types on its own; the mouse, fingers and scripted pointers have id ranges of their own and
  no longer share an id. An optional Slate input source hears input before the viewport, so the UI keeps working in the
  engine's UI-only input mode.
- **Rendering.** A canvas walks only the widgets that asked to change, patches in place the sections whose geometry
  changed rather than rebuilding them, and answers its materials' parameters through render-thread proxies instead of a
  material instance per draw call. A render-target canvas is drawn by a render command of its own, so it updates
  whether or not anything renders its world. The screen effects share one way to read, crop and write back the screen,
  on the render graph's textures.

Two more structural changes: upstream's prefab asset model is gone, along with `SavePrefab`, `Apply`,
`ClearLoadedPrefab` and *Save on Apply* — a UI tree is a class now (see [Widget Blueprints](../concepts/widget-blueprints.md));
and the automation suite is part of the plugin, where upstream had none (see [Tests](../tools/tests.md)). The plugin
ships no redirects: assets saved under the old class names are resaved once with 1.0.0, 2.1.0's redirect block
borrowed for it; see [Migration](../guides/migration.md).

## Credits and license

MIT — see `LICENSE` in the repository:

```text
Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu
```

Substantial portions of this software remain the work of Lex Liu and are used under the MIT terms of the
[original project](https://github.com/liufei2008/LGUI). **The MIT notice must travel with any copy or substantial
portion of this code, including yours.** The same goes for the bundled msdfgen (Viktor Chlumsky, MIT), whose
`ThirdParty/msdfgen-single-file/LICENSE.txt` `Config/FilterPlugin.ini` packages into BuildPlugin's output.

Source and issues: https://github.com/TypeDreamMoon/DreamGUI
