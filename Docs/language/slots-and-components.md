---
title: "Slot lines and components"
description: "Telling the parent panel where a node goes with @slot and @fill, and attaching behaviours, layout containers and layout-self objects with + Class."
---

## Slot lines: `@slot` and `@fill`

A widget inside a panel -- a parent whose container lays children out (a box, a grid, an overlay) -- has a **panel
slot**: how the parent places it. `@slot` lines set it:

```
Text Label {
    @slot HorizontalAlignment = Fill
    @slot Padding = (0, 4, 0, 4)
}
```

Several at once go in a block, with the property syntax of a `+` block:

```
@slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }
```

And the two lines a box layout is mostly made of have a shorthand:

| Shorthand | Stands for |
|---|---|
| `@fill` | `@slot SizeRule = Fill` |
| `@fill 2` | `@slot SizeRule = Fill` and `@slot FillWeight = 2` |

`@fill` stands alone on its line, or with its weight and nothing else. A slot line on a node whose parent lays out no
panel is DUI5003.


## Components: `+ Class`

`+ Class { … }` attaches a behaviour, a layout container or a layout-self object to the node, and its block sets that
object's properties:

```
Widget Confirm {
    + UIButton { TransitionType = None }
    + VerticalBox { Spacing = 10  Padding = (24, 28, 24, 28) }
}
```

The class is found by its name with the plugin's prefixes tried: `VerticalBox` finds the vertical box container,
`UIButton` the button behaviour, `Button` too. A full `/Script/Module.Class` path works as well. A name that resolves to
nothing a widget can carry is DUI3006.

A node has one layout container. `+ VerticalBox` on a plain `Widget` and a container type are two ways to write the
same thing; the type is shorter.
