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

The block holds, in any order: properties, `@slot` lines, `@tag` lines, `+ Component` blocks, child nodes, and the
control statements `if` and `for` / `each`. A node with nothing to say needs no block: `Image Divider` alone on a line is a
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

- `: StyleName` -- the node wears a style, or several, `: A : B`. See [Styles](styles-and-resources.md#styles).
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


## Tags: `@tag`

A tag is a name a widget carries so that code can find it by what it is rather than by its id -- every trim, every
glow -- as an actor's tags do. A `@tag` line gives a node one name or several:

```
Image TrimLeft {
    @tag Trim
    @tag Glow, UI.Accent
}
```

- **A name** is an identifier, case kept. A `.` may join identifiers (`UI.Accent`), and characters outside ASCII are
  letters, so a tag may be Chinese. Names are separated by commas, and the line ends at the line break, a `;` or the
  closing `}`. `tag` is a keyword only directly after `@` and only in lower case; it stays a usable id and property name.
- **Where.** On any widget node: the block of a `slot` declaration, the block of a `rows` line and the widget a `for` or
  an `each` repeats included -- every copy carries the tags. A `style` cannot hold one, and neither can the body of a
  `for` / `each` directly (put the line on the widget it repeats): both are DUI2024. A branch of an `if` holds widgets
  only, so a tag line directly in one is DUI2018, and a slot fill that holds widgets cannot hold a tag line either
  (DUI2019): tag the widgets.
- **A line that does not read** -- nothing after `@tag`, a number or a string, two names with no comma between them, a
  trailing comma -- is DUI2024, reported at the `@`.
- **A name said twice is one tag**, on one line or on two.
- **They land in `Tags`**, the `TArray<FName>` every widget has. A `Tags = [Trim, Glow]` line on the same node works
  too, and the two add up.

`@tag` is also how a node typed by an `Asset` resource is written (`@Name Id { }`, see [Node types](#node-types)),
so a resource that is called `tag` has to be told apart. A node, not a tag line, is `@tag` followed by a name and then
`{`, `:` or `(was: …)` -- `@tag Trim { }`, `@tag Trim : Style` -- or directly by `{` or `.`: `@tag { }` and
`@tag.Something`. Everything else after `@tag` is a tag line.

### Finding widgets by tag

`UDreamWidget` has `Tags`, `HasTag(FName)`, `AddTag(FName)` and `RemoveTag(FName)`, all of them callable from Blueprint.
A user widget finds the tagged widgets of its own contents with `GetWidgetsByTag` (a pure Blueprint function), or from
C++ with `ForEachWidgetWithTag`, which takes a visitor and makes no array:

```cpp
for (UDreamWidget* Trim : GetWidgetsByTag(TEXT("Trim")))
{
    Trim->SetRenderOpacity(Opacity);
}
```

- It searches the widget's **own** contents, parents before children, siblings in order. It does not look inside a
  nested component's own contents -- those answer to that component's own `GetWidgetsByTag` -- but the component
  instance itself is searched, and so is whatever this widget put into the instance's slots.
- Content a host put into **this** widget's slots belongs to the host and is not found.
- It is empty before the widget is initialized, and for `None`.

Nothing in the library reads `Tags`; they are for the code that asks.


## Node types

What a node's type means is decided in this order:

1. **A built-in tag**: `Widget` (a rect with no visual, the ordinary container), or the tag of a visual -- `Text`,
   `Image`, `RectBlock`, `Sprite`, `Texture`, `Ring`, `Polygon`, `PolygonLine`, `Shape`, `Line2DRaw`, `Line2DChildren`,
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
   a short name; `use … as` is the one to reach for. (`@tag` followed by a name is a [tag line](#tags-tag), unless a
   block or a style follows.)
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

A property is found on the object that has it, and a plain `Widget` node has only the widget: `bRaycastTarget` belongs to a
visual (an `Image`, a `RectBlock`, an `Empty`), so on a `Widget` node it is an error (DUI5002 names a node type that has it).
What a widget itself has is `Raycastable` -- `Inherit`, `Enabled` or `Disabled` -- and `Raycastable = Disabled` is how to
take a widget, and what inherits from it, out of hit testing.

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
| List | `[1, 2, 3]`, `["A", "B"]`, `[(0, 0), (8, 8)]`, `[]` | An array or a set: each element a value of the element type, node ids included in an array. A set keeps no order. May span lines; a trailing comma is allowed. |
| Map | `{ Hover: #FFCC00, Pressed: #AA8800 }`, `{}` | A map: `key: value` entries, each side a value of its type. A name key that is one word may be written bare. |
| Struct | `{ Distance: 12, Angle: 45 }`, `{ "Lamp", Tag: Lamp }` | A struct that has no short form: `field: value` entries, the field named as a property is. See [Structs as field maps](#structs-as-field-maps). |
| Contour | `Gaussian`, `"custom(0 0, 0.5 1 corner, 1 0)"` | A layer style's contour: a named shape's word, or a custom curve's points -- each its input and output, `corner` after a point the curve bends at. |

A colour literal is sRGB-encoded, as in CSS: `#8C8C94` is the bytes 140, 140, 148. On an `FColor` property (a visual's
`Color`, a brush's `TintColor`) it is stored as written. On an `FLinearColor` it is decoded to linear first, so the same
literal comes out at about 0.26 in its first two channels, not 0.55. Where colours are multiplied the arithmetic is not the
same everywhere: a widget's content tint and colour multiplier are applied to a visual's `Color` in linear space (decode,
multiply, encode again), while an image's `Brush.TintColor` is multiplied into its colour channel by channel on the encoded
bytes. A mid-grey tint in linear space darkens more than its look suggests: `#808080` is 0.22 linear, and the tint that
halves the light is `#BCBCBC`.

A flags enum has no `|`; write a combination as the number its flags add up to.

### Positions, sizes and axes

A node outside a box is placed by its `AnchorData`, the way a Unity RectTransform is, and **Y goes up**:

- `AnchorMin`, `AnchorMax`: points of the parent, 0 to 1 on each axis, `(0, 0)` its **bottom-left** and `(1, 1)` its
  top-right. Equal, they hold the node at that point; different, they stretch it with the parent.
- `Pivot`: the node's own point, 0 to 1 the same way, `(0.5, 0.5)` its centre -- what `AnchoredPosition` places.
- `AnchoredPosition`: from the anchors to the pivot, in pixels; a positive Y moves the node **up**.
- `SizeDelta`: width and height in pixels on an axis whose anchors meet; on an axis where they stretch, how much larger
  (or, negative, smaller) than the span between them. `AnchorMin = (0, 0)`, `AnchorMax = (1, 1)`, `SizeDelta = (0, 0)`
  fills the parent.

A node inside a box (`VerticalBox`, `HorizontalBox`, a grid) is placed by the box instead, through its `@slot` lines
([Slot lines](slots-and-components.md#slot-lines-slot-and-fill)): a vertical box gives a child the width it measures,
not the box's, unless the child's `@slot HorizontalAlignment = Fill`.

The render transform moves where a node is drawn and leaves layout alone:

- `RenderTranslation = (X, Y, Z)`, in pixels: **X is depth** -- negative brings the node toward the viewer and draws it
  larger, positive pushes it back -- Y is right and Z is up. `(0, -40, 0)` is 40 px to the left; `(-40, 0, 0)` is 40
  nearer.
- `RenderRotation = (Pitch, Yaw, Roll)`, in degrees, about `RenderTransformPivot`: Yaw turns the node about its vertical
  axis (positive brings its right edge nearer), Pitch about the horizontal one, Roll in the screen's plane (positive is
  clockwise).
- `RenderScale = (X, Y, Z)` scales it as drawn.

A list on a property that is not an array or a set, a map on one that is not a map, or a set or map of nodes, is
DUI4009; a list or a map that does not read (a bracket never closed, a map entry with no `:`, other than the first of a struct's, which may go without a key) is DUI2023.

A string can carry the key it is localized under: `Text = "OK" @key("Dialog.Confirm")`. Without one the key is
`<node id>.<property>` -- with the component's class and position between the two for a line of a `+` block -- in the
namespace the `class` line gives. A file without a `class` line uses its path under its `DUI/` folder
(`Panels/Settings.dui`, `Plugin.X:Panels/Settings.dui`), the same on every machine.

Three more annotations say what a translator needs, after the string and in any order:

```
Text = "应用 {Count} 项更改" @note("Footer button; applies at once") @max(16)
Text = "v1.0.3" @noloc
```

- `@note("…")` -- what the text is for. It reaches the localization tools with the text (and the engine's gather, as
  the text's developer notes).
- `@max(n)` -- the longest a translation may be, in display width: a full-width character counts 2.
- `@noloc` -- the same text in every language, a version number or a name: it is never translated and no tool lists
  it. It takes none of the other three.

Each is written once; any of them on a value that is not a string, or written wrong, is DUI2022.


### Structs as field maps

A struct with no short form of its own -- not a vector, a colour or a margin, which the tuple and colour literals
cover -- is written as a map of its fields, wherever a value of that struct goes: on a struct property, or as an element
of an array, a set or a map.

```
Style.DropShadows = [{ Distance: 12, Angle: 45 }, { Distance: 4 }]
```

- Each key is a field name, resolved as a property name is, and each value is read against the field's type, so a field
  that is itself a text, a colour or a list is written as one. A field not written keeps the struct's default. A name the
  struct has no field for is DUI4002.
- Text fields are localized like any other string, under the key of their property joined with the field's name
  (`Items.Label`), so a string inside a struct reaches the translators.
- **A struct with a primary field** takes it as the map's first entry with no key: `{ "Lamp", Tag: Lamp }` is
  `{ Label: "Lamp", Tag: Lamp }`. A struct names its primary field with the `DreamGUI_PrimaryField` meta; today
  `FDreamRingMenuItem` does, and its primary field is `Label`. Only the first entry may go without a key; the same on a
  map that is really a `TMap`, or on a struct with no primary field, is DUI4009.
- The older form, an element written as the engine's own text for the struct inside a string --
  `["(Distance=12)"]` -- still reads.

A ring menu's items are the case this was made for:

```
Native.RingMenu Wheel {
    Items = [
        { "Lamp", Tag: Lamp },
        { "Door", Tag: Door, bEnabled: false, Weight: 2 },
        { Label: "Window", Tag: Window },
    ]
}
```

What is written back is narrower than what is read. The designer and the Blueprint converter print a map only for the
structs that name a primary field; every other struct keeps the quoted form on the way out, since a field such as a
linear colour would be quantized through a hex code. See
[Designer write-back](write-back.md#what-the-designer-writes-back).
