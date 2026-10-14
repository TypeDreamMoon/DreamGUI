---
title: Your first screen
description: Take the plugin's HelloDreamGUI.dui from text to the screen, read the file line by line, then stand the same class up in a level.
---

The plugin ships one sample, `Content/Samples/HelloDreamGUI.dui`. It is the smallest `.dui` that is still a real
screen — an anchored root, an overlay, a card, a vertical column of text. The four steps below are the whole path from
text to a UI on screen, and nothing else needs configuring: the screen root, the raycaster and the event system are
created on demand.

## Copy the file into the project first

Copy it under the project's `DUI/` directory — say `<Project>/DUI/HelloDreamGUI.dui` — and edit only that copy. Two
reasons:

- the copy in the plugin folder is a reference, and a plugin update overwrites it;
- when you pick a file, DreamGUI stores its path relative to a `DUI/` root — `HelloDreamGUI.dui` for one under the
  project, `Plugin.X:…` for one under a plugin. A file under no `DUI/` root can only be stored as an absolute path,
  which works on exactly one machine.

A `.dui` is source, not an asset, so it does not go under `Content/`. See [Project layout](project-layout.md).

## Four steps to the screen

### Make a widget Blueprint

The Content Browser's **Add** menu (or a right-click on empty space) → **DreamGUI Widget**, in the top group beside the
engine's Blueprint Class and Material; it is also first in the **DreamGUI** submenu. Two pickers follow:

- **Parent class**: pick **DreamUI Text User Widget** (`UDreamTextUserWidget`). Only a widget Blueprint with that parent
  builds its tree from a `.dui`; with any other parent the designer does not show the source-file button at all.
- **Root layout**: anything, None included — the compile replaces the hierarchy with the file's tree.

