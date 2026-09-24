<p align="center">
  <img width="112" src="./Resources/Icon128.png" alt="DreamGUI">
</p>

<h1 align="center">DreamGUI</h1>

<p align="center">A 3D UI system for Unreal Engine 5.8 — widgets that live in the world, widget Blueprints you subclass, and a designer built to feel like UMG's.</p>

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
- **A tree that is a class.** A UI tree is authored in a widget Blueprint (`UDreamWidgetBlueprint`)
  and reused by subclassing and by nesting one widget class inside another, the way UMG does it.
- **Per-widget perspective.** A widget can establish a perspective its subtree is foreshortened
  into, the way CSS `perspective` works.

It costs you Slate's ecosystem: none of UMG's widgets, styles or bindings apply.

## Install

Requires **Unreal Engine 5.8**. Clone into your project's `Plugins/` directory:

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

Regenerate project files and build. That is the whole install for a fresh project.

> [!IMPORTANT]
> **A source build of the engine is required**, not a launcher install. `DreamGUI.Build.cs` adds
> `Engine/Source/Runtime/Renderer/Private`, `Runtime/Renderer/Internal` and `Engine/Source` itself to
> its private include paths, for `SceneRendering.h`, `ScenePrivate.h`, `SceneTextures.h` and the
> single-file `ThirdParty/msdfgen/msdfgen.cpp` that the glyph rasteriser compiles. A binary engine
> ships none of those, and the failure is a missing-header compile error rather than anything that
> names this requirement.

### Keyboard layouts: give DreamGUI the game viewport client

A DreamGUI text field is not a Slate widget, so it is never on the keyboard focus path, and the
engine's only landing place for a platform **character** in a game is the virtual
`UGameViewportClient::InputChar` — which has no delegate to subscribe to. Without an owner for that
function, `UUITextInput` falls back to its own `FKey` → character table, and that table is only
correct on **US QWERTY**: AZERTY, QWERTZ, Dvorak, Cyrillic, dead keys and AltGr all type the wrong
character. (IME users are unaffected — composition text arrives through TSF, not through the table.)

Pick whichever of these fits the project. The field logs one warning the first time it is edited if
neither is in place.

