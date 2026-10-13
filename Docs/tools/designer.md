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

A project's own registered controls come after DreamGUI's categories, in the order they were registered. The category
names are in `DreamUIPaletteCategory` (`DreamUIControlRegistry.h`); an extension that compared
`FDreamUIControlDescriptor::Category` with "Controls", "Post Process", "Extensions" or a legacy category name reads them
from there instead.

## The details panel

A selected widget's categories run in the order a widget is put together: where it sits (Slot, Canvas, Layout), what it
is (Visual, Panel, Self Layout), how it acts (Appearance, Behavior), then Render Transform and — collapsed —
Perspective, Navigation, Accessibility and Localization. Is Enabled is in Behavior. The Transform and anchor-data
categories at the bottom, which repeated the Layout rows raw, are gone.

A visual's own rows are ordered by what they do:

- A **Text** leads with Text, Font, Font Size, Font Style, Color, alignment, Overflow Type, Auto Wrap Text and Rich Text;
  the rest — Wrapping, Typography (the language picker included), Text Style / Paint, Rendering (override material,
  preset materials, blend mode, mesh size, small-text raster and so on) — is in groups that start closed.
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

### The text is the only truth

Once a class names a `.dui`, **the file is the only truth about its hierarchy, and the designer is a graphical front
end for it.** That buys one place to read, one to diff and one to review. It costs the designer the ability to do
everything — and it tells you so before the edit, rather than letting the edit live until the next compile and then
quietly vanish.

**Values are written back.** Change a property and the write-back lands on **the line that holds the value** —
whatever the node's type is written as, inside a `@slot { … }` block when there is one, and on a node with no id
by the id made for it. The designer never writes over a file that changed on disk (DUI7002). Moving a widget on
the canvas is a value edit too: it recomputes the anchors, and `AnchorData.*` is written back.

**Structure belongs to the file.** Creating, deleting, moving in the tree, renaming, duplicating and pasting
widgets, adding, removing or reordering behaviours, replacing a panel — on a class backed by a `.dui` each of these
is refused, with a message that names the file: *structure lives in the text; edit the file to change it; the
designer edits values.* The rotate and scale gizmos are withheld too, since the write-back carries `AnchorData.*`
and the language has no spelling for a relative rotation or scale.

**Property rows that cannot be written back are disabled**, with the reason:

| Reason | Example |
| --- | --- |
| The file writes it as a binding | `Text <- GetTitle()` — a literal would delete the binding |
| A loop made it | The N copies a `for` makes have no per-instance value to write |
| Outside the writable set | The write-back has no home for the property, so an edit would not survive a compile |
| The value has no `.dui` spelling | In practice, object references: a font, a texture, a material. Pick a new font in the panel and the preview changes, the file does not, and the change is gone at the next compile |

The write-back also refuses a few cases out loud rather than writing them: a value that cannot be written as a literal
the language reads back (today, only non-finite floats) is DUI7003; a visibility an `if` decides, or a shorthand that
cannot take the change, is DUI7004; a value that comes from a `+ Component` line of the node's style is DUI7005 —
the style is shared, and writing there would move every node that wears it. The codes are in
[DUI7xxx](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx).

A class whose hierarchy is built in the designer and names no `.dui` has none of these limits.

## Events

The details panel's **Events** section lists the events the selected widget can raise. The `+` (*Add Event Handler*)
creates a custom event in the event graph with the right signature, **and** the route that names it — the same thing
as `OnClick -> HandleConfirm` in a `.dui`. When a handler exists, the same button opens it. *Remove Event Route*
takes the route away.

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
the class; because **the file owns it**, every compile rebuilds it, so the animation editor opens it **read-only**, says
why in the log, and refuses to rename it ("rename it in the `.dui`'s timeline block").

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
