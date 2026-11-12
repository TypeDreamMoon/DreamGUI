# The .dui language

> This page stands on its own: what an author needs is here, and an agent that cannot open its links loses nothing it
> needs. The links lead to the online manual for depth. Look names up (DreamMCP's `lookup`, or
> `DUI/.dui-symbols.json`) instead of guessing them. Built from `Docs/language/*.md` by `.skill/build-reference.ps1`.

A `.dui` file is the source of a DreamGUI widget Blueprint: a tree of widgets, their properties, their bindings and their
animations, as text. The Blueprint names the file as its *Source File*, compiling the Blueprint reads the file, and the
designer writes its edits back into the file. This page is the reference for writing one by hand.

## Where a file lives

Under a `DUI/` directory: the project's (`<Project>/DUI/`) or an enabled plugin's (`<Plugin>/DUI/`). Not under
`Content/` -- a `.dui` is source, not an asset.

A path to another file -- in a Blueprint's Source File, or after `use` -- is written one of three ways:

| Spelling | Means |
|---|---|
| `Panels/Settings.dui` | Relative to a `DUI/` root: the project's first, then each plugin's, the first that has the file. |
| `Plugin.MyPlugin:Panels/Settings.dui` | That plugin's `DUI/` directory. |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | Absolute, used as written. |


## File structure

A file is a list of statements. A statement ends at the end of its line or at a `;`. Comments are `// …` to the end of
the line and `/* … */`.

At the top level a file holds:

| Statement | Purpose |
|---|---|
| `class /Game/UI/WBP_Settings` | The Blueprint this file is the source of. At most one. |
| `use …` | Another file or class made available to this one. See [`use`](#use). |
| `resources { … }` | Named constants. See [Resources](#resources). |
| `props { … }` | Properties this file's class declares. See [Writing a component](#writing-a-component). |
| `events { … }` | Events this file's class raises. |
| `viewmodels { … }` | The view models this file's class holds. See [View models](#view-models). |
| `style Name { … }` | A named set of lines. See [Styles](#styles). |
| `timeline Name { … }` | An animation. See [Timelines](#timelines). |
| one node | The root of the tree. |

The order of these is free: a style may be declared below the node that wears it. A file compiled into a class has
exactly one root node (DUI2006). A file with no root is a *library*: it can be `use`d, and it cannot be compiled.

The `class` line does two jobs. It says which Blueprint the file is for (a compile into a different one warns,
DUI6003), and its path is the namespace of every localized string in the file, which keeps the keys stable when the
file is renamed. Without one the file's own name is the namespace.

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

- `: StyleName` -- the node wears a style, or several, `: A : B`. See [Styles](#styles).
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
   [`use`](#use).
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
| Resource | `@Accent` | The value of a `resources` entry. See [Resources](#resources). |
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
[Designer write-back](#what-the-designer-writes-back).

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

### `@slot Size`

A box that is exactly one size is the other common slot line, and it has a shorthand too:

```
@slot Size = (120, 48)
@slot { Size = (120, 48)  Padding = (4, 0, 4, 0) }
```

`Size` stands for `@slot MinDesiredSize` and `@slot MaxDesiredSize` given the same value, so the slot is held at that
size on both axes. It reads in a `@slot { … }` block and in a style's slot lines as it does on its own line. Only `=`
gives it a value: `@slot Size <- Function()` is DUI4001, whose message names the two properties to bind separately,
`MinDesiredSize` and `MaxDesiredSize`.


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

## Styles

A style is a named set of lines, applied to a node by `: Name`:

```
style Caption {
    FontSize = 14
    Color    = #8C93A6
}

style Warning : Caption {
    Color = #E05A47
}

Text Note : Warning { Text = "Unsaved changes" }
```

- A style may inherit another (`style Warning : Caption`): the base's lines apply first, then the derived style's,
  then the node's own. A base that comes back around is DUI3015. A style may name several bases, and so may a node; see
  [Several styles](#several-styles).
- A style may carry `+ Component` blocks and slot lines, so that a kind of column is one name:

  ```
  style RowColumn {
      + VerticalBox { Spacing = 15 }
      @fill
  }

  Widget Left : RowColumn { … }
  ```

  The style's components come first. A component of the same class the node also writes is one object, the style's
  values first and the node's after; the node's own `@slot` lines win over the style's.
- A local style shadows an imported one of the same name. Two local styles of one name are DUI3005.
- **A control's `Style` is not one of these.** A `Native.Button` and its kin have a property called `Style`, a struct of
  the control's own, written `Style.TickChecked = #FF3355` and chosen by `StyleSource`. Whether such a line takes effect,
  and what the `bOverride_` bits beside the fields do, is on [Styles and style sheets](https://gui.toolchain.64hz.cn/en/docs/controls/styles/#in-dui-and-at-run-time).


### Several styles

A style, a node and a `rows` header each take any number of styles, every one behind its own colon:

```
style Danger : Button : Loud { Color = #FF4040 }
Text Title : Heading : Glow { }
rows Row : ListRow : Dense (Label, Value) { … }
```

- **Order.** The names apply from left to right, and each name first applies its own bases, recursively, so a base comes
  before the style that inherits it. A style applies once, at the place it first appears. A later style overrides an
  earlier one, and the node's own lines come last. With `style A : Base` and `style B : Base`, a node written `: A : B`
  gets Base, A, B.
- An unknown name is DUI3004, once for each name that is unknown. A style that comes back around to itself through its
  bases is DUI3015.
- **Naming a style that is already applied** changes nothing, and is DUI3025, a warning. It is raised on the later name,
  whether an earlier one applied it as a base (`: A : Base`, where `A` inherits `Base`) or the name is simply written
  twice (`: A : A`). Each clause is checked once -- a node's or a `rows` header's, and a style declaration's, however
  many nodes wear it -- and every redundant name in it is reported. A style imported from another file is never
  warned about here; the file it came from is, when it compiles. The messages say which case it is:
  `style 'X' is already applied through 'Y', so naming it again changes nothing`, and
  `style 'A' is named twice in this clause, so the second changes nothing`.

## Resources

```
resources {
    Color   Accent = #FF6600
    Number  Gap    = 8
    Vector2 Icon   = (24, 24)
    String  Brand  = "Dream"
    Asset   Row    = /Game/UI/WBP_Row
}
```

`@Accent` then stands for `#FF6600` wherever a value goes, and in a binding expression. The type is checked against the
entry's own value (DUI4008); a missing entry is DUI4007. Each entry also becomes a variable of the compiled class, so a
graph can read it.

An `Asset` entry naming a widget class can be a node type: `@Row Audio { … }`. `use … as` does the same with less.

Entries accumulate over several `resources` blocks; one name declared twice is DUI3014.

## `use`

`use` makes another file, or a class, available to this one. Four forms:

```
use "UI/Library.dui"                     // a library, merged
use "UI/Components/Row.dui" as Row       // a component, by a short name
use /Game/UI/WBP_Slider as Slider        // a class with no .dui, by a short name
use "UI/NieR_Common.dui" as nier         // a library, under a namespace
```

**Merged.** A plain `use` of a file brings in its styles, its resources and the component aliases it declares. The
file's own `use` lines come along, so libraries layer. A name this file declares shadows an imported one.

**A component.** `use "…" as Row` on a file that has a root node names that file's class, and takes nothing else from
it: a screen does not borrow a component's styles. The class is the one the file's `class` line names; for a file with
no `class` line, the editor finds the Blueprint whose Source File it is (without the editor -- a commandlet -- that is
DUI5018, and a `class` line is the fix). Then `Row` is a node type:

```
Row Audio { Label = "Audio" }
```

**A class.** `use /Game/UI/WBP_Slider as Slider` names a class that has no `.dui` behind it. The `as` is required here.

**A namespace.** `use "…" as nier` on a file with no root is a namespace: everything the library declares or brought in
is entered as `nier.X`. Its styles are `: nier.Label`, its resources `@nier.Ink`, its component aliases `nier.Row`. Two
libraries that both say `Label` can then be used side by side. A style that came in under a namespace still finds its
own library's names: `Color = @Ink` written in the library means the library's `Ink`.

**Re-export.** A library's own `use … as Row` lines travel with it. A library that names a family of components gives
every screen that uses it those names -- `Row` after a plain `use`, `nier.Row` after `use … as nier`:

```
// UI/NieR_Common.dui
use "UI/Components/NieR_Row.dui" as Row
use "UI/Components/NieR_Tab.dui" as Tab

// UI/NieR_Menu.dui
use "UI/NieR_Common.dui"
Widget Root {
    Row Item_0 : ListRow { Label = "Warped Wire" }
}
```

The name after `as` is one word, not a keyword (DUI2015), not the name of a tag or a container (DUI3018), and given
once per file (DUI3017). A `ns.` no `use … as ns` declares is DUI3021. An import that cannot be read, does not parse,
or comes back around to a file already being imported is DUI2012.

## Writing a component

A component is a widget Blueprint used inside other trees. Written in `.dui` it declares, beside its tree, what its
hosts may set and hear.

### `props`

```
props {
    Text   Label
    Text   Value
    Number ValueIndex = 0
    Enum   /Script/MyGame.ERowKind Kind = Cycle
}
```

One property per line: a type, a name, and optionally `= default`. The types are `Text`, `String`, `Number`, `Integer`,
`Bool`, `Color`, `Vector2`, `Asset`, `Class`, and `Enum` followed by the enum's path. Each becomes a Blueprint variable
of the class, editable on instances; a C++ parent's property of the same name and type is used instead of declaring one.

A host sets them like any property, and the component binds them:

```
// in the component
Text LabelText { Text <- Label }

// in a host
Row Audio { Label = "Audio" }
Row Track { Label <- GetTrackName() }
```

A host may bind a prop as well as set it. A prop has no setter -- it is a Blueprint variable -- so the binding writes it
straight into the instance when the value changes and announces it (props are FieldNotify), and the component's own
bindings on it show the change. This is the one setter-less property a binding accepts: a user widget's own variable,
one way (`<-`, not `<->`).

A line that is not `Type Name` or `Type Name = value` is DUI2016; one name twice is DUI3019; at compile, a type with no
Blueprint pin is DUI6008, a name the class already uses DUI6009, a default its type cannot hold DUI6013.

### `events` and `emit`

```
events {
    Picked(Number Index)
    Closed
}
```

Entries end at a line break or a `;`, so a short list fits on one line: `events { Picked(Number Index); Closed }`. Each
becomes an event dispatcher of the class. The component raises one from any route with `emit`, its arguments written as
binding expressions:

```
+ UIButton { OnClick -> emit Picked(ValueIndex) }
```

and a host routes it like any event:

```
Row Audio { Picked -> HandleAudioPicked }
```

DUI2017 is a malformed entry and DUI3020 a duplicate. At compile, `emit` of an event not declared here is DUI6010,
arguments that do not match its parameters DUI6011, an `emit` that cannot be compiled where it stands (inside a loop body,
or on an event whose parameters Blueprints cannot take) DUI6012, and an event named like another member of the class
DUI6014.

### Slots

A component opens a hole its host fills with `slot`:

```
slot Footer
slot Rows default
slot Detail : DetailPanel {
    + VerticalBox { Spacing = 15 }
    @slot SizeRule = Fill
}
```

- `default` marks the slot that content goes to when the host names none. One per file (DUI3022).
- A block, or a style, lays the hole out: components, properties, slot lines and `@tag` lines -- never children, since
  what goes in it is the host's (DUI2019). A slot with a layout container takes several widgets; one without takes one.
- `(was: OldName)` renames a slot, as it does a node.

A host fills the **default** slot by nesting, and a **named** one with a `slot` block of its own inside the instance:

```
ListPage Page_2 {
    Row Item_0 { Label = "Warped Wire" }        // into the default slot
    slot Detail {                               // into the slot named Detail
        Text Note { Text = "A length of wire bent out of shape." }
    }
}
```

A fill holds widgets and nothing else: a `@tag` line next to them is DUI2019 -- tag the widgets, or the slot's own
declaration. A named slot holds one widget -- put several in a container. A fill outside a
component instance is DUI5019; one naming a slot the component does not declare is DUI5020.

## View models

A view model is an object that holds a screen's state and commands, apart from the widgets that show them. The widgets
bind to it; it never refers to them. One view model can serve several screens, and its logic can be tested without any
UI.

```
viewmodels {
    PlayerVM    Player                   // the host gives it
    SettingsVM  Settings = new           // this widget makes one
    InventoryVM Inventory = global       // from the game's registry, by class
    InventoryVM Stash = global "Stash"   // by class and name
    PartyVM     Party = parent           // the nearest enclosing widget's PartyVM
}

VerticalBox Root {
    Text Name { Text <- Player.Name }
    Native.Slider Volume { Value <-> Settings.MasterVolume }
    Native.Button Apply { OnClicked += Settings.Apply() }
}
```

Each entry is a type and a name, and optionally where the object comes from. The type is a class: its reflected name
(`PlayerVM` for a C++ `UPlayerVM`), a full path (`/Script/MyGame.PlayerVM`, `/Game/UI/BP_PlayerVM`), or a name given
by `use /Script/MyGame.PlayerVM as PlayerVM`. A type that names no class, or two, is DUI6015; two entries of one name
are DUI3024; a name the class already uses is DUI6016.

Each entry becomes a variable of the class -- an object reference of that class, FieldNotify, Expose on Spawn, in the
*ViewModels* category -- so everything that can set a variable can hand the widget its view model.

### Where the object comes from

| Written | Means |
|---|---|
| `PlayerVM Player` | The host gives it: a pin of the node that creates the widget, `SetViewModel(Name, Object)` from C++ or Blueprint, a Blueprint Set of the variable, or a host's `.dui` line on this widget used as a component (`Row Audio { Channel <- Settings.Audio }`). |
| `= new` | The widget makes one, outered to itself, when it was given none. Also in the designer's preview, which then shows the view model's defaults; such an instance answers `IsDesignTimeInstance()` true. `new` of an abstract class is DUI6017. |
| `= global`, `= global "Name"` | `UDreamViewModelSubsystem` (one per game instance): `Register(Object, Name)` shares an object, and the entry is filled from it by class, and name when one is written. When nothing fits yet, the entry waits and is filled when a fitting object is registered. Empty in the designer. |
| `= parent`, `= parent "Name"` | The nearest enclosing user widget's entry of a fitting class (and that name, when written), followed as it changes. What a component uses to share its host's view model without being handed it. |

The object is in place before On Initialized runs and before any binding's first value. An entry the host already set
keeps what it was given.

### Writing a view model

Any UObject class works. A class that implements `INotifyFieldValueChanged` -- `UDreamViewModel` is the convenient
base, and UE's own `UMVVMViewModelBase` works the same way -- tells bindings when a field changes, and bindings through
it then update only when it does. A class that does not is read every frame.

```cpp
UCLASS()
class UPlayerVM : public UDreamViewModel
{
    GENERATED_BODY()
public:
    void SetHealth(float InHealth) { DREAM_VM_SET(Health, InHealth); }

    UFUNCTION(BlueprintCallable, Category = "Player")
    void Heal(float Amount) { SetHealth(Health + Amount); }

protected:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Player")
    float Health = 100.f;
};
```

- Members a binding reads must be visible to Blueprint: `BlueprintReadOnly` / `BlueprintReadWrite` properties,
  `BlueprintCallable` / `BlueprintPure` functions (DUI5023 names the one that is not).
- `DREAM_VM_SET(Member, Value)` assigns when the value changed and announces the field. A Blueprint view model's Set node
  of a FieldNotify variable announces on its own.
- A value computed from others (`GoldText` from `Gold`) is announced with `BroadcastFieldValueChanged` when its sources
  change. Offering such display-ready members is how a view model converts values; there is no separate converter.
- A view model that wants to react when the UI writes a value back through `<->` offers `Set<Member>` (BlueprintCallable,
  one parameter): `<->` calls it instead of writing the property.

## Bindings and routes

Three arrows connect the tree to the class's code.

### `<-` drives a property

```
Text <- GetTitle()
RenderOpacity <- Fade() * 0.8
Shown <- Count() > 0 && !IsLocked()
```

The right side is an expression, re-evaluated as the class runs:

- a call to a function of the class, `Func()` or `Func(a, b)`;
- a variable of the class, `Volume` -- including a `props` entry and a `viewmodels` entry;
- a member path through an object variable, `Player.Name`, `Player.Stats.Title`, or a call at its end,
  `Player.FormatGold(Player.Gold)`;
- a literal: a number, a string, `true`, `false`, or `@Resource`;
- operators, loosest first: `||`; `&&`; `==` `!=`; `<` `<=` `>` `>=`; `+` `-`; `*` `%`; and the prefixes `!` and `-`.
  Parentheses group. There is no `/` -- it belongs to paths and comments -- and no `? :`; division and choices go in
  a function.

A bare `Func()` binds that function directly. Anything richer is compiled into a generated function. The property
needs a setter (DUI5005) -- a component's prop excepted, see [`props`](#props) -- and only whole properties of the widget, its visual or a behaviour can be bound (DUI5008):
not a slot property, not a field inside a struct. A function the class lacks is DUI5004; an expression the compiler
cannot lower is DUI5011. A path segment its class does not have, or cannot show to Blueprint, is DUI5023; a path that
goes on past a value that is not an object is DUI5024.

**When a binding updates.** The compiler records every variable and member the expression reads. When each of them is
FieldNotify -- the variable on this class, and every member along a path on an object that implements
`INotifyFieldValueChanged` -- the binding updates when one of them announces a change, and costs nothing in between.
Otherwise it is read every frame. A call with arguments (`Format(Gold)`) may read anything, so a binding holding one is
always read every frame; a view model offering the formatted value as a member is the way out. While an object along a
path is not set, the binding is not evaluated and the property keeps the value it had.

`DreamUI.Binding.Dump` lists every live widget's bindings, how many update on a change and how many every frame, and
why. `DreamUI.Binding.ForcePoll 1` reads them all every frame, which tells a change nobody announced from a wrong
binding.

### `->`, `+=` and `=` route an event

```
OnClicked -> HandleConfirm
OnClick -> emit Picked(ValueIndex)
OnClicked += Settings.Apply()
OnValueChanged += Settings.SetVolume(Value)
OnValueChanged += Settings.SetVolume
OnInit = HandleInit
```

The left side is an event of the widget, its visual or a behaviour: a `BlueprintAssignable` multicast delegate, a
`DreamUIEventDelegate` property, or a single-cast delegate. The operator says what happens to the event's other
listeners:

| Operator | Means | Takes |
|---|---|---|
| `+=` | Adds this listener beside the others. | A multicast delegate or a `DreamUIEventDelegate`. |
| `=` | This is the event's one listener. | A single-cast delegate. |
| `->` | Either, by the event's kind. | Any event. |

`+=` on a single-cast delegate, which cannot hold a second listener, and `=` on a multicast event, which would have to
drop the listeners the control and the Blueprint graph added, are DUI5025.

The right side is one of:

- a function of the class, no parentheses (`HandleConfirm`): it takes what the event sends;
- `emit` of an event the class declares, with its arguments;
- a function of an object the class holds, by a member path (`Settings.Apply`). With parentheses, they are its
  arguments -- expressions over the class, with the event's own parameters in scope by name (`Settings.SetVolume(Value)`);
  without them, it takes nothing or exactly what the event sends. Nothing is called while the object is not set. A
  function the object lacks is DUI6018; arguments that do not fit, DUI6019.

DUI5010 is an event that is not one; DUI6004 and DUI6005 a handler the class lacks or whose parameters do not match.

### `<->` mirrors both ways

```
Value <-> Volume
Value <-> Settings.MasterVolume
```

The property follows the variable, and a change the control makes is written back into it. The variable must be a
FieldNotify variable of the class, or a member of an object it holds. Writing a member back calls the object's
`Set<Member>` when it has a BlueprintCallable one, and otherwise writes the member and announces it on that object. A
member that cannot be written either way is DUI6020.

### `Shown`

Every widget has `Shown`: `true` is Visible, `false` is Collapsed. It keeps nothing of its own -- reading it reads
Visibility, writing it writes Visibility -- and it is what a condition binds:

```
Widget Detail {
    Shown <- HasDetail()
}
```

A widget that is Hidden or hit-test invisible and stays shown keeps that state.

### Reflection-based binding at run time

The arrows above are language constructs: the compiler resolves them and records their dependencies, including
FieldNotify members and view models. `UDreamDataBinding` provides a separate, reflection-based one-way binding at
run time. It is a `UDreamUIBehaviour` component in the core `DreamGUI` module, connecting compatible `UPROPERTY`
values on two `UObject`s by property name. It does not require their types to be known to the `.dui` compiler.

| Field | Default | Meaning |
|---|---|---|
| `SourceObject` / `SourceProperty` | Unset | The object and property to read. |
| `TargetObject` / `TargetProperty` | Unset | The object and property to write. An unset `TargetObject` means the widget that owns this component. |
| `UpdateMode` | `OnAwake` | An `EDreamBindingUpdateMode`: `OnAwake`, `EveryFrame` or `Manual`. |

`ApplyBinding()` is BlueprintCallable. It resolves the named properties through reflection and returns whether the
value could be applied. This path has the cost and constraints of string property names and compatible reflected
types; it is not the compiler's FieldNotify subscription mechanism.

#### Responsive appearance with `UDreamResponsiveBehaviour`

`UDreamResponsiveBehaviour` applies the **first matching breakpoint rule**, using the parent's size, or the widget's
own size when it is a root. Each `FDreamResponsiveRule` contains:

| Field | Default | Meaning |
|---|---|---|
| `Name` | Unset | The rule's name, reported as `ActiveRule` while it is active. |
| `MinWidth` / `MinHeight` | `0` | Inclusive minimum dimensions. |
| `MaxWidth` / `MaxHeight` | `0` | Inclusive maximum dimensions; zero means no upper bound. |
| `Visibility` | `Visible` | Visibility to apply while the rule matches. |
| `RenderOpacity` | `1` | Opacity to apply while the rule matches. |

Rules override the widget's authored appearance. If no rule matches, the component **restores the appearance it
found before the first override**, rather than leaving the previous rule's visibility and opacity in place.
`EvaluateResponsiveRules()` is BlueprintCallable; `GetActiveRule()` reports the active rule.

`ActiveRule` is `Transient`. The private `bHasBaselineAppearance`, `BaselineVisibility` and `BaselineRenderOpacity`
record the appearance temporarily overridden by a rule. They are deliberately not `UPROPERTY` fields: this record
has no meaning across a load, and there is nothing to restore until the component applies another override.

Both classes live in
[`Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h`](https://github.com/TypeDreamMoon/DreamGUI/blob/main/Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h).
The filename covers both runtime binding and responsive rules; there is no separate `DreamDataBinding.h` to include.

## `if` and `else`

```
if HasSave() {
    Row Continue { Label = "Continue" }
} else if IsLoading() {
    Native.Throbber Spinner { }
} else {
    Text NoSave { Text = "No save data" }
}
```

The condition is a binding expression. Each branch holds widgets. Every widget of every branch becomes a child of the
enclosing node, in order, with a `Shown` bound to "this branch is the one taken": `HasSave()` for the first,
`!HasSave() && IsLoading()` for the second, `!HasSave() && !IsLoading()` for the last. Switching is a change of
visibility: a hidden branch is collapsed, not destroyed, and keeps its state.

- A branch holds widgets only: a property, a slot line, a `@tag` line or a `+` block in one is DUI2018, and so is a slot
  or a loop. Tag the widgets in the branch.
- A widget in a branch that writes a `Shown` of its own is shown when its branch is taken and its own condition holds.
- `if` belongs inside a node; at the top of a file, or an `else` with no `if` before it, is DUI2018.
- `else` may stand on the line the `}` closed or on the next one.

`if` and `else` are keywords only where they lead a branch: a property named `if` is still a property.


## `for` and `each`

Both repeat one template widget per item of a source. The source is a function, `GetOptions()`, or a variable,
`Options` -- a FieldNotify array refreshes the copies when it changes -- or either through a member path,
`Inventory.Items`, `Inventory.Filtered()`, refreshed when anything along the path changes. Inside the body,
`Prop <- Item.Member` binds a property of each copy to a member of its item, and `Event -> Item.Func()` routes an event
of each copy to a function of its item:

```
for Item in Inventory.Items {
    HorizontalBox {
        Text { Text <- Item.Name }
        Native.Button { OnClicked += Item.Use() }
    }
}
```

When an item announces a change of a member a copy shows (the item implements `INotifyFieldValueChanged` and the member
is FieldNotify), that copy is updated alone; the rest of the list is not touched. A member nothing announces is read
again when the list refreshes.

### `for`: copies in the panel

```
VerticalBox Options {
    Text Header { Text = "Options" }
    for Option in GetOptions() {
        Row { Label <- Option.Label  Kind <- Option.Kind }
    }
    Text Footer { Text = "…" }
}
```

At run time the template stays in the tree, collapsed, and one copy per item takes its place among the host's
children, in item order -- between whatever siblings surround the `for`. Nothing is virtualized and nothing else is
made: the host's own container arranges the copies like any children. Use it for short lists: a settings page's
options, a tab bar.

### `each`: a list view's cells

```
Widget Inventory {
    + UIListView { }
    each Item in GetItems() {
        Row Cell { Label <- Item.Name }
    }
}
```

`each` fills the list view of the widget it is written in: the cells are made and recycled as the list scrolls. Use it
for long lists.

### The rules both keep

- The body is exactly one widget, the template (DUI5021, DUI5012). Wrap several in a container. A `@tag` line goes on
  that widget, not in the body beside it (DUI2024); every copy carries the widget's tags.
- A loop is not the root, and does not nest in another loop (DUI5021, DUI5012). The way to nest is a component whose own
  file has the inner loop, given its list through a `props` entry.
- A `for` needs a host that takes any number of children; an `each` needs a `+ UIListView` (or a recyclable scroll
  view) on its host.
- In the body, the only binding is the single hop `Item.Member`, and the only route to an object is `Item.Func` /
  `Item.Func()`; an expression, a `<->`, or a route to another object there is DUI5014.
- The source must exist on the class (DUI6006) and be an array of objects (DUI6007).
- When the array's element class is known (`TArray<UItemVM*>`, not `TArray<UObject*>`), `Item.Member` must be a member
  of it (DUI6021), and `Item.Func` a function of it that takes nothing or exactly what the event sends (DUI6022). With an
  array of `UObject` the item's class is only known at run time, where a line that does not fit is skipped.

## `rows`: a table of instances

The same component several times, differing in a few values, is a table: the type, the style and the property names
are written once, then one line per instance.

```
ListPage Page_0 : PageSize {
    Icon = @IconMap
    rows Row : ListRow (Label, Description) {
        "City Ruins",      "The overgrown remains of a city. The Resistance camp lies to the east."
        "Desert Zone",     "Sand and wind as far as the eye can see."
        "Factory",         "An abandoned factory, still running somewhere deep inside." { Kind = Count }
    }
}
```

is the same tree as three unnamed `Row : ListRow { Label = "…"  Description = "…" }` written where the table stands.
Nothing runs at run time: the table is read into those widgets, so a row is a widget like any other -- it builds,
binds and lays out exactly as the line it stands for would.

- **The header** is `rows`, a node type (a tag, a container, an alias, `@Name`, a path), optional `: Style` clauses (one or
  several, `: ListRow : Dense`), and the
  columns: property names, dotted or not, in parentheses. Each is written once (DUI2020).
- **A row** is its values, in column order, separated by commas -- any value a property takes: a string, a number, a
  tuple, a colour, a word, `@Resource`. One row per line, or several on a line separated by `;`. A row has exactly as
  many values as there are columns (DUI2020), and a row that does not read makes no widget. Values are written, not
  bound: a column is `=`, never `<-`.
- **A block at the end of a row** (`{ Kind = Count }`) holds what that one row needs beyond the columns: more
  properties, `@slot` lines, `@tag` lines, `+` components, children. A line in it that names a column wins over the cell.
- **Ids come from the first value**, the row's key: `<id of the enclosing node>__<type>_<key>`, the key with every run
  of characters an id cannot hold made one `_` and cut to 32 characters -- `Page_0__Row_City_Ruins`. A row inserted or
  moved therefore leaves every other row's id, and the localization keys made from it (`Page_0__Row_City_Ruins.Label`),
  where they were. Two rows whose keys make the same id are told so (DUI3023, a warning): the second gets `_1`, which
  does move when rows are reordered. A key nothing of which an id can hold is counted like any unnamed node.
- `rows` is a keyword only before a type and a column list: a property called `rows`, or a node whose type is, reads
  as it always did.

Code that needs a row by name writes that row as a node of its own.

## Timelines

A `timeline` block is an animation the file owns:

```
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external
```

One line per track: a path of node ids (`Row/Title`), the property it drives, and keys `time = value`, each with an
optional `ease` from the tween library's names. A line with no path drives the widget the animation lives on. `@time
-> Name` is a key on the event track. `loop` is `Once`, `Loop` or `PingPong`; `duration` defaults to the last key. A
track may drive what the animation editor offers -- a property marked `Interp`. `external` names an animation that
lives in the asset and is edited in Sequencer. See also [Timelines](https://gui.toolchain.64hz.cn/en/docs/dui/timelines) on the docs site.

A block the file owns can be edited in the animation editor too: each edit is written back into the block -- keys,
eases, event keys, the duration, a track added or removed as a line -- and a key with tangents of its own is refused.
See [Designer write-back](#write-back).

## What the designer writes back

The designer is a front end for the file: a change made there is a change to the text, written into the line that
holds the value -- or a new line after the node's others -- with everything else byte for byte as it was.

**Values.** Numbers, strings, tuples, colours, enums and asset references are written as the literals the language
reads. A list or a set is written as `[a, b]` and a map as `{ key: value }`, the whole container replacing its line;
a set or a map that only came back in another order is no change, and a changed one keeps the file's order with new
entries after it. An element that is a struct with no short form is written one of two ways. A struct that names a primary field
(`DreamGUI_PrimaryField`; today a ring menu's item) is a [field map](#structs-as-field-maps):
`Items = [{ "Lamp", Tag: Lamp }, { "Door", Tag: Door, bEnabled: false }]`, the primary field first and without its key,
only the fields that differ from the struct's defaults after it, and its text fields given localization keys of their
own (`Items.Label`). Any other struct -- a layer style's drop shadow, an adjustment -- is the quoted text of its fields
that differ from the struct's defaults, as the builder reads it: `Style.DropShadows = ["(Angle=45.000000,Distance=8.000000)"]`.
It stays in that form because a field such as a linear colour would be quantized through a hex code in a map.
A reference to another widget of the hierarchy (an
explicit navigation target, a toggle's group) is written as that node's bare id; one into another file's component has
no id here and is refused.

A turn -- made with the 2D view's rotate handle, the 3D view's gizmo or the details panel -- is written as the euler,
`RelativeRotationEuler = (0, 0, 30)`: pitch, yaw and roll in degrees, a turn in the canvas plane being the roll,
clockwise for a positive one. The quaternion the asset keeps has no spelling and is never written.

A value a node takes from its **style's** `+ Component` line is written as an override on the node: a
`+ Component { … }` line of its own holding the changed value. The style is shared and stays as it was.

`@fill N` given a new weight has the number replaced; a bare `@fill` given another `SizeRule` becomes
`@slot SizeRule = …`; and `@fill 2` given another rule becomes its two lines, `@slot SizeRule = …` and
`@slot FillWeight = 2`.

**Tags.** `Tags` is written as the node's `@tag` lines. A name the designer removes comes out of its line, and a line
left empty is taken out; names it adds join the last line that keeps names, or a new `@tag` line goes after the node's others
when there is none. A node that writes `Tags = [...]` keeps that line and has it rewritten. A node that writes tags both
ways is refused (DUI7004), and so is a name an `@tag` line cannot
hold (one with a space, or a leading digit) on a node that has tag lines. On a node with none, such a name is written as
a `Tags = [...]` line.

**`@slot Size`.** An edit that leaves the slot's minimum and maximum desired size equal rewrites the shorthand's value.
One that makes them differ rewrites the line into `@slot MinDesiredSize = …` and `@slot MaxDesiredSize = …` -- inside a
`@slot { }` block two lines of the block, side by side when the block is on one line. A node that writes neither and is
given both with one value gets one `@slot Size` line, which is what the Blueprint converter writes for such a slot.

**Several styles.** A node's `: A : B` is never rewritten by an edit to its values. The values the styles give the node
are taken in the order the builder applies them, and a changed one is written as the node's own line, which comes last.

**Structure.** A widget added in the designer is written with the type this file would use: its `use … as` alias when
the class has one here, a container type for a plain widget that lays out children (`VerticalBox Column`). One dropped
into a named slot of a component instance is written into that slot's fill, `slot Detail { … }`, which is written first
when the instance has none. Removing a widget takes its whole block; moving one lifts its text and puts it back in front
of the sibling it now stands before; a rename is written as the new id with `(was: OldId)`, so the next compile carries
the variable, the bindings, the animation paths and the localization keys the old name owned. An unnamed node renamed in
the designer gets its first id, written after its type. Behaviours are `+` lines added, removed and moved; a new panel
or visual class rewrites the node's type or its `+` line, and lines the new class has no property for are taken out.

A row of a `rows` table is one line: removing it takes the line, moving it lifts the line to before another row of
the same table, and duplicating it copies the line (the copy's key is bumped until the author gives it its own).

**Statements.** A binding from the Bind control is written as `Property <- Function()`, replacing the property's own line
when it has one; *Remove Binding* takes the line out, and *Unbind and Set to This Value* writes `Property = value` in its
place. A two-way binding is `Value <-> Variable` (or a viewmodel member, `Value <-> Settings.Volume`), offered only on a
control that announces its value through `OnValueChangedBP`. A route from the Events rows is `Event -> Handler`: on the
node's line, or in the node's own `+ Component` block for a behaviour the node writes as one. These are written
straight into the document as one line each, in the gesture's transaction.

**Animations.** An edit in the animation editor to a `timeline` block's animation is written into the block: keys' times
and values, the ease from one key to the next, event keys, the duration, a track added or removed as a line. A track
whose keys did not change keeps its line byte for byte; comments and the `loop` line stay as they are.

**What is refused**, with the reason, before anything changes -- a few things have no line to write into, and the
designer says so (DUI7004) instead of inventing one:

- the visibility of a widget an `if` shows or hides -- change the condition, or move the widget out of the branch -- and
  a binding or route written over a line the front end made rather than read;
- the template of a `for` or an `each` removed or moved out of its loop: a loop with nothing to repeat does not build;
- on a `rows` table: renaming a row, giving it a `+` block, moving it out of its table, or placing a node beside the
  table (the place would be inside it);
- a change to `Tags` on a node that writes it both as `@tag` lines and as a `Tags = [...]` line, or one that adds a name
  an `@tag` line cannot hold to a node that has tag lines;
- a value whose type or content has no spelling (DUI7003, today only non-finite floats among values that have one);
- in the animation editor, a key with tangents of its own or stepped interpolation -- write `timeline Name external` to
  hand the animation to Sequencer for good.

If a source file is read-only or a write fails, the document keeps the edited text in memory. A later flush retries
that write even when no value changed. Compile retries it too, and reports DUI6023 while it still cannot write; the
current hierarchy and edits are retained. Check the file out or resolve the write failure, then compile again.

### From designer values back to text

`FDreamUITextWriteBack` turns designer edits into `.dui` changes. Its central rule is to **compare values, not source
spellings**. `FDreamUITextPatcher` edits text slices; giving it every property the designer might have touched would
rewrite unchanged values whenever the printer's formatting differs from the author's. For example, `(400,240)` and
`(400, 240)` describe the same value.

A flush therefore asks whether rebuilding the current text would produce the value held by the live tree. It builds
a second tree from that text with the compiler's own `FDreamUITextBuilder`, then compares the values. It does not
maintain a separate interpretation of the language for write-back.

One flush is one undo step, or no step when nothing changed. A no-op does not call `SetContent` or create an empty
transaction. Undo marks the tree for regeneration **on the next tick**: the notification arrives inside
`FTransaction::Apply` while it is still restoring objects, so other widgets in the transaction may not yet have
their restored values.

### Where the text lives: `UDreamUIDocument`

For text-backed widgets, the text is the source of truth and the designer is its graphical front end.
`UDreamUIDocument`, a `UCLASS(Transient)` `UObject` in `DreamGUIEditor`, holds the full text of one `.dui` file so
Unreal's transaction system can undo it.

The `UObject` matters because undo restores objects. If the text were outside the transaction, Ctrl+Z could restore
the widget while leaving the new value in the file. A recompile, a reopen or the next unrelated edit would then
regenerate the tree from that file and bring the supposedly undone change back.

Callers must preserve three contracts:

1. **The caller owns the transaction.** `SetContent` calls `Modify()` but never opens a transaction itself. It reports
   a missing transaction instead of silently producing a non-undoable edit.
2. **Memory is authoritative; disk may lag.** A failed write, such as a read-only file that has not been checked out,
   keeps the edit in memory, reports the failure and retries on a later flush.
3. **`FlushToDisk` never clears the read-only bit.** Check out the file through source control rather than making it
   writable behind the source-control system's back.

There is **one document per file, not per editor window**. Two toolkits holding separate documents for the same file
would have independent text and disk hashes: an undo in one could look like an external edit to the other, causing
the windows to keep reloading and overwriting each other's state.

`FDreamUIDocumentRegistry` prevents that by using the normalized absolute path as its key. The registry holds weak
references; callers retain documents through `FDreamUIDocumentHandle`, a move-only, non-copyable scoped handle.
The document API and this ownership contract are declared in
[`Source/DreamGUIEditor/Public/Text/DreamUIDocument.h`](https://github.com/TypeDreamMoon/DreamGUI/blob/main/Source/DreamGUIEditor/Public/Text/DreamUIDocument.h).