Put it in `/Game/UI` and name it `WBP_HelloDreamGUI`. The file's first line, `class /Game/UI/WBP_HelloDreamGUI`, names
that place; when the two disagree the compile warns with [DUI6003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui6xxx#dui6003) — move the
asset or edit the line.

### Point it at the file

Open the Blueprint. The designer toolbar has a source-file button, which reads **No Source File** while none is named.
Open its menu → **Set Source File...** → pick the file you copied in. The button then shows the file's name, and its
tooltip the resolved path.

The same menu has **Create Source File...** (offered only while the class names no file: it writes a new `.dui` with a
starter hierarchy and points the class at it), **Open in Default Editor**, **Show in Explorer**, and, with the VS Code
extension, **Reveal in VS Code**.

### Compile

The hierarchy panel now holds the tree the file describes: `Root`, `Card`, `Backdrop`, `Column`, `Heading`,
`Subheading`. The designer edits the same class, and what you change there is written back into the file (see
[Write-back](../language/write-back.md)); the other way round, saving the file in an outside editor recompiles the loaded
classes built from it.

### Show it

In a Blueprint — the level Blueprint's BeginPlay, for instance — a **Create Dream Widget** node with `WBP_HelloDreamGUI`
as its class, its return value wired into **Add to Viewport**, as with UMG's Create Widget. Once the class is picked, the
node has a pin for each of its Expose on Spawn properties (every `props` and `viewmodels` entry of a `.dui` is one) and
an Owning Player pin; the pins are assigned **before** the widget initializes, so On Initialized and every binding's
first value already see them. Add to Viewport's ZOrder orders it against other screen pages, and **Remove from Parent**
takes it off again.

From C++ one call does it, with a dependency on the `DreamGUI` module:

```cpp
#include "DreamUIBPLibrary.h"

// HelloClass is a TSubclassOf<UDreamUserWidget>, e.g. a UPROPERTY set to WBP_HelloDreamGUI.
UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, HelloClass);
```

It returns the root it put on the screen; the optional third argument, `SortOrder`, orders it against other screen
pages. `UDreamUIBPLibrary::RemoveFromViewport` takes it off again.

## Reading the file

Here is the sample without its header comment and one divider line — still a complete file that compiles:

```dui
class /Game/UI/WBP_HelloDreamGUI

style Title {
    Font     = /DreamGUI/DefaultFont_DistanceField
    FontSize = 28
    Color    = #E6E9F0
    HAlign   = Center
}

style Body {
    Font     = /DreamGUI/DefaultFont_DistanceField
    FontSize = 15
    Color    = #8C93A6
    HAlign   = Center
}

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)

    + Overlay {}

    Widget Card {
        AnchorData.SizeDelta = (420, 220)
        @slot HorizontalAlignment = Center
        @slot VerticalAlignment   = Center

        + Overlay {}

        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            @slot HorizontalAlignment = Fill
            @slot VerticalAlignment   = Fill
        }

        Widget Column {
            @slot HorizontalAlignment = Fill
            @slot VerticalAlignment   = Fill

            + VerticalBox {
                Spacing = 10
                Padding = (24, 28, 24, 28)
            }

            Text Heading : Title {
                AnchorData.SizeDelta = (0, 34)
                Text = "Hello, DreamGUI"
                WrapTextAt = 372
                @slot HorizontalAlignment = Fill
                @slot SizeRule = Auto
            }

            Text Subheading : Body {
                AnchorData.SizeDelta = (0, 46)
                Text = "This screen is a text file. Edit it, compile, and the class changes with it."
                WrapTextAt = 360
                @slot HorizontalAlignment = Fill
                @slot SizeRule = Auto
            }
        }
    }
}
```

### `class`

Which widget Blueprint this file is the source of; at most one per file. Its second job is the localization
namespace: every localized string in the file is keyed under this path, so the keys stay put when the file is renamed.
Without the line, the file's own name is the namespace.

### `style`

A named set of property lines, put on a node with `Node : StyleName` (`Text Heading : Title`). A node's own lines win
over its style's. Order is free: a style may be declared below the node that wears it.

### `Widget`

A node with no visual of its own — a rect that exists to hold others. A node is a type, an id and an optional block.
The id (`Root`, `Card`, ...) is the widget's identity: its name, the member variable a Blueprint graph reads, the key a
binding resolves through, and the localization key. It is a C++ identifier, unique in the file regardless of case
([DUI3001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3001)).

### `+ Overlay {}`

A component attached to the node. Layout lives in components (`Overlay`, `VerticalBox`), not in the widget's type —
which is why a node can be laid out one way and drawn another. A node has one layout container. An Overlay stacks its
children in declaration order, so `Backdrop`, written first, is drawn behind.

### `Image` / `Text`

A node whose visual draws something. A property name can be a dotted path into a struct property (`Brush.TintColor`);
colours are sRGB hex with 3, 4, 6 or 8 digits; what struct a tuple like `(24, 28, 24, 28)` is depends on the property.

### `@slot` versus `AnchorData.SizeDelta`

The one distinction in the file worth reading twice:

- `AnchorData.*` is the **node's own rect**. `Root`, anchored at (0, 0)–(1, 1) with a `SizeDelta` of (0, 0), means
  "pinned to all four edges of the parent, no inset": it fills whatever it is added to, at any resolution. `Card`'s
  `SizeDelta` of (420, 220) is its size.
- `@slot …` is **what the node asks of the panel above it** — the parent's arrangement of it: `HorizontalAlignment`,
  `VerticalAlignment`, `SizeRule`. A slot line on a node whose parent lays out no panel is
  [DUI5003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5003).

The `WrapTextAt` on the two texts follows from the same distinction. A text breaks its lines at `WrapTextAt` when it
has one and at its own width when it does not — and a column asks how tall a child wants to be **before** it gives the
child a width. So a text in a Fill slot names a width to break at: here the widest the card is meant to be, 420, less
the column's 24 on each side, which is 372. Without it the heading is measured at its own width, 0, as one character per
line.

**Note:**

  The same tree has a shorter spelling: a layout container can be the node's type (`Overlay Card`,
  `VerticalBox Column`), slot lines can share an `@slot { … }` block, and `@fill` stands for `@slot SizeRule = Fill`.
  The sample uses the long form to keep "component" and "slot" apart while you learn them. The whole language starts at
  the [.dui overview](../language/overview.md).