**1. Use the plugin's viewport client.** In `Config/DefaultEngine.ini`:

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUI.DreamGameViewportClient
```

**2. Keep your own viewport client.** Either derive it from `UDreamGameViewportClient` instead of
`UGameViewportClient`, or keep its base and add one line to its `InputChar` override:

```cpp
bool UMyGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
    if (Super::InputChar(InViewport, ControllerId, Character)) { return true; }
    return UUITextInput::RouteCharacterInputToActiveInput(Character);
}
```

`RouteCharacterInputToActiveInput` is the entire contract — it hands the character to whichever
field currently owns the keyboard and returns whether one took it. From the first character that
arrives this way, the `FKey` table stops synthesising printable characters altogether, so the two
roads never double-type.

### If you have assets authored against LGUI / LexUI, or from before an in-fork rename

They reference the old class names and the old `/LGUI/` mount, so they need CoreRedirects — and
**the engine only reads those from the project's config**. A plugin's own config is not consulted
for them: `Config/DefaultEngine.ini` here is a template to copy, and a plugin's
`Config/Default<PluginName>.ini` is mounted after the redirects have already been read, which is why
none live there any more.

Copy the `[CoreRedirects]` block from
[`Config/DefaultEngine.ini`](./Config/DefaultEngine.ini) into your project's
`Config/DefaultEngine.ini`. It covers the LGUI/LexUI rename, the prefab-vocabulary rename that the
class model replaced, and the control renames (`UIButtonComponent` → `UIButton` and its siblings).

Skip this if you are starting fresh.

**Removed in this version.** These have no redirect, because there is nothing left to point at:

- the root Blueprints `WorldSpaceRoot_DreamRenderer`, `WorldSpaceRoot_UERenderer`, `ScreenSpaceRoot`
  and `DreamWorldSpaceRaycasterSource_Mouse`;
- `UDreamWidgetPresenterComponent` — the abstract base `UDreamWidgetPresenterComponentBase` stays, and
  `UDreamWorldWidgetComponent` is what you place now;
- the `UDreamWorldSpaceRaycasterBase`, `UDreamWorldSpaceRaycasterForWorldTrigger` and
  `UDreamWorldSpaceRaycasterSource` family — `UDreamWorldSpaceRaycaster` absorbed all of it;
- the plugin settings' `ScreenSpaceRootClass`, `WorldSpaceRootClass`, `WorldSpaceUERendererRootClass`
  and `WorldSpaceRaycasterSourceClass`.

A level that still holds one of those Blueprints drops that actor on load — its class no longer
resolves, so the whole export is discarded and a warning is logged naming it. Nothing is left behind
to fix up: drag the widget Blueprint into the level again.

## How it differs from UMG

Worth knowing before you commit to either, because the difference is structural rather than
cosmetic.

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| Widget | `SWidget`, retained-mode Slate | `UObject` in a component-like tree |
| Sizing | Content-sized: a widget's size **is** its desired size | Box-first: you author a rect, content is arranged inside it |
| Text | The box grows to the text | The text is aligned in the box, and may overflow it |
| Placement | Slot-relative | Anchors + pivot, resolution-independent |
| Reuse | Widget Blueprint subclassing | Widget Blueprint subclassing, plus named slots for content |
| In-world | `WidgetComponent`, a rendered quad | A first-class render mode |

The text difference is the one that surprises people. In UMG a `TextBlock` cannot overflow, because
its box is derived from the text; you control wrapping instead. Here the rect is authored, so text
can overflow it, and you get controls UMG has no need for — `Margin`, `LineHeightPercentage`,
`WrapTextAt` and **Best Fit** (shrink the font until it fits, which neither UMG nor Slate offers).

### Coming from UMG: the names are the same

The structure differs; the vocabulary deliberately does not. A control here answers to the name its
UMG counterpart uses, with the meaning it has there: `ScrollWidgetIntoView` takes a destination and a
padding, a scroll box has `bFrontPadScrolling` and `WheelScrollMultiplier`, a spin box has
`MinSliderValue` and `ClearMaxValue`, a panel slot has `Nudge` and `bForceNewLine`, every widget has
`SetIsEnabled`, `SetRenderShear` and a `FlowDirectionPreference`. Every knob the details panel can
turn is one a Blueprint can turn at runtime, through a setter that pushes the change instead of
writing a field nothing reads again.

Where a name could not be kept, that is written down rather than left to be discovered.
`Resources/UMGParity` holds one table per UMG class -- 58 of them, some 960 Blueprint-facing members
-- and each row says one of three things: *adopt* (same name, same meaning), *map* (here under
another name, or on another type, and which), or *reject* (deliberately absent, with the reason:
there is no immediate-mode paint context to draw into, a rect block has no per-instance material,
and so on). The automation suite holds those tables against UMG's own reflection, in both
directions: a member the engine gains in an upgrade turns up as a row that does not exist, and a row
naming something this plugin has since renamed turns up as a name that does not resolve.

Two differences are worth knowing in advance. A new knob defaults to **what the control already
did**, not to UMG's default, so that existing content does not move -- `ScrollWhenFocusChanges` is
`AnimatedScroll` here and `NoScroll` there, and the tables record each such case. And appearance
lives in a control's style struct while behaviour lives on the control, so a few UMG properties are
one level down: `EntrySpacing` is `Style.RowSpacing`.

`Docs/Reference` is the property and function reference, one page per class, each ending with that
class's UMG comparison. It is printed from reflection rather than written, so it cannot fall behind
the headers:

```
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

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

**The designer**, reviewed against UMG's widget designer. Viewport picking is by widget *rect*
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

## Widget Blueprints

A UI tree is a **class**, not an asset you instance. Authoring one gives you a
`UDreamWidgetBlueprint` whose generated class is a `UDreamUserWidget`; you subclass it, drop it
inside another tree, and bind to its named children by name — the same shape as UMG's widget
Blueprints, compiled by DreamGUI's own `FDreamWidgetBlueprintCompilerContext`.

The designer edits a preview instance of that class and writes back to the class, so there is no
apply step and no per-instance override list to reconcile. Content a parent supplies to a child goes
through `UDreamNamedSlotHost`.

> [!NOTE]
> The prefab asset model this forked from is gone, along with `SavePrefab`, `Apply`,
> `ClearLoadedPrefab` and *Save on Apply*. Assets saved against the old class names are covered by
> the redirects in [`Config/DefaultEngine.ini`](./Config/DefaultEngine.ini).

### Try it

[`Content/Samples/HelloDreamGUI.dui`](./Content/Samples/HelloDreamGUI.dui) is the smallest `.dui`
that is still a real screen — an anchored root, an overlay, a card, a vertical column of text. The
file's own header says what to do with it: make a Dream Widget Blueprint, point it at the file with
*Pick Text Source* in the designer toolbar, compile, and add it to the viewport with
`UDreamUIBPLibrary::AddWidgetOfClassToViewport`. That is the whole path from text to a UI on screen,
and nothing else needs configuring — the screen root, the raycaster and the event system are created
on demand.

