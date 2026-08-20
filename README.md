<p align="center">
  <img width="112" src="./Resources/Icon128.png" alt="DreamGUI">
</p>

<h1 align="center">DreamGUI</h1>

<p align="center">A 3D UI system for Unreal Engine 5.8 — widgets that live in the world, a prefab workflow, and a designer built to feel like UMG's.</p>

<p align="center">
  <a href="#install">Install</a> ·
  <a href="#how-it-differs-from-umg">vs UMG</a> ·
  <a href="#how-it-differs-from-upstream">vs upstream</a> ·
  <a href="#status">Status</a> ·
  <a href="#license">License</a>
</p>

---

DreamGUI is a fork of [LGUI / LexUI](https://github.com/liufei2008/LGUI) by Lex Liu, MIT licensed.
It is not a drop-in replacement for it — see [how it differs](#how-it-differs-from-upstream).

## What it is

A UI widget here is a `UObject` with a rect, a pivot and anchors, arranged into a tree and drawn by
a canvas that batches the whole tree into as few draw calls as it can. That is Unity's uGUI shape
rather than Slate's, and it buys three things UMG cannot do as directly:

- **UI in the world.** A canvas can render in screen space or sit on a surface in the level, at any
  angle, lit or unlit, with correct hit testing either way.
- **Prefabs.** A UI tree is an asset you instance, nest and override, rather than a Blueprint class
  you subclass.
- **Per-widget perspective.** A widget can establish a perspective its subtree is foreshortened
  into, the way CSS `perspective` works.

It costs you Slate's ecosystem: none of UMG's widgets, styles or bindings apply.

## Install

Requires **Unreal Engine 5.8**. Clone into your project's `Plugins/` directory:

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

Regenerate project files and build. That is the whole install for a fresh project.

### If you have assets authored against LGUI / LexUI

They reference the old class names and the old `/LGUI/` mount, so they need CoreRedirects — and
**the engine only reads those from the project's config**. A plugin's own `Config/DefaultEngine.ini`
is not consulted for them.

Copy the `[CoreRedirects]` block from
[`Config/DefaultEngine.ini`](./Config/DefaultEngine.ini) into your project's
`Config/DefaultEngine.ini`.

Skip this if you are starting fresh.

## How it differs from UMG

Worth knowing before you commit to either, because the difference is structural rather than
cosmetic.

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| Widget | `SWidget`, retained-mode Slate | `UObject` in a component-like tree |
| Sizing | Content-sized: a widget's size **is** its desired size | Box-first: you author a rect, content is arranged inside it |
| Text | The box grows to the text | The text is aligned in the box, and may overflow it |
| Placement | Slot-relative | Anchors + pivot, resolution-independent |
| Reuse | Widget Blueprint subclassing | Prefab assets with nested instances and overrides |
| In-world | `WidgetComponent`, a rendered quad | A first-class render mode |

The text difference is the one that surprises people. In UMG a `TextBlock` cannot overflow, because
its box is derived from the text; you control wrapping instead. Here the rect is authored, so text
can overflow it, and you get controls UMG has no need for — `Margin`, `LineHeightPercentage`,
`WrapTextAt` and **Best Fit** (shrink the font until it fits, which neither UMG nor Slate offers).

## How it differs from upstream

Forked from upstream `LexUI/5.7` at `765efeaf1` (2026-07-13); 214 commits since.

Upstream is actively developed, but the two branches can no longer be merged cheaply:

| | Upstream | Here |
| --- | --- | --- |
| Engine | UE 5.7 — no 5.8 branch on the LexUI line | **UE 5.8** |
| Layout | FlexBox + Grid family, still being developed | **Family deleted**; UMG-shaped panels only |
| Editor | — | Designer largely rebuilt |

The layout split is the sharpest of these. Upstream's 2026-08-08 fix for an infinite loop in
`ULexLayoutContainerFlexBox` has no meaning here, because that class no longer exists.

### What was rebuilt

**Layout**, along the lines Blink and Yoga use. Measurement is `const` and separated from
application; panels arrange into an immutable fragment that is committed in one write; desired size
is memoised for the duration of a pass; invalidation carries a reason, so moving a widget no longer
re-measures the whole ancestor chain. The legacy Lex layout family
(`ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
`ULexLayoutSelfGrid`, and the `ELexUILayoutMode` switch) was deleted.

**The prefab editor**, reviewed against UMG's widget designer. Viewport picking is by widget *rect*
rather than by rendered triangles — layout-only panels have no mesh, so a raycast could never hit
them, which made panels unclickable and undroppable. Added since: hover feedback, per-axis resize
handles, an anchor medallion, marquee selection, drag-to-reparent on the canvas, Content-Browser
drops onto the design surface, palette favourites, type-aware search, and undo coverage for create,
paste and drop.

**Text**, with the four controls listed above.

**Perspective**, per widget and inherited by its subtree. Requires a screen-space canvas with a
perspective projection; inert otherwise.

**Render transform**, widened to three dimensions, so a widget can be animated inside a layout
without the layout fighting it.

## Prefabs

The prefab editor works on a loaded preview hierarchy. Nothing reaches the asset until **Apply**,
and nothing reaches disk until **Save** — or until Apply does it for you, if *Save on Apply* is set
to something other than its default of *Never*.

Saving a child prefab refreshes its instances inside any loaded parent. Overrides that were
registered survive that refresh; changes that were never recorded as overrides can be replaced by
it. So: edit the source prefab for a change every instance should see, and pin an override for a
change only one parent should.

> [!WARNING]
> `SavePrefab` performs **full serialization**, not a property-level patch, and
> `ClearLoadedPrefab` + `Init` rebuilds the hierarchy and can discard unapplied editor changes.
> Tooling that rewrites prefabs should work on a transient duplicate, refuse to run against an
> editor with unapplied changes, and never call `SavePrefab` on a production asset from a test.

## Status

279 automation tests — `Automation RunTests DreamGUI`. There were none before this fork.

Known gaps:

- `LineHeightPercentage` and `WrapTextAt` are only reachable through a real font asset, so they are
  not covered by tests.
- 25 content assets still carry `Lex` in their names. Renaming a `.uasset` file does not rename the
  object inside it, so only an editor-side rename can change those; the code points at what is
  actually on disk.
- The editor work is verified by tests, not by eye. Expect rough edges in the designer.

## License

MIT — see [LICENSE](./LICENSE).

Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu

Substantial portions of this software remain the work of Lex Liu and are used under the MIT terms
of the [original project](https://github.com/liufei2008/LGUI). The MIT notice must travel with any
copy or substantial portion of this code, including yours.