Here is the card and the heading in the short form (the heading only):

```dui
class /Game/UI/WBP_HelloDreamGUI

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay {}

    Overlay Card {
        AnchorData.SizeDelta = (420, 220)
        @slot { HorizontalAlignment = Center  VerticalAlignment = Center }

        Image Backdrop {
            Brush.TintColor = #1B1E26FF
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
        }

        VerticalBox Column {
            Spacing = 10
            Padding = (24, 28, 24, 28)
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

            Text Heading {
                AnchorData.SizeDelta = (0, 34)
                Text       = "Hello, DreamGUI"
                FontSize   = 28
                HAlign     = Center
                WrapTextAt = 372
                @slot { HorizontalAlignment = Fill  SizeRule = Auto }
            }
        }
    }
}
```

## The same class, in the world

The same class can be a surface in the level instead of a layer on the screen. Drag `WBP_HelloDreamGUI` from the
Content Browser into a level, or place a **DreamUI World Widget Actor** from the *Place Actors* panel: either way you get
an `ADreamWorldWidgetActor`, whose entire content is a single `UDreamWorldWidgetComponent`. Move it, rotate it and attach
it like any other scene component.

The component hosts the tree; it does not redefine it. The Blueprint's own root canvas remains the truth about how the
UI is built; the component writes render mode, sort order and trace channel down onto it and leaves the rest alone.

| Property | What it does |
| --- | --- |
| `WidgetClass` | The widget Blueprint class to load. Changing it is the one edit that rebuilds the tree |
| `Backend` | `DreamUIRenderer`: DreamGUI's own renderer — flat, unlit, untouched by post process, exact draw order. `UERenderer`: the engine's pipeline — lit, shadowed, fogged and occluded like any mesh in the level, sorted by translucency rules |
| `bUseDesignSize` / `DrawSize` | The size the tree lands at. On by default, following the size the Blueprint was designed at; off, `DrawSize` (world units, 1920 × 1080 by default) |
| `Pivot` | Where the actor's origin sits within that rectangle; (0.5, 0.5) centres it |
| `SortOrder` | Order among world-space canvases |
| `TraceChannel` | The channel this canvas answers on, Visibility by default. A raycaster only sees canvases whose channel matches its own |

The properties are live: changing the size, the pivot, the backend or the sort order re-applies to the tree already
loaded.

**Interaction needs no setup.** On `BeginPlay` the component asks for an event system and a
`UDreamWorldSpaceRaycaster` for each local player and supplies whichever is missing, so pressing Play is enough to click
a button hanging in the world. The raycaster points from the cursor or from the middle of the screen (`PointerSource`:
`Mouse` or `ScreenCenter`). `bOccludeByWorld`, on by default, makes solid geometry block a click the way it blocks a
line trace; to click through walls, untick it on a raycaster of your own, or call `SetOccludeByWorld(false)` on the
player's. Put a raycaster of your own with the same user index on any actor and nothing is added on top of it — the test
is for one that exists, not for one this plugin made.

From code it is `UDreamUIBPLibrary::ConstructWidget`, then `UDreamUIBPLibrary::AttachWidgetToSceneComponent`, which
attaches the root to a scene component; the root must carry a `DreamCanvas`. `ADreamWorldWidgetActor` can be possessed
by a Level Sequence directly, and `WidgetOpacity`, `WidgetOffset` and `bWidgetVisible` on the component are keyable from
it.

## Next

  - [Workflow](workflow.md) — Write, compile, place, interact, animate, debug — the shape of a day.
  - [World space](../concepts/world-space.md) — World-space canvases, the two backends and raycasting in detail.
  - [.dui overview](../language/overview.md) — File structure, nodes, values, and the rest of the language.