Copy it into your own project before editing it; a plugin update overwrites the copy in the plugin
folder.

### In the world

The same class can be a surface in the level instead of a layer on the screen. Drag a widget
Blueprint from the Content Browser into a level, or place a **DreamUI World Widget Actor** from the
Place Actors panel: either way you get an `ADreamWorldWidgetActor`, whose entire content is a single
`UDreamWorldWidgetComponent`. Move it, rotate it and attach it like any other scene component.

The component hosts the tree; it does not redefine it. The Blueprint's own root Canvas remains the
truth about how the UI is built; the component writes render mode, sort order and trace channel down
onto it and leaves the rest alone.

- **`WidgetClass`** — the widget Blueprint class to load.
- **`Backend`** — `DreamUIRenderer` draws the tree with DreamGUI's own renderer: flat, unlit and
  untouched by post process. `UERenderer` sends it through the engine's pipeline instead, so it takes
  post process and depth like any other mesh in the level.
- **`bUseDesignSize`** / **`DrawSize`** — the size the tree lands at. On by default, following the
  size the Blueprint was designed at; turn it off to give this actor a size of its own.
- **`Pivot`** — where the actor's origin sits within that rectangle.
- **`SortOrder`** — order among world-space canvases.
- **`TraceChannel`** — the channel this canvas answers on. A raycaster only sees canvases whose
  channel matches its own.

Interaction needs no setup. On `BeginPlay` the component asks for an event system and a
`UDreamWorldSpaceRaycaster` for each local player and supplies whichever is missing, so pressing Play
is enough to click a button hanging in the world. The raycaster points either from the cursor or from
the middle of the screen (`PointerSource`), and `bOccludeByWorld` makes solid geometry block a click
the way it blocks a line trace. It is on by default: whatever blocks the raycaster's `TraceChannel`
(Visibility) stops the pointer, and the actor it hits is handed the pointer's events through the
pointer interfaces, which is also how a render-target surface on a mesh is clicked. To click through
walls instead, untick it on a raycaster of your own, or call `SetOccludeByWorld(false)` on the
player's. Put a raycaster of your own on any actor with the same user index and nothing is added on
top of it — the test is for one that exists, not for one this plugin made.

From code it is the two calls that were already there: `ConstructWidget`, then
`AttachWidgetToSceneComponent` on whatever component should carry the tree.

`ADreamWorldWidgetActor` can be possessed by a Level Sequence directly, and `WidgetOpacity`,
`WidgetOffset` and `bWidgetVisible` on the component are keyable from it.

### Animation, in the file

A `timeline` block is an animation the language owns:

```
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale        : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}
```

One line per track: a path of node ids, the property it drives, and the keys. A line with no path at
all (`RenderScale : ...`) drives the widget the animation lives on. The path is the same display-name
path an animation binding already resolves through; the values are spelled the way they are
everywhere else.

**What a track may drive is exactly what the animation editor offers**: a property marked `Interp`,
named either by its own name (`RenderScale`, `Color`) or by the label on its row (`Width`,
`Height` -- those are the `Animatable*` mirrors that exist so the anchor block can be keyed, since
a struct has no property track). One list, so a line that compiles is a track you can see. Note the
asymmetry with assignment, which is deliberate: `Width = 400` is still DUI4001 pointing at
`AnchorData.SizeDelta`, because an assignment writes a property and a timeline drives a track.

Easing is a **name** out of `EDreamTweenEase` -- one word list for the whole plugin -- never a
tangent quadruple: tangents are stored data, and a text form that expressed them would be
unwritable by hand and lossy to read back. `@<time> -> Name` is a key on the block's event track,
broadcast through the component's `OnAnimationEvent`.

The block compiles into a `UDreamWidgetAnimation` in the root's animation component, gets its class
member variable like any other animation, and plays through the same entry points. Because the FILE
owns it, every compile rebuilds it and **the animation editor opens it read-only** -- edit the
`.dui`, or hand the animation to Sequencer for good:

```
timeline Celebrate external
```

`external` builds nothing. It is a manifest entry: the animation lives in the asset, Sequencer edits
it freely, and the file still lists it -- which is what makes "what animations does this class have"
answerable by reading the file. Material-parameter tracks, hand-shaped curves and anything else layer
one cannot express stay `external` by design; a compile warns when an animation is not listed, and
when a listed one does not exist.

