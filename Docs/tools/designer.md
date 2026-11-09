---
title: The widget designer
description: The DreamUI Designer, rebuilt against UMG's — picking by rect, resize handles and the anchor medallion, marquee selection and drag-to-reparent, the palette, the details panel, components, undo, the source-file menu, event routes, the Content Browser's menu and thumbnails, and what it can and cannot change on a class backed by a .dui.
---

Opening a Dream Widget Blueprint opens the **DreamUI Designer**. It has two modes: *Designer* (the canvas, palette,
hierarchy, details panel and animations) and *Graph* (the Blueprint event graph). The designer edits a **preview
instance** of the class and writes back to the class itself, so there is no apply step and no per-instance override
list to reconcile.

**Note:**

  **The designer is verified by tests, not by eye.** Its gestures are covered by automation tests (the `Designer`
  preset), but nobody has walked every corner of it by hand. Expect rough edges, and report them.

## Gestures on the canvas

The designer was reviewed against UMG's widget designer. Added since upstream:

- **Picking by rect.** A click hits a widget's *rect*, not its rendered triangles. Layout-only panels have no mesh,
  so a raycast could never hit them, which upstream left them unclickable and undroppable; picking by rect is what
  fixes that.
- **Hover feedback**: the widget under the pointer is outlined.
- **Per-axis resize handles**, and the **anchor medallion**, which shows the selected widget's anchors on the canvas.
- **Handles for what the selection has**: a warp's lattice points, a path's points, a rect block's gradients, a clip's
  mask, a glass, shadows and lights, one kind at a time (see [Viewport handles](#viewport-handles)). A drag is one undo
  step, Esc puts it back, and a class backed by a `.dui` gets the change in its file when the button comes up.
- **Marquee selection**: drag a box over empty space to select the widgets in it.
- **Drag to reparent on the canvas**: drag a widget into another container.
- **Content Browser drops**: drag assets straight onto the design surface.
- **Undo**: every designer gesture is one undo step, with create, paste and drop covered.
- **Snapping and the grid**: the toolbar's *Snap* switch and grid size; snapping guides while dragging.
- **Align and distribute**: the *Arrange* menu lines selected siblings up along an edge or centre (two or more
  selected) or evens out the gaps between them (three or more).

The hierarchy panel's context menu has *Wrap With...* (group the selection under a new container inserted at its
position; the widgets keep their layout), *Unwrap* (take a widget out from between its parent and its children,
which keep their order and land where it stood), *Replace With...* (swap the panel arranging the children — unlike
UMG, the widget's own name, place, slot, components and children all stay put; only the layout container changes),
*Show / Hide in Designer*, *Lock / Unlock*, *Find References*, *Focus Selection* (what the F key does), and
*Reveal in VS Code*.

After a widget's name, the hierarchy marks what makes it a layer, one small icon per kind: a **style** (a layer style),
a **group** (a layer group or a shape blend group), a **mask** (a mask on its clip, clipping to the layer below, a mask
transition), a **blend** mode other than normal, an **effect** (a post-process visual such as glass or an adjustment
layer, a holo foil, a pointer light, a tilt card, a mesh modifier), a **warp** (warp, jelly, text on a path) and
**particles**. Hovering a mark lists what it stands for and how each is set. Clicking it turns the effects of that kind
off, and on again, through their own switches -- to compare with and without them; the mark dims while they are off. It
is an ordinary edit: one undo step, saved, and written to the `.dui` of a class backed by one.

### Viewport handles

With one widget selected — unlocked, and the asset's own — the viewport draws handles for what it has. A widget can have
several kinds; one kind is shown at a time, named under the selection with the kind **Tab** shows next. Tab steps through
the kinds the selection has and then to none, which leaves the selection box alone (to resize from a corner a handle sits
on, say), and then back to the first. Tab is the viewport's only while it has the keyboard; in the details panel, the
hierarchy or any text box it moves the focus as it always does.

Which kind a selection shows: the one last picked — by Tab, or a details panel's *Edit in Viewport* button — when the
selection has it; otherwise the first it has: a warp's lattice and a path's points, then the kinds its properties are
tagged with (`DreamGUI_Handle`, see [Property meta tags](../guides/property-meta.md#viewport-handles)) — its visual's,
each of its behaviours', its own — in the order they are declared. With the handles turned off by Tab, every selection
shows none until Tab is pressed again.

| Kind | What it shows, and what a drag changes |
|---|---|
| Warp Lattice, Path Points | A warp's lattice points; a path's points |
| Body Gradient, Border Gradient | A rect block's gradient: its ends, centre and stops ([Gradients](../concepts/gradients.md#viewport-handles)) |
| Shadows | A rect block's own outer and inner shadow, where they fall: dragging one sets its angle and its distance together |
| Glass | A glass's outline and the line its refracting edge reaches in to: its corners (`CornerRadius`, in its own unit mode), its `EdgeWidth`, and where its highlight's light comes from (`LightAngle`) |
| Style Shadows | Each shadow of a layer style (drop shadows round, inner shadows diamond), where it falls, angle and distance together. A shadow that uses the global light turns the global light, and every effect that uses it turns with it |
| Global Light | A layer style's global light, when an effect uses it, as a disc — its angle round the disc, its altitude in from the rim |
| Pointer Light | Where a pointer light stands (in the designer it stands at `LightPosition`) and the circle it reaches |
| Mask | A clipping widget's clip and the mask on it ([Clip masks](../concepts/clip-masks.md)): the cut the mask makes and its feather's band; the clip rect's sides (`ClippingMargin`, which the mask's shape is fitted to), a rect mask's corners (`ClippingCornerRadius`), a polygon's `Roundness`, a star's inner corners or a ring's hole (`InnerRatio`), and the `Feather` |

Corners and feathers move together — all four at once, as a widget's clip does while it sets its corners as one — and
**Alt** moves the one dragged alone. **Shift** keeps an angle to steps of 15°, and on a clip's side moves the opposite side
out with it. **Esc** while dragging puts everything back. A drag is one undo step and is written to the `.dui` when the
button comes up. Things that are not the widget's own to change have no handles: a layer style that follows a preset, a
mask a behaviour drives, a glass that takes its shape from a shape blend, and an effect that is off.

## The palette

The palette lists what can be placed, grouped the way UMG's is: **Basic** (Widget, Text, Image, RectBlock), Panels,
Common, Input, Lists, Scrolling, Containers, Primitive, Shapes, Effects, Components, Modifiers, Advanced, **User
Created** (the project's widget Blueprints), and last **Legacy**.

- Rows are listed by name within a group, and the panels lose their "UMG " prefix.
- **Legacy** holds what the control library replaced — the LGUI-era behaviours, the Blueprint presets, and the Tile View
  and Scroll Box behaviours — kept so that a widget built on one stays explicable. It is always last and starts closed;
  once opened, the designer remembers.
- User Created does not list the plugin's own preset folder.
- The hierarchy's *Create* menu uses the same groups in the same order.
- **Favourites**: mark the entries you use; the favourites group comes first. Registry keys are unchanged, so earlier
  favourites keep their stars.
- **Search**: the search box filters entries, with every group expanded while it does.

The layer effects are all here, and each arrives showing something, since several draw nothing at their class
defaults:

| Group | Row | What it puts down |
| --- | --- | --- |
| Effects | Glass | glass with a little frost, so that it shows over a plain colour |
| Effects | Adjustment Layer | an adjustment layer turning what is behind it black and white |
| Effects | Stylize | a CRT effect |
| Components | Layer Style | a rect block with a drop shadow and a light stroke |
| Components | Layer Group | an isolated group at 60 % with two overlapping blocks in it |
| Components | Shape Blend Group | two circles that melt into one |
| Components | Mask Transition | a block part way through a wipe |
| Components | Holo Foil, Pointer Light, Tilt Card | a block to show it on |
| Modifiers | Warp, Text On Path | a text bent into an arc, a text round a circle |
| Modifiers | Jelly | a block |
| Shapes | Path | a curve |

Everything they set is an ordinary authored value. A post-process Blueprint is listed under Effects by its name without
its `BP_` and `_C`.

A project's own registered controls come after DreamGUI's categories, in the order they were registered. The category
names are in `DreamUIPaletteCategory` (`DreamUIControlRegistry.h`); an extension that compared
`FDreamUIControlDescriptor::Category` with "Controls", "Post Process", "Extensions" or a legacy category name reads them
from there instead.

## The details panel

A selected widget's categories run in the order a widget is put together: what makes it a layer (Layer), where it sits
(Slot, Canvas, Layout), what it is (Visual, Panel, Self Layout), how it acts (Appearance, Behavior), then Render Transform
and — collapsed — Perspective, Navigation, Accessibility and Localization. Is Enabled is in Behavior. The Transform and
anchor-data categories at the bottom, which repeated the Layout rows raw, are gone.

Where a property goes is said by `DreamGUI_` tags on it, and a property without them is shown under its own category, so
nothing a widget, a visual or a component can be given is ever missing from the panel; rows that apply only to some values
(a fill, a transition's colours) appear with the value. The tags, and how an extension's classes use them, are in
[Property meta tags](../guides/property-meta.md).

The **Layer** category gathers what makes a widget a layer, from wherever it lives:

| Row | What it edits |
| --- | --- |
| Opacity | the widget's `RenderOpacity` |
| Blend Mode | its visual's, when the visual is an element (an image, a text, a rect block...) |
| Clip To Layer Below | `bClipToBelow` |
| Mask | a shape to pick at once -- picking one makes the widget clip to its bounds, which the mask needs -- and the whole mask (`ClipMask`) under it |
| Layer Style, Layer Group | a switch each: on adds one to a widget that has none, as *Add Component* does; off turns it off and keeps its settings. *Edit...* opens its own properties |
| Group Mode | the layer group's isolation, while every selected widget has one |
| Effects | every layer effect on the widget: an eye that turns it off and on, and its name, which opens its properties |

None of these rows is anywhere else as well: the opacity used to be in Appearance, the mask and clipping to the layer
below under Appearance > Clipping, the blend mode among the visual's own rows.

### Editing controls

- **Blend modes** show as pictures: each mode's picture is a disc blended over hues and greys by the mode's own
  formula, and the menu lists all 27 in an image editor's order and groups (Normal is `Alpha`, Linear Dodge (Add) is
  `Additive`). Hovering a mode shows the widgets being edited drawn in it, until the pointer moves on or the menu closes;
  nothing is recorded or saved until a mode is clicked.
- **Angles** tagged `DreamGUI_Editor = "AngleWheel"` are a dial beside the number: drag it (Shift snaps to 15 degrees) or type.
  What follows a colon says which way the angle runs: nothing for an image editor's (0 at three o'clock,
  counter-clockwise), `AngleWheel:CW`, or `AngleWheel:TopCW` (a clock's). A layer style's effect with *Use Global Light* on edits the style's
  global light angle from its own row, and every effect sharing the global light turns with it; the row says *Global*.
  A drag is one undo step. The layer style's angles, the gradient overlay's, the glass's light, a mask transition's
  rotation and a text-on-path circle's start are dials.
- **Contours** are curves: the row shows the curve; its popup has the named shapes as pictures to pick and the curve to
  edit -- click to add a point, drag one out of the box to remove it, Alt+click or double-click to make a corner.
  Editing a named shape starts a custom curve from it.
- **Eases** (`EDreamTweenEase`) show their curves, in the row and in the menu.

These controls are registered for the whole editor, not for the designer's panel alone: any details panel that shows
such a property -- a data asset's, a dialog's, a structure shown on its own -- edits it with the same control.

A visual's own rows are ordered by what they do:

- A **Text** leads with Text, Font, Font Size, Font Style, Color, alignment, Overflow Type, Auto Wrap Text and Rich Text;
  the rest — Wrapping, Typography (the language picker included), Text Style / Paint, Rendering (override material,
  preset materials, mesh size, small-text raster and so on) — is in groups that start closed.
- An **Image** leads with its brush and ends with the raycast rows.
- A **Sprite**'s or **Texture**'s colour sits under its sprite or texture.

## Components

*Add Component* lists only the components the selected widget supports — it used to list every component class on
every widget, where most of them then did nothing:

| Component | Needs |
| --- | --- |
| a mesh modifier | a visual that builds a mesh (`UDreamVisualBatchMesh`) |
| `UISpriteSequencePlayer` | a Sprite visual |
| `UISpriteSheetTexturePlayer` | a Texture visual |
| `UITextHyperlink`, the text animation | a Text visual |
| `UDreamUMGWidgetInteraction` | a UMG Widget visual |
| `UDreamRetainerBox`, `UDreamInvalidationBox` | the widget's canvas (`UDreamCanvas`) first |
| a canvas, navigation, an animation component, a named slot, a content widget, a retainer or invalidation box | one per widget |
| a selectable (`UUISelectable` and kin), a scroll view (`UUIScrollView` and kin) | one kind per widget |
| a content widget | not on a widget with several children |

Paste, duplicate and component assets dropped on the panel follow the same rules; a refused drop says why. The rules
live in `FDreamUIComponentSupport` (module `DreamGUIEditor`): one registered for a class holds for every subclass of it,
Blueprint ones included. A project adds rules for its own components from its editor module's `StartupModule`, with
`RequireVisual`, `RequireComponent`, `OnePerWidget` or `AddRule`.

## The viewport toolbar

| Item | What it does |
| --- | --- |
| Screen Size | The device resolution previewed; *Flip Orientation* swaps width and height, *Show Resolution Guides* overlays common resolutions |
| Size Rule | *Custom*: the canvas is the resolution picked or typed; *Fill Screen*: the canvas is the viewport, one design unit per pixel; *Desired*: size the canvas to what the root's panel measures, for authoring a tooltip-sized widget at its own size |
| Zoom | *Zoom to Fit* frames the whole canvas (F frames the selection), *Zoom 1:1* is one design unit per screen pixel |
| Rulers, Guides, Layout Debug | Rulers; snapping guides; the selected widget's measure / arrange / slot / clipping diagnostics |
| Safe Zone | Draw the platform's title-safe area (a desktop shows one only with `r.DebugSafeZone.TitleRatio`) |
| Respect Locks | Switch it off to select and drag a locked widget without unlocking it |
| Designer Chrome | Switch off the canvas boundary, outlines, handles, guides and every other overlay to see the widget alone |
| Preview DPI | Shrink the canvas by the project's DPI curve, previewing the application scale Slate will really apply |
| Preview Language | Resolve the preview's text in another culture, like UMG's Localization Preview; switched off when the designer closes |
| Screen Space / Canvas Eye | Build the preview the way play builds it, as a screen-space overlay canvas (which a declared perspective needs); stand the 3D view's camera where the canvas's own virtual camera stands |

## Source files: classes backed by a .dui

A class's hierarchy can come from a `.dui` file. The *DreamUI Source* dropdown on the toolbar handles it:

| Entry | What it does |
| --- | --- |
| Create Source File... | Writes a starter `.dui` (a root plus one centred label), points the class at it and compiles. Offered only while the class has none |
| Set Source File... | Pick an existing `.dui` as the class's hierarchy, then recompile |
| Open in Default Editor | Open the file in whatever the system opens `.dui` files with |
| Show in Explorer | Select the file in the file browser |
| Reveal in VS Code | Open it in VS Code at the right place (also in the hierarchy's context menu, which goes to the node's line) |

A class can name a source file when it derives from `UDreamTextUserWidget`; the file is its class default
`SourceFile`. Doing the same from a script is in [Scripting](scripting.md).

### Converting a hand-built Blueprint

**Convert to Text** on the toolbar (shown only for a Blueprint that names no `.dui`), **Convert to Text (.dui)** in the
Content Browser's menu (several at once), or the `DreamUIConvertBlueprints` commandlet, turns a Blueprint whose
hierarchy was built in the designer into one authored in a `.dui`:

- The file is written by the same write-back the designer uses: the hierarchy, every value that differs from the
  default, the behaviours and panels, the bindings (`<-`) and routes (`->`), and every animation the asset keeps as
  `timeline Name external`. It goes to `DUI/<path under /Game>/<AssetName>.dui`, and never over a file that exists.
- Node ids are the compiler's names for the widgets, so the event graph, the variables and every binding keep working.
- What the file cannot hold is listed first -- a value with no spelling, a node or a binding with no line -- and the
  conversion goes ahead only when you confirm it (the commandlet: only with `-AllowLoss`). That loss is final.
- A backup of the asset, `<AssetName>_PreDui`, is made beside it before anything changes; delete it once you are happy.
- A Blueprint whose parent is `UDreamUserWidget` is reparented to `UDreamTextUserWidget` (its subclass: the graph is
  unaffected). A parent that is any other class not derived from `UDreamTextUserWidget` is refused -- make that C++ base
  derive from `UDreamTextUserWidget` first.
- After the compile the rebuilt tree is compared with the one it came from, and any difference is reported.

```text
UnrealEditor-Cmd.exe <project>.uproject -run=DreamUIConvertBlueprints -Path=/Game/UI [-DryRun] [-AllowLoss]
UnrealEditor-Cmd.exe <project>.uproject -run=DreamUIConvertBlueprints -Assets=/Game/UI/WBP_A,/Game/UI/WBP_B
```

`-DryRun` prints the report and changes nothing. The commandlet saves each converted Blueprint and its backup, and exits
non-zero when any Blueprint was skipped.

### The text is the only truth

Once a class names a `.dui`, **the file is the only truth about its hierarchy, and the designer is a graphical front
end for it.** That buys one place to read, one to diff and one to review. It costs the designer the ability to do
everything — and it tells you so before the edit, rather than letting the edit live until the next compile and then
quietly vanish.

**Values are written back.** Change a property and the write-back lands on **the line that holds the value** —
whatever the node's type is written as, inside a `@slot { … }` block when there is one, and on a node with no id
by the id made for it. The designer never writes over a file that changed on disk (DUI7002). Moving a widget on
the canvas is a value edit too: it recomputes the anchors, and `AnchorData.*` is written back; a turn is written as
`RelativeRotationEuler`, a scale as `RelativeScale`. Asset references (a font, a texture, a material) are written as
their quoted paths, lists, sets and maps as `[a, b]` and `{ key: value }`, and a reference to another widget of the
hierarchy as that node's id. A value a node takes from its **style's** `+ Component` line is written as an override on
the node -- a `+ Component { … }` line of its own -- and the shared style is left alone.

**Structure is written back too.** Creating, deleting, duplicating and pasting widgets, moving and reordering them,
renaming (written with `(was: OldId)`, so the compile carries what the old name owned), wrapping in a panel and
unwrapping, adding, removing and reordering behaviours, replacing a panel and the three class pickers (visual, panel,
self layout) are each written into the file before the compile that rebuilds the tree from it, and undo with it. An
edit the file cannot take is refused before anything changes, with the reason -- the template of a `for` or an `each`,
a widget an `if` shows, the inside of another file's component, a row renamed or given a `+` block. A row of a `rows`
table is removed, moved inside its table and duplicated as its line.

**Bindings and routes are lines.** The Bind control writes `Property <- Function()` (and *Create Binding* checks the line
before it makes the function); *Remove Binding* takes the line out. A bound property's value row stays read-only, and
its Bind menu offers **Unbind and Set to This Value**, which writes the value it shows in place of the binding. A
control that announces its value through `OnValueChangedBP` (a slider, a spin box, a toggle) offers **Two-Way Bind to
Variable**: the class's own variables and viewmodel members of exactly the type the event hands over, written as
`Value <-> Variable`.

**Property rows that cannot be written back are disabled**, with the reason:

| Reason | Example |
| --- | --- |
| The file writes it as a binding | `Text <- GetTitle()` — a literal would delete the binding |
| A loop made it | The N copies a `for` makes have no per-instance value to write |
| Outside the writable set | The write-back has no home for the property, so an edit would not survive a compile |
| The value has no `.dui` spelling | A reference to an object inside another file's hierarchy, or a value the language cannot spell. The preview would change, the file would not, and the change would be gone at the next compile |

The write-back also refuses a few cases out loud rather than writing them: a value that cannot be written as a literal
the language reads back (today, only non-finite floats) is DUI7003; a visibility an `if` decides, or a line nobody
wrote, is DUI7004. The codes are in [DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx), and what
each edit becomes in the text is in [Designer write-back](../language/write-back.md).

A class whose hierarchy is built in the designer and names no `.dui` has none of these limits.

## Events

The details panel's **Events** section lists the events the selected widget can raise. The `+` (*Add Event Handler*)
creates a custom event in the event graph with the right signature, **and** the route that names it — the same thing
as `OnClick -> HandleConfirm` in a `.dui`. When a handler exists, the same button opens it. *Remove Event Route*
takes the route away. On a class backed by a `.dui` the route is that line: written on the node's line, or in the
node's own `+ Component` block for a behaviour the node writes, in the same transaction as the handler, and taken out
by *Remove Event Route*. The handler stays in the event graph either way.

A route names a function on the **user widget** — the class the hierarchy compiles into — which is where UMG puts event
handling too. The compiler resolves every route into `UDreamWidgetBlueprint::EventBindings`, and
`UDreamUserWidget::BindEventBindings` attaches them at Initialize. It reaches both kinds of event this plugin has: the
`BlueprintAssignable` dynamic multicast delegates the controls declare, and the `FDreamUIEventDelegate` properties the
older `Interaction/` behaviours declare.

**Note:**

  `FDreamUIEventDelegate`'s own per-instance event list is legacy and read-only. Bindings saved in it still fire and can
  still be removed from the panel, but new ones are not authored there: such a binding calls a function on an arbitrary
  object with a literal argument, which UMG has no equivalent of and `.dui` has no syntax for. Opening a panel that holds
  one logs a warning naming it. Nothing is rewritten automatically — turning "call `Foo` on that behaviour with this
  value" into "call a handler on the user widget" would change what the game does — so re-author them as routes when you
  touch them.

## The animation editor

The animation editor is built on Sequencer. A `timeline` block in a `.dui` compiles into a `UDreamWidgetAnimation` on
the class; because **the file owns it**, every compile rebuilds it. On a class whose `.dui` the editor can write, the
animation editor opens it editable and **writes each edit back into the block**: a key's time and value, the ease
between two keys (read back from the curve the compile sampled), event keys, the duration, and a track added or
removed as a line written or taken out. A track whose keys did not change keeps its line byte for byte. A key Sequencer
adds with automatic tangents becomes linear, in the sequence as well, so what you see is what the file builds; a key
with tangents of its own, or stepped, has no spelling and is refused, with the way out. Renaming is refused ("rename it
in the `.dui`'s timeline block").

To hand an animation to Sequencer, make its block `external` — below, `Pulse` belongs to the file and `Celebrate` to
the asset:

```dui
class /Game/UI/WBP_Results

Widget Root {
    Text Title { Text = "Victory" }
}

timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Title.RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
}

timeline Celebrate external
```

`external` builds nothing. It is a manifest entry: the animation lives in the asset, Sequencer edits it freely, and the
file still lists it — which is what makes "what animations does this class have" answerable by reading the file.
Material-parameter tracks, hand-shaped curves and anything else a timeline cannot express belong in `external`. See
[Timelines](../language/timelines.md).

## The Content Browser

**The Add menu.** **DreamGUI Widget** is in the *Add* menu's top group, beside the engine's Blueprint Class and
Material. The **DreamGUI** submenu below it is in sections — Basic (DreamGUI Widget, Widget Animation), Fonts, Graphics,
Rich Text — each entry a short label with a one-line tooltip. The bitmap font, no longer developed, reads Legacy Bitmap
Font and points to the distance-field font. Asset type names, on tiles and in filters, are unchanged.

**Thumbnails.** A widget Blueprint's tile is the screen it authors: the thumbnail builds the widget the way the
designer's preview does, lays it out on the design canvas and draws it, as UMG draws its widget thumbnails. It is drawn
when an asset has no clean thumbnail and when it is saved, not on every frame the pointer rests on it, so a large screen
does not stall the browser. A thumbnail already saved in an asset (the old anchor wireframe) stays until the asset is
next changed and saved; a Blueprint with no compiled class still gets the wireframe.
