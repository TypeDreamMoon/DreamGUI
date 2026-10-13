---
title: Layout
description: How the layout engine works — measurement is const and separate from arranging, arranging produces an immutable fragment committed in one write, desired size is memoised for a pass, invalidation carries a reason — plus the UMG-shaped panels, panel slots, the deleted Lex layout family, and why a wrapping text in a fill slot wants WrapTextAt.
---

A widget's own `AnchorData` decides its rect — until its parent carries a layout container. A widget carrying one is a
**panel**: it measures its children, decides their rects, and writes the result back into their `AnchoredPosition` and
`SizeDelta`. A panel is not a separate class here but a sub-object of the widget (`LayoutContainer`), so a widget can be
laid out one way and drawn another. `bIgnoreLayout` on a child takes it out of its parent's layout.

## Rebuilt along the lines of Blink and Yoga

The layout was rebuilt the way Blink and Yoga do it, on four principles:

1. **Measuring is `const` and separate from arranging.** A panel's `MeasureLayout(WidthSpec, HeightSpec)` is a `const`
   method: it answers "how big, inside this constraint" and writes nothing. The constraint is an `FDreamMeasureSpec`, in
   the sense Android's MeasureSpec and Yoga's measure mode use: `Undefined` (no constraint, report the natural size),
   `AtMost` (the natural size, but no larger than the value) and `Exactly` (the value, whatever the content). Specs flow
   down from the one place real numbers exist — a panel's arrange, which knows the rect its parent gave it — and each panel
   narrows what it received by the space it is about to spend (`ForChild`: padding, spacing, the siblings' fixed extent)
   before handing it on.
2. **Arranging produces an immutable fragment, committed once.** A panel's `ArrangeChildren` records into an
   `FDreamFragment` — its own size and a rect per child — and writes no child geometry; the base class then commits it onto
   the widgets in one pass (`CommitFragment`). A layout result used to have nowhere to live except the widgets themselves,
   so arranging *was* writing, and every write re-entered invalidation while the pass that caused it was still running.
   Blink hit the same wall and answered it the same way: its layout algorithm returns an immutable fragment rather than
   mutating the layout tree in place.
3. **Desired size is memoised for a pass.** Measuring a panel asks its container, which measures every child, each of
   which may be a panel — one question costs O(children^depth). Within one arrange or measure, desired sizes are remembered
   (`FDesiredSizeMemoScope`) and all forgotten when the outermost scope closes. That is sound only because an arrange no
   longer writes.
4. **Invalidation carries a reason.** `EDreamLayoutInvalidation` has two values: `Measure` (this widget's desired size may
   have changed, so every ancestor that measures it re-runs) and `Arrange` (only where it sits inside its parent changed —
   panels measure their children by desired size, never by position, so nothing above the parent can produce a different
   answer; the parent re-arranges and the walk stops there). Moving a widget no longer re-measures the whole ancestor chain.

One rule runs through all of it: **layout output never feeds back into measurement.** When a panel asks how big a child
wants to be (`UDreamPanelLayoutBase::GetDesiredSize`) it looks at the fitter, the container's preferred size, the
visual's intrinsic size, the content children and the authored rect, in that order — never at a rect a panel pass wrote.

### Driving layout from code

| Call | Does |
| --- | --- |
| `GetDesiredSize` | How big this widget **wants** to be, answered by its parent panel; a widget with no measuring parent reports the size it has |
| `ForceLayoutPrepass` | Lay the tree out now rather than on the next pass — when you have just changed content and need the size in the same frame |
| `InvalidateLayoutAndVolatility` / `UDreamUIBPLibrary::InvalidateLayout` | Ask for a layout pass next frame (the "volatility" half has no counterpart here) |
| `UDreamPanelSlot::NotifySlotChanged(Reason)` | The slot changed; alignment, size rule, fill weight and z-order pass `Arrange`, while padding, the grid coordinates and `bAutoSize` keep the default `Measure` |

## Self layout: four variants

A panel looks after its **children**; how big a widget itself wants to be is answered by its **self layout**
(`LayoutSelf`) — a sub-object that has nothing to do with the panel:

| Class | Notes |
| --- | --- |
| `UDreamLayoutSelf` | The default self layout |
| `UDreamLayoutSelfAspectRatio` | Constrains itself to an aspect ratio |
| `UDreamLayoutSelfAuthoredSurface` | Uses an authored surface |
| `UDreamLayoutSelfSpacer` | A placeholder that takes up the remaining space |

## The panels

Every panel is a subclass of `UDreamPanelLayoutBase`, named and behaving after its UMG counterpart. In a `.dui` the
container's name is itself a node type (`VerticalBox Column { Spacing = 29 }` is `Widget Column { + VerticalBox { Spacing = 29 } }`).

| `.dui` type | Class | Main properties | Notes |
| --- | --- | --- | --- |
| `HorizontalBox` / `VerticalBox` / `StackBox` | [`UDreamLayoutContainerHorizontalBox`](../Reference/DreamLayoutContainerHorizontalBox.md) / [`…VerticalBox`](../Reference/DreamLayoutContainerVerticalBox.md) / [`…StackBox`](../Reference/DreamLayoutContainerStackBox.md) | `Padding`, `Spacing`, `Orientation` (StackBox) | Both boxes are StackBox subclasses |
| `Overlay` | [`UDreamLayoutContainerOverlay`](../Reference/DreamLayoutContainerOverlay.md) | `Padding` | Stacks children in declaration order; the first is behind |
| `CanvasPanel` | [`UDreamLayoutContainerCanvasPanel`](../Reference/DreamLayoutContainerCanvasPanel.md) | `bSortChildrenByZOrder` | Children place themselves by their own anchors; not mirrored for right-to-left (nor is Slate's `SConstraintCanvas`) |
| `GridPanel` | [`UDreamLayoutContainerGridPanel`](../Reference/DreamLayoutContainerGridPanel.md) | `Padding`, `Spacing`, `ColumnFill`, `RowFill` | Children placed by the slot's `Row`, `Column`, `RowSpan`, `ColumnSpan` |
| `UniformGridPanel` | [`UDreamLayoutContainerUniformGridPanel`](../Reference/DreamLayoutContainerUniformGridPanel.md) | `SlotPadding`, `MinDesiredSlotWidth`, `MinDesiredSlotHeight` | Equal cells |
| `WrapBox` | [`UDreamLayoutContainerWrapBox`](../Reference/DreamLayoutContainerWrapBox.md) | `WrapSize`, `bExplicitWrapSize`, `Orientation`, `HorizontalAlignment` | Three slot properties of its own, below |
| `ScrollBox` | [`UDreamLayoutContainerScrollBox`](../Reference/DreamLayoutContainerScrollBox.md) | `ScrollOffset`, `WheelScrollMultiplier`, `bFrontPadScrolling`, `bBackPadScrolling`, `ScrollWhenFocusChanges` | Abstains from measurement along its scroll axis — the content's extent must never reach the parent |
| `SizeBox` | [`UDreamLayoutContainerSizeBox`](../Reference/DreamLayoutContainerSizeBox.md) | `WidthOverride` / `HeightOverride` (with `bOverride*`), `MinDesiredSize`, `MaxDesiredSize`, `MinAspectRatio`, `MaxAspectRatio` | |
| `ScaleBox` | [`UDreamLayoutContainerScaleBox`](../Reference/DreamLayoutContainerScaleBox.md) | `Stretch`, `UserSpecifiedScale`, `StretchDirection` (`Both`, `DownOnly`, `UpOnly`) | The only panel that scales what it arranges |
| `SafeZone` | [`UDreamLayoutContainerSafeZone`](../Reference/DreamLayoutContainerSafeZone.md) | `bUsePlatformSafeZone`, the four `bPad*`, `SafePadding` | |
| `WidgetSwitcher` | [`UDreamLayoutContainerWidgetSwitcher`](../Reference/DreamLayoutContainerWidgetSwitcher.md) | `ActiveWidgetIndex` | |
| `Border` | [`UDreamLayoutContainerBorder`](../Reference/DreamLayoutContainerBorder.md) | `Padding`, `HorizontalAlignment`, `VerticalAlignment`, `BrushColor`, `ContentColorAndOpacity` | As in UMG, the alignment belongs to the border, not the slot |
| `MenuAnchor` | [`UDreamLayoutContainerMenuAnchor`](../Reference/DreamLayoutContainerMenuAnchor.md) | `Placement`, `MenuClass`, `bIsOpen`, `bFitInWindow` | An open menu goes through the popup layer; see [Input](input.md) |

A widget has one layout container: `+ HorizontalBox` on a container-typed node is DUI5022. Some containers need a
behaviour on the widget to match the UMG panel they mirror (a size box needs the ContentWidget, for example), and the
missing ones are added when the container is assigned. A container can also animate its layout: `bUseAnimation` with an
`AnimationHandler` makes children **tween** to their new places instead of jumping. The three classes live together in
`Core/Components/DreamLayout.h`: `UDreamLayoutAnimation` (the `UObject` base, with `GetLayoutContainer()` and the Blueprint
event `OnApplyLayoutResults`), `UDreamLayoutAnimation_CommonTween` (a general tween; `Duration` defaults to **0.3** and can
be swapped for a curve) and `UDreamLayoutAnimation_SlideIn` (slides in).
⚠️ They are **`UObject`s, not components** — you will not find them among the components you add to a widget.

```dui
VerticalBox Settings {
    Spacing = 12
    Padding = (24, 24, 24, 24)

    Text Title {
        Text = "Settings"
        FontSize = 28
        @slot HorizontalAlignment = Center
    }

    HorizontalBox VolumeRow {
        Spacing = 16

        Text VolumeLabel {
            Text = "Volume"
            @slot VerticalAlignment = Center
        }
        // Takes whatever width the row has left
        Native.Slider Volume {
            @fill
            @slot VerticalAlignment = Center
        }
    }

    Text Hint {
        Text = "Changes apply at once."
        @slot { SizeRule = Auto  Padding = (0, 8, 0, 0) }
    }
}
```

## Slots

UMG has a slot class per panel; here there is one, `UDreamPanelSlot`, owned by the **child**, so there is nothing to cast:
`AddChild` returns it, and `UDreamUILayoutLibrary::SlotAsPanelSlot` is UMG's whole `SlotAs*` family in one node.

| Property | Notes |
| --- | --- |
| `Padding` | The margin around the child |
| `HorizontalAlignment` / `VerticalAlignment` | `Fill`, `Left` / `Top`, `Center`, `Right` / `Bottom`; the slot's own default is `Fill`. A border's alignment is on the border |
| `SizeRule` / `FillWeight` | In a box: `Auto` takes the desired size, `Fill` shares out the remaining space by weight; `@fill` and `@fill 2` are the `.dui` shorthand |
| `Row` / `Column` / `RowSpan` / `ColumnSpan` | Coordinates and spans in a grid |
| `ZOrder` | Paint order among siblings, as a stable reorder. Overlay and GridPanel always read it, CanvasPanel with `bSortChildrenByZOrder` on; every other panel arranges strictly by sibling order. UMG's grid-slot `Layer` is this |
| `bAutoSize` | Size to content, UMG's canvas-slot property of the same name |
| `MinDesiredSize` / `MaxDesiredSize` | A floor and ceiling on whatever the child measures, per axis; zero means no opinion |
| `bFillEmptySpace` / `FillSpanWhenLessThan` / `bForceNewLine` | WrapBox only: the last child on a line takes the room left; a line of its own once the wrap width drops below this; begin a line here |
| `Nudge` | A fixed offset added to wherever the panel put the child, in the panel's content space (x right, y down, the same sense as `Padding`) |

A slot defaults to `Fill`, which every `.dui` file and every control that builds its own tree was written against. Only
when a child is **added by hand** in the designer is a panel-specific default written into the slot, as UMG's per-slot
classes have them (an overlay stacks things at their own size in its corner, a box gives each child its band, a scale box
centres). UMG's canvas-slot family — `SetPosition`, `SetSize`, `SetOffsets`, `SetAnchors`, `SetAlignment`, `SetLayout` —
reads and writes the child's own `AnchorData`; `GetAlignment` is this plugin's `Pivot`.

## Right to left

`FlowDirectionPreference` (`Inherit`, `Culture`, `LeftToRight`, `RightToLeft`) says which way a widget's layout flows.
`Inherit`, the default, takes the answer from the nearest ancestor that states one, so setting `Culture` once on a root
mirrors a whole screen. Mirroring is done in one place: a panel reflects the finished rect about its own width. That one
reflection is the whole of what mirroring a horizontal layout means — it reverses the order of anything laid out along x,
swaps `Left` and `Right` alignment, and swaps the left and right halves of the panel's and the slot's padding; nothing
vertical moves, and measurement is untouched. A canvas panel's children place themselves by their anchors and never pass
through that step, so a canvas is not mirrored.

## The Lex layout family is gone

Upstream's FlexBox + Grid family — `ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
`ULexLayoutSelfGrid` and the `ELexUILayoutMode` switch — was deleted here, leaving the UMG-shaped panels only. There is no
redirect, because there is nothing for one to point at: a tree laid out by one of them is laid out again by hand.
Upstream's 2026-08-08 fix for an infinite loop in `ULexLayoutContainerFlexBox` has no meaning here, since the class no
longer exists. See [Migration](../guides/migration.md).

## A wrapping text in a fill slot

A text that wraps at its box (`OverflowType` is `VerticalOverflow`, or `bAutoWrapText` is on) is as tall as the width it
gets. A panel always asks a child how big it wants to be **before** giving it a width, so what comes back depends on
whether the panel offered one when it asked:

- Offered a width — a vertical box measuring its children offers its own — the text answers with its height at that
  width. A paragraph in a vertical box's Fill slot is measured at the slot's width, not at the width the widget happened
  to have.
- Offered none — a horizontal box measuring along its own axis — the text answers with the paragraph's one-line width,
  and wraps once it is arranged narrower than that.

So give a wrapping text in a horizontal arrangement a `WrapTextAt`: it then breaks at that width, and is measured at it,
whatever the panel offered. The shipped sample, `HelloDreamGUI.dui`, gives each of its texts a `WrapTextAt` too, so they
break at the width they were designed for.

```dui
VerticalBox Column {
    Padding = (24, 28, 24, 28)

    // A horizontal box offers no width along its own axis: give the text a width to break at
    HorizontalBox Row {
        Image Icon { Brush.ImageSize = (32, 32) }
        Text Description {
            Text = "A description long enough to wrap."
            WrapTextAt = 320
            @fill
        }
    }
}
```

`WrapTextAt` has a use of its own as well: the box is also where the text is **aligned**, so a wrap width narrower than the
box gives a narrow column of text that still centres over the whole widget. The rest of the text rules are in
[Text](text.md).