### Handling events

One mechanism, spelled the same way everywhere: a **route**.

```
Confirm : UIButton {
    OnClick -> HandleConfirm
}
```

`EventName -> Handler` names a function on the **user widget** — the class the tree compiles into —
which is where UMG puts event handling too. The compiler resolves every route into
`UDreamWidgetBlueprint::EventBindings` and `UDreamUserWidget::BindEventBindings` attaches them at
Initialize. In the designer the same thing is the *Events* section of the details panel: the `+`
creates a custom event with the right signature and the route that names it.

A route reaches both kinds of event this plugin has — the `BlueprintAssignable` dynamic multicast
delegates the `Controls/` family declares, and the `FDreamUIEventDelegate` properties the older
`Interaction/` behaviours declare.

> [!NOTE]
> **`FDreamUIEventDelegate`'s own per-instance event list is legacy and read-only.** Bindings saved
> in it still fire, and can still be removed from the panel, but new ones are not authored there: a
> binding of that kind calls a function on an arbitrary object with a literal argument, which UMG has
> no equivalent of and the `.dui` has no syntax for. Opening a panel that holds one logs a warning
> naming it. Nothing is rewritten automatically — turning "call `Foo` on that behaviour with this
> value" into "call a handler on the user widget" would change what the game does — so re-author
> those as routes when you touch them.
>
> **Asset format**, all additive and back-compatible: `FDreamUIEventDelegateData` gained
> `HelperComponentIndex` (a behaviour's position in the widget's component array, which is now the key
> — `HelperComponentName` is still read for assets saved before it and the position is recorded the
> first time that name resolves), and `StructValue` + `StructValueType` for the new `Struct`
> parameter type, which carries any USTRUCT as exported text. Older assets load unchanged.

## Platforms

What is *claimed* and what has been *run* are different lists, so both are here.

| | Builds | Verified |
| --- | --- | --- |
| **Win64** | yes | **yes** — the editor and the whole automation suite |
| Mac, Linux | yes | no — no machine here to run them on |
| iOS, Android | yes | no — never run on a device |
| Dedicated server | see below | no — this project has no server target to build |

The per-module `PlatformAllowList` and the descriptor's `SupportedTargetPlatforms` name the five
platforms the code is written for, and they are kept in step with each other by a test. They are a
statement about what compiles and what gets cooked, not a claim that anyone has shipped on them.

**Mobile** is better supported than "untested" suggests, and worse than "supported" would: touch is
routed end to end (`BindTouch` → the standalone input module), the virtual keyboard is implemented
(`FPlatformApplicationMisc::RequiresVirtualKeyboard` → `ShowVirtualKeyboard`), and the one live
platform branch in the renderer flips culling for Android GLES. MSAA is *not* available on GLES, and
the renderer now falls back rather than pretending — see `AntiAliasingMethod`.

**Dedicated server.** There is no server target in this project, so the honest statement is that the
server configuration has never been compiled. What has been done is the part that can be checked by
reading: `Config/DefaultEngine.ini`-style guesses are not involved, the `#if !UE_SERVER` blocks are
balanced, and — the thing that actually breaks a server build — *no member or function referenced
outside a server guard is declared inside one*. `UDreamUMGWidget` is the only class with `UE_SERVER`
blocks, and every field they touch (`SlateWindow`, `SlateWidget`, `WidgetRenderer`) is declared
unconditionally. `WITH_FREETYPE=0` / `WITH_HARFBUZZ=0` are handled the same way: every function the
text path exposes is *defined* unconditionally with the body guarded, so nothing goes undefined at
link time. Six interaction subsystems already decline to exist on a server
(`ShouldCreateSubsystem` → `!IsRunningDedicatedServer()`).

## Status

1112 automation tests — `Automation RunTests DreamGUI`. There were none before this fork.

Known gaps:

- `LineHeightPercentage` and `WrapTextAt` are only reachable through a real font asset, so they are
  not covered by tests.
- One content asset still carries `Lex` in its name
  (`Content/Blueprints/LexEventSystemActor_EnhancedInput`). Renaming a `.uasset` file does not rename
  the object inside it, so only an editor-side rename can change it; the code points at what is
  actually on disk.
- The editor work is verified by tests, not by eye. Expect rough edges in the designer.

## License

MIT — see [LICENSE](./LICENSE).

Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu

Substantial portions of this software remain the work of Lex Liu and are used under the MIT terms
of the [original project](https://github.com/liufei2008/LGUI). The MIT notice must travel with any
copy or substantial portion of this code, including yours.
