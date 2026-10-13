---
title: "Nodes and values"
description: "How a node is written, the rules an id keeps, nodes with no id, the order node types resolve in, and every kind of value a property takes."
---

## Nodes and ids

A node is a type, an id and an optional block:

```
Text Title {
    Text = "Settings"
}
```

The block holds, in any order: properties, `@slot` lines, `+ Component` blocks, child nodes, and the control
statements `if` and `for` / `each`. A node with nothing to say needs no block: `Image Divider` alone on a line is a
node.

The **id** is the node's identity. It names the widget, the class member variable a Blueprint graph reads, the key a
binding resolves through, and the localization key of the node's strings. So:

- It is a C++ identifier: letters, digits and `_`, not starting with a digit (DUI3002). Characters outside ASCII are
  letters, so an id may be Chinese.
- It is unique in the file, compared without regard to case (DUI3001) -- the variable it becomes is an `FName`, which
  ignores case.
- It is not a keyword: `class`, `style`, `resources`, `slot`, `for`, `each`, `in`, `was`, `use`, `timeline`,
  `external`, `ease`.

Two clauses may follow the id, in either order:

- `: StyleName` -- the node wears a style. See [Styles](styles-and-resources.md#styles).
- `(was: OldId)` -- the node was renamed. The next compile moves what pointed at the old id -- graph references,
  bindings, animation tracks -- to the new one. Keep the clause until the file has been compiled once; a second rename
  keeps the first old id. DUI3010 to DUI3013 are the ways a rename can be refused.

### Nodes with no id

A node nothing refers to by name may leave the id out, as long as a block or a style clause follows the type:

```
HorizontalBox {
    Spacing = 14
    Text { Text = "Status" }
    Text : Caption { Text = "Ready" }
}
```

The parser gives such a node an id of its own: `<parent id>__<type><n>` -- the id of the nearest enclosing node that
has one (or `Root`), two underscores, the type with every character an id cannot hold made `_`, and the count of earlier
unnamed siblings of that type. The first unnamed `Text` under `Root__HorizontalBox0` is `Root__HorizontalBox0__Text0`.
The same text always gives the same ids, so they are as stable as written ones while the file keeps its shape; adding an
unnamed sibling of the same type before one renumbers the ones after it.

An unnamed node still has a class member variable -- the run time finds a binding's widget through it -- but a hidden
one: Blueprint graphs cannot see it. Give a node an id when code needs it. An unnamed node cannot carry `(was: …)`
(DUI2004): there is nothing to rename from.

A type alone on a line, with no id, no block and no style (`Text`), is still an error (DUI2004): it is as likely a
property whose `=` went missing.


## Node types

What a node's type means is decided in this order:

1. **A built-in tag**: `Widget` (a rect with no visual, the ordinary container), or the tag of a visual -- `Text`,
   `Image`, `RectBlock`, `Sprite`, `Texture`, `Ring`, `Polygon`, `PolygonLine`, `Line2DRaw`, `Line2DChildren`,
   `Empty`, `StaticMesh`, `BackgroundBlur`, `BackgroundPixelate`, `PixelSort`, `PostProcessRenderElement`,
   `PostProcessRenderElementText`, `CanvasRenderTargetPreviewer`, `UMGWidget`. A plugin adds a visual with
   `DECLARE_DREAM_GUI_VISUAL`.
2. **A layout container**: `VerticalBox`, `HorizontalBox`, `StackBox`, `Overlay`, `CanvasPanel`, `GridPanel`,
   `UniformGridPanel`, `WrapBox`, `SizeBox`, `ScaleBox`, `SafeZone`, `ScrollBox`, `WidgetSwitcher`, `Border`,
   `MenuAnchor`. The node is a plain widget carrying that container, and its own lines set the container's
   properties as well as the widget's:

   ```
   VerticalBox Categories : Lists {
       Spacing = 29
       Row Audio { Label = "Audio" }
   }
   ```

   is the same tree as `Widget Categories : Lists { + VerticalBox { Spacing = 29 } … }`. A name the widget and the
   container both have means the widget's. A container-typed node takes no second container: `+ HorizontalBox` on it is
   DUI5022.
3. **A component alias**: a name given by `use … as` -- `Row`, or `nier.Row` for one a namespace brought. See
   [`use`](use.md#use).
4. **`@Name`**: the widget class an `Asset` entry of a `resources` block names. The older way to name a component by
   a short name; `use … as` is the one to reach for.
5. **A registry tag**: `Scope.Name`, a widget class registered with `DECLARE_DREAM_GUI_WIDGET`. The plugin's controls
   are under `Native`: `Native.Button`, `Native.Toggle`, `Native.Slider`, `Native.SpinBox`, `Native.ProgressBar`,
   `Native.Dropdown`, `Native.TextInput`, `Native.EditableText`, `Native.MultiLineEditableText`, `Native.RichText`,
   `Native.ScrollBox`, `Native.ScrollBar`, `Native.List`, `Native.TileView`, `Native.TreeView`, `Native.TabView`,
   `Native.ExpandableArea`, `Native.Border`, `Native.Dialog`, `Native.MenuAnchor`, `Native.RingMenu`,
   `Native.RadioButton`, `Native.InputKeySelector`, `Native.Throbber`, `Native.NativeWidgetHost`.
6. **An asset path**: `/Game/UI/WBP_Row`, or `/Script/MyGame.MyWidget` for a native class.

A node typed by a class (3 to 6) is an *instance* of a component: its contents come from that class, and its lines set
that class's properties. The class must be a concrete DreamUI user widget (DUI5006).

Because tags and containers are asked first, an alias can never change what `Text` or `VerticalBox` means; an alias
that tries is refused (DUI3018).

`DUI/.dui-symbols.json`, which the editor writes on start and on `DreamUI.ExportSymbols`, lists every tag, container
and registered widget with its properties, for an editor's completion.


## Properties and values

```
Name = Value
AnchorData.SizeDelta = (400, 240)
```

A name is a reflected property, or a dotted path into a struct property. A bare name is looked for on the widget
first, then on its visual, then -- for a container-typed node -- on its container, then on its behaviours; the first
that has it takes it. A name nothing has is DUI4001, with the nearest match suggested.

When a block names a property twice, the later line wins. A node's own lines win over its style's.

### Values

| Kind | Examples | Notes |
|---|---|---|
| Number | `24`, `0.95`, `-3`, `1e-45` | Read against the property's numeric type. |
| String | `"Settings"`, `"a \"quote\""` | Escapes: `\"`, `\\`, `\n`, `\t`, `\r`. On an `FText` the string is localized. |
| Colour | `#FFF`, `#FFFF`, `#1E1E1E`, `#1E1E1EFF` | 3, 4, 6 or 8 hex digits. sRGB. |
| Tuple | `(400, 240)`, `(0, 8, 0, 0)` | A vector, a margin, a rotator -- whatever struct the property is. May span lines. |
| Word | `Left`, `Collapsed`, `true`, `None` | An enum value, a bool, or `None` for an empty reference. |
| Asset path | `/Game/UI/T_Icon`, `"/Game/UI/T_Icon.T_Icon"` | An object or class reference. Quoted works too. |
| Resource | `@Accent` | The value of a `resources` entry. See [Resources](styles-and-resources.md#resources). |
| Node id | `CheckMark` | On a property that holds a widget, a visual or a behaviour: the node of that id in this file. |

A flags enum has no `|`; write a combination as the number its flags add up to.

A string can carry the key it is localized under: `Text = "OK" @key("Dialog.Confirm")`. Without one the key is
`<node id>.<property>` -- with the component's class and position between the two for a line of a `+` block -- in the
namespace the `class` line gives.
