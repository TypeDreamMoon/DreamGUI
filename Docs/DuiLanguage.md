# The `.dui` language

A `.dui` file is the source of a DreamGUI widget Blueprint: a tree of widgets, their properties, their bindings and their
animations, as text. The Blueprint names the file as its *Source File*, compiling the Blueprint reads the file, and the
designer writes its edits back into the file. This page is the reference for writing one by hand.

- [Where a file lives](#where-a-file-lives)
- [File structure](#file-structure)
- [Nodes and ids](#nodes-and-ids)
- [Node types](#node-types)
- [Properties and values](#properties-and-values)
- [Slot lines: `@slot` and `@fill`](#slot-lines-slot-and-fill)
- [Components: `+ Class`](#components--class)
- [Styles](#styles)
- [Resources](#resources)
- [`use`](#use)
- [Writing a component](#writing-a-component)
- [View models](#view-models)
- [Bindings and routes](#bindings-and-routes)
- [`if` and `else`](#if-and-else)
- [`for` and `each`](#for-and-each)
- [Timelines](#timelines)
- [What the designer writes back](#what-the-designer-writes-back)
- [Diagnostics](#diagnostics)
- [A worked example](#a-worked-example)

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

- `: StyleName` -- the node wears a style. See [Styles](#styles).
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
   [`use`](#use).
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
| Resource | `@Accent` | The value of a `resources` entry. See [Resources](#resources). |
| Node id | `CheckMark` | On a property that holds a widget, a visual or a behaviour: the node of that id in this file. |

A flags enum has no `|`; write a combination as the number its flags add up to.

A string can carry the key it is localized under: `Text = "OK" @key("Dialog.Confirm")`. Without one the key is
`<node id>.<property>` -- with the component's class and position between the two for a line of a `+` block -- in the
namespace the `class` line gives.

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
  then the node's own. A base that comes back around is DUI3015.
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
- A block, or a style, lays the hole out: components, properties and slot lines -- never children, since what goes in
  it is the host's (DUI2019). A slot with a layout container takes several widgets; one without takes one.
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

A fill holds widgets and nothing else. A named slot holds one widget -- put several in a container. A fill outside a
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

- A branch holds widgets only: a property, a slot line or a `+` block in one is DUI2018, and so is a slot or a loop.
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

- The body is exactly one widget, the template (DUI5021, DUI5012). Wrap several in a container.
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

- **The header** is `rows`, a node type (a tag, a container, an alias, `@Name`, a path), an optional `: Style`, and the
  columns: property names, dotted or not, in parentheses. Each is written once (DUI2020).
- **A row** is its values, in column order, separated by commas -- any value a property takes: a string, a number, a
  tuple, a colour, a word, `@Resource`. One row per line, or several on a line separated by `;`. A row has exactly as
  many values as there are columns (DUI2020), and a row that does not read makes no widget. Values are written, not
  bound: a column is `=`, never `<-`.
- **A block at the end of a row** (`{ Kind = Count }`) holds what that one row needs beyond the columns: more
  properties, `@slot` lines, `+` components, children. A line in it that names a column wins over the cell.
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

## What the designer writes back

The designer is a front end for the file: a change made there is a change to the text, written into the line that
holds the value -- or a new line after the node's others -- with everything else byte for byte as it was. A few
things have no line to write into, and the designer says so (DUI7004) instead of inventing one:

- the visibility of a widget an `if` shows or hides -- change the condition, or move the widget out of the branch;
- a `SizeRule` other than Fill on `@fill 2` -- write `@slot SizeRule = …` and `@slot FillWeight = 2` in its place.
  A new weight replaces the number in `@fill 2`, and a bare `@fill` given another rule becomes `@slot SizeRule = …`;
- anything on a row of a `rows` table but a column's value or a line of the row's own block: the row's line spells only
  its columns. A column's value is replaced in its cell. A row is not removed, moved, named or given a `+` block from
  the designer, and no node is placed beside a table (the place would be inside it) -- edit the text.

A value a node takes from its style's `+ Component` line is not written either (DUI7005): the style is shared, and the
node has no `+` line of its own to hold the change. Add one (`+ VerticalBox { Spacing = 20 }`) or change the style.

A widget added in the designer is written with the type this file would use: its `use … as` alias when the class has
one here, a container type for a plain widget that lays out children (`VerticalBox Column`). One dropped into a named
slot of a component instance is written into that slot's fill, `slot Detail { … }`, which is written first when the
instance has none. An unnamed node renamed in the designer gets its first id, written after its type. The template of a
`for` or an `each` is not removed or moved out of its loop: a loop with nothing to repeat does not build.

A turn -- made with the 2D view's rotate handle, the 3D view's gizmo or the details panel -- is written as the euler,
`RelativeRotationEuler = (0, 0, 30)`: pitch, yaw and roll in degrees, a turn in the canvas plane being the roll,
clockwise for a positive one. The quaternion the asset keeps has no spelling and is never written.

If a source file is read-only or a write fails, the document keeps the edited text in memory. A later flush retries
that write even when no value changed. Compile retries it too, and reports DUI6023 while it still cannot write; the
current hierarchy and edits are retained. Check the file out or resolve the write failure, then compile again.

## Diagnostics

Every message has a code, `DUInnnn`, printed as `File.dui(line,col): error DUI3001: …`. The first digit says which stage
refused: 1 lexer, 2 parser, 3 meaning, 4 values, 5 building the tree, 6 compiling into the Blueprint, 7 writing back.

| Code | Name | Meaning |
|---|---|---|
| DUI1001 | UnexpectedCharacter | A character that cannot begin any token. |
| DUI1002 | UnterminatedString | A string that reaches the end of its line without its closing quote. |
| DUI1003 | UnterminatedComment | A `/*` that never reaches `*/`. |
| DUI1004 | MalformedNumber | A number that cannot be read: two decimal points, a trailing dot, a unit glued on (`24px`). |
| DUI1005 | MalformedHexColor | A colour whose digit count is not 3, 4, 6 or 8. |
| DUI1006 | IdentifierTooLong | A name longer than an `FName` holds. |
| DUI1007 | AssetPathTooLong | An asset path longer than an `FName` holds. |
| DUI2001 | UnexpectedToken | A token where the grammar wants something else; the message names both. |
| DUI2002 | UnclosedBlock | A `{` with no matching `}`. |
| DUI2003 | UnclosedTuple | A `(` with no matching `)`. |
| DUI2004 | MissingNodeId | A node with neither an id nor a block, or an unnamed node with `(was: …)`. |
| DUI2005 | MissingPropertyValue | A property with no `=` or arrow, or nothing after it. |
| DUI2006 | MalformedRoot | No root node in a file compiled into a class, or more than one. |
| DUI2007 | MalformedClassDeclaration | `class` twice, with an empty path, or inside a node. |
| DUI2008 | MalformedWasClause | `(was: …)` holding something other than one id. |
| DUI2009 | MalformedKeyOverride | `@key(…)` holding something other than one string, or after a value that is not a string. |
| DUI2010 | MalformedLoopHeader | A `for` / `each` header that is not `<keyword> Var in Source` or `Source()`. |
| DUI2011 | MalformedBindingExpression | The right side of `<-`, `<->` or an `emit`'s arguments that do not parse. |
| DUI2012 | ImportFailed | A `use` that could not be honoured: no such file, unreadable, does not parse, or a cycle. |
| DUI2013 | NestingTooDeep | Blocks or parentheses nested deeper than 256. |
| DUI2014 | MalformedTimeline | A `timeline` block or line that is not the grammar. |
| DUI2015 | MalformedUseDeclaration | `use … as` without a name, a keyword as the name, or a class path without `as`. |
| DUI2016 | MalformedPropsBlock | A `props` line that is not `Type Name` or `Type Name = value`, or `props` inside a node. |
| DUI2017 | MalformedEventsBlock | An `events` entry that is not `Name` or `Name(Type Param, …)`, or `events` inside a node. |
| DUI2018 | MalformedConditional | An `if` without its condition or block, an `else` with no `if`, or a branch holding something other than widgets. |
| DUI2019 | MalformedSlotDeclaration | A slot with `default` twice, or a block that both declares and fills. |
| DUI2020 | MalformedRows | A `rows` table that does not read: a column list that is not names, a column twice, a row with the wrong number of values. |
| DUI2021 | MalformedViewModelsBlock | A `viewmodels` line that is not `Type Name`, `= new`, `= global ["Name"]` or `= parent ["Name"]` (an empty `""` name included), or a `viewmodels` block inside a node. |
| DUI3001 | DuplicateNodeId | Two nodes share an id. |
| DUI3002 | InvalidNodeId | An id that is not an identifier, starts with a digit, or is a keyword. |
| DUI3003 | UnknownNodeType | A type that is no tag, container, alias, registered widget or path. |
| DUI3004 | UnknownStyle | `: Name` naming no style this file sees. |
| DUI3005 | DuplicateStyle | Two styles of one name in one file. |
| DUI3006 | UnknownBehaviourClass | `+ Name` naming nothing a widget can carry. |
| DUI3007 | DuplicateSlotName_Retired | Retired; two slots of one name are DUI3001. |
| DUI3008 | ShadowedLoopVariable | A loop variable that hides an enclosing loop's (a warning). |
| DUI3009 | ParentRefusedChild | A parent that will not take this child: a full content widget, a named slot filled twice. |
| DUI3010 | RenameOldIdStillInUse | `(was: X)` while X is still an id in the file. |
| DUI3011 | DuplicateWasId | Two nodes claim `(was: X)`. |
| DUI3012 | SelfRename | `(was: X)` on the node called X. |
| DUI3013 | RenameGraphReferenceAmbiguous | The old id is also another member's name, so graph references are not moved (a warning). |
| DUI3014 | DuplicateResource | Two resource entries of one name. |
| DUI3015 | StyleCycle | A style that inherits itself through its bases. |
| DUI3016 | DuplicateTimeline | Two timelines of one name. |
| DUI3017 | DuplicateComponentAlias | Two `use … as` lines give one name. |
| DUI3018 | AliasShadowsBuiltIn | A `use … as` name that is a tag or a container. |
| DUI3019 | DuplicateProp | Two `props` lines declare one name. |
| DUI3020 | DuplicateEvent | Two `events` entries of one name, or one parameter twice. |
| DUI3021 | UnknownNamespace | `ns.Name` whose `ns` no `use … as ns` declares. |
| DUI3022 | MultipleDefaultSlots | More than one `slot … default` in one file. |
| DUI3023 | DuplicateRowKey | Warning. Two rows of a `rows` table whose first values make the same id; the second's id then moves with the order. |
| DUI3024 | DuplicateViewModel | Two `viewmodels` entries of one name. |
| DUI4001 | UnknownProperty | No property of that name; the nearest one is suggested. |
| DUI4002 | UnknownPropertyPathSegment | A dotted path whose head resolves and whose tail does not. |
| DUI4003 | ValueTypeMismatch | A value whose shape cannot be the property's type. |
| DUI4004 | TupleArityMismatch | A tuple with the wrong number of elements. |
| DUI4005 | UnknownEnumValue | A word the property's enum does not declare. |
| DUI4006 | PropertyNotWritable | A property text cannot write: transient, deprecated, part of the object graph, a delegate. |
| DUI4007 | UnknownResource | `@Name` naming no resource entry. |
| DUI4008 | ResourceTypeMismatch | A resource whose value is not its declared type, or a non-`Asset` resource used as a node type. |
| DUI5001 | AssetNotFound | An asset or class path that does not load. |
| DUI5002 | NoVisualForProperty | A property of a visual the node's type does not create. |
| DUI5003 | NoPanelSlotForProperty | A slot line on a node whose parent lays out no panel. |
| DUI5004 | BindingFunctionNotFound | A bound function the class does not have, or one that takes parameters. |
| DUI5005 | BindingTargetHasNoSetter | A bound property with no setter. |
| DUI5006 | NotAUserWidgetClass | A node typed by a class that is not a concrete DreamUI user widget. |
| DUI5007 | LoopNotExpanded | A loop built by a caller with nowhere to record it, and skipped (a warning). |
| DUI5008 | BindingTargetNotSupported | A binding on something no binding can name: a slot, a container, a field of a struct. |
| DUI5009 | NothingToBuild | The tree had no root; the parser said why. |
| DUI5010 | EventNotFound | `X -> …` where X is not an event of the destination. |
| DUI5011 | BindingExpressionUnsupported | An expression the compiler cannot turn into a function. |
| DUI5012 | EachMisplaced | An `each` at the root, nested, without a list view, or without exactly one template. |
| DUI5013 | NodeReferenceNotFound | A node id written as a value that names no node in the file. |
| DUI5014 | LoopBodyBindingUnsupported | An expression or a `<->` inside a loop body. |
| DUI5015 | TimelineTargetNotFound | A track path that names no node. |
| DUI5016 | TimelinePropertyNotAnimatable | A track property no animation track can drive. |
| DUI5017 | UnknownEaseName | An `ease` name the tween library does not have. |
| DUI5018 | ComponentAliasUnresolved | An alias that names no class: no `class` line and no Blueprint for the file, or a path that loads nothing. |
| DUI5019 | SlotFillOutsideComponent | `slot Name { … }` filling a slot under a node that is not a component instance. |
| DUI5020 | UnknownSlotToFill | A fill naming a slot the component does not declare. |
| DUI5021 | ForMisplaced | A `for` at the root, in another loop, in a host with fixed room, or without exactly one template. |
| DUI5022 | SecondLayoutContainer | A second layout container on one node. |
| DUI5023 | MemberPathNotFound | A member path segment its class does not have, or does not show to Blueprint (an UnrealSharp property needs `BlueprintReadOnly` or `BlueprintReadWrite`). |
| DUI5024 | MemberPathThroughNonObject | A member path that goes on past a value that is not an object. |
| DUI5025 | RouteOperatorMismatch | `+=` on a single-cast delegate, or `=` on a multicast event. |
| DUI6001 | SourceFileUnreadable | The Blueprint's Source File does not exist or cannot be read. |
| DUI6002 | EmptyTree | The file parsed and produced no tree. |
| DUI6003 | ClassPathMismatch | The `class` line names another Blueprint than the one compiling (a warning). |
| DUI6004 | EventHandlerNotFound | `-> Handler` naming a function the class does not have. |
| DUI6005 | EventHandlerSignatureMismatch | A handler whose parameters are not the event's. |
| DUI6006 | EachSourceNotFound | A loop source the class has neither as a function nor as a variable. |
| DUI6007 | EachSourceNotObjectArray | A loop source that is not an array of objects. |
| DUI6008 | PropTypeUnknown | A `props` or `events` type with no Blueprint pin. |
| DUI6009 | PropNameTaken | A `props` name another member already has with another type, or a widget's id. |
| DUI6010 | EmitUnknownEvent | `emit Name` of an event this file does not declare. |
| DUI6011 | EmitArgumentMismatch | `emit` arguments that are not the event's parameters. |
| DUI6012 | EmitRouteUnsupported | An `emit` where no handler can be generated: in a loop body, or for an event Blueprints cannot carry. |
| DUI6013 | PropDefaultInvalid | A `props` default its type cannot hold. |
| DUI6014 | EventNameTaken | An `events` name another member of the class already answers to. |
| DUI6015 | ViewModelClassUnknown | A `viewmodels` type that names no class, or two. |
| DUI6016 | ViewModelNameTaken | A `viewmodels` name another member of the class already answers to. |
| DUI6017 | ViewModelSourceInvalid | `= new` of an abstract class. |
| DUI6018 | RouteMemberFunctionNotFound | `-> Path.Func` whose function the object's class does not have, or does not let Blueprint call. |
| DUI6019 | RouteArgumentMismatch | `-> Path.Func` whose arguments do not fit the function, or, without parentheses, whose function takes something other than nothing or what the event sends. |
| DUI6020 | TwoWayTargetReadOnly | `<-> Path.Member` whose member cannot be written back: read-only, and no `Set<Member>`. |
| DUI6021 | LoopItemMemberNotFound | `Item.Member` that the loop source's element class does not have. |
| DUI6022 | LoopItemRouteMismatch | `-> Item.Func` that the element class does not have, or whose parameters fit neither nothing nor the event. |
| DUI6023 | SourceFileWritePending | The document has edits that could not be written, so compiling the older file was refused. |
| DUI7001 | PatchTargetNotFound | A designer edit with no home in the file: an unknown node, a bound property, a slot with no block. |
| DUI7002 | SourceFileChangedUnderEdit | The file changed under a pending edit; nothing was written. |
| DUI7003 | PatchValueNotRepresentable | A value the language cannot spell (a non-finite number); its line is left alone. |
| DUI7004 | PatchSyntaxNotWritable | An edit to something no line spells: a visibility an `if` decides, a shorthand that cannot be rewritten. |
| DUI7005 | PatchStyleComponentNotWritable | An edit to a value a style's `+` line gives the node; add the line to the node or change the style. |

## A worked example

A settings screen built from one row component. Three files: the component, a library that names it and styles the
family, and the screen.

The row -- a label on the left, a value with arrows on the right -- declares what a host sets and hears:

```
// DUI/Settings/SettingsRow.dui
class /Game/UI/Settings/WBP_SettingsRow
use "Settings/SettingsLibrary.dui"

props {
    Text   Label
    Text   Value
    Number Index = 0
}
events {
    Changed(Number Index, Number Step)
}

Widget Root : RowBox {
    + HorizontalBox { Padding = (16, 0, 16, 0)  Spacing = 12 }
    + UIButton { TransitionType = None }

    Text LabelText : Caption {
        Text <- Label
        @fill
    }
    Native.Button Previous : Arrow {
        OnClicked -> emit Changed(Index, -1)
        Text { Text = "◀" }
    }
    Text ValueText : Caption {
        Text <- Value
        HAlign = Center
        @slot MinDesiredSize = (220, 0)
    }
    Native.Button Next : Arrow {
        OnClicked -> emit Changed(Index, 1)
        Text { Text = "▶" }
    }
}
```

The library holds the family's look and its names. Because it has no root it can be used plainly or under a namespace,
and its `use … as` line travels with it:

```
// DUI/Settings/SettingsLibrary.dui
use "Settings/SettingsRow.dui" as Row

resources {
    Color Ink   = #E6E9F0
    Color Muted = #8C93A6
    Color Panel = #1B1E26
}

style Caption {
    FontSize = 18
    Color    = @Ink
}
style RowBox {
    AnchorData.SizeDelta = (0, 48)
}
style Arrow {
    AnchorData.SizeDelta = (40, 40)
}
style Column {
    + VerticalBox { Spacing = 8 }
    @fill
}
```

The screen places three rows, routes each one's `Changed` to a handler of its own, shows a hint only while there are
unsaved changes, and lists the key hints from the class's data:

```
// DUI/Settings/SettingsScreen.dui
class /Game/UI/Settings/WBP_SettingsScreen
use "Settings/SettingsLibrary.dui" as ui

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay { }

    Image Backdrop {
        Brush.TintColor = @ui.Panel
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
    }

    VerticalBox Page {
        Spacing = 24
        Padding = (64, 48, 64, 48)
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

        Text Heading : ui.Caption {
            Text = "Settings"
            FontSize = 32
        }

        Widget Rows : ui.Column {
            ui.Row Audio {
                Label = "Master volume"
                Value = "80 %"
                Changed -> HandleVolumeChanged
            }
            ui.Row Display {
                Label = "Display mode"
                Value = "Fullscreen"
                Index = 1
                Changed -> HandleDisplayModeChanged
            }
            ui.Row Language {
                Label = "Language"
                Value = "English"
                Index = 2
                Changed -> HandleLanguageChanged
            }
        }

        if HasUnsavedChanges() {
            Text UnsavedHint : ui.Caption {
                Text = "Unsaved changes"
                Color = @ui.Muted
            }
        }

        HorizontalBox {
            Spacing = 16
            Native.Button Apply { OnClicked -> HandleApply }
            Native.Button Back  { OnClicked -> HandleBack }
            for Hint in KeyHints {
                Text : ui.Caption { Text <- Hint.Label }
            }
        }
    }
}
```

What each part relies on:

- `use … as ui` keeps the library's names apart: `: ui.Caption`, `@ui.Panel`, and the row as `ui.Row` -- the
  library's own `use "SettingsRow.dui" as Row` came along under the namespace.
- `ui.Row Audio { … }` is an instance of the row: `Label`, `Value` and `Index` set its props -- the first values; a
  handler changes `Value` later through the row's variable, `Audio` -- and `Changed ->` routes the event the row raises
  with `emit` from its arrow buttons.
- `VerticalBox Page` and the unnamed `HorizontalBox` are containers written as node types; their `Spacing` and
  `Padding` are the containers'.
- `Widget Rows : ui.Column` gets its vertical box and its fill from the style.
- The `if` shows the hint only while `HasUnsavedChanges()` is true; nothing is created or destroyed when it flips.
- The `for` makes one text per item of the class's `KeyHints` array (a FieldNotify variable of objects with a `Label`
  member), after the two buttons, and `Text <- Hint.Label` binds each copy to its item.
- The screen's class supplies `HasUnsavedChanges`, `KeyHints`, the three `Handle…Changed(Index, Step)` handlers,
  `HandleApply` and `HandleBack`, in C++ or in the Blueprint's graph.
