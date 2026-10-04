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

## New in 2.1

- **Gradient text.** A text's face, its outline and an overlay over the face can be painted with a gradient --
  linear, radial, conic, diamond or four-corner, written as CSS writes gradients -- on the whole text, on a rich-text
  run (`<gradient=Name>`), or from a shared preset (a gradient asset, or a CSS string in the project settings), and
  moved, turned or swept by a shimmer without laying the text out again (`FDreamTextPaint`, `UDreamTextPaintLibrary`).
- **Tab and the gamepad.** Tab and Shift+Tab walk a screen in hierarchy order (`TabIndex`, `bIsTabStop` and
  `TabNavigation` on every widget); modals, dimmed dialogs and popups that cycle keep them inside, a dropdown's list or a
  menu closes and lets Tab go on, and text fields commit and move on. Keys and the pad act on the focus, never on what
  the mouse hovers. A navigation scope says whether its screen takes menu or game input; the focus look shows only when
  keys or a pad moved the focus; the accept and back buttons are the platform's, read at run time, and the key tables are
  project settings; the shoulder buttons switch tabs.
- **Text.** Small text draws from hinted coverage glyphs on the device pixel grid, crisp as Slate's -- with an outline, a
  glow or an underlay too, its effects from the field, and inside a render layer once the layer holds still; colour
  emoji, and fallback faces by range, culture and presentation; line breaking follows the game's culture; long texts lay
  out again only where an edit touched them.
- **Shipping it.** `DreamGUI.Memory` prints what the fonts, atlases and canvases hold;
  [Docs/FontsAndPackaging.md](Docs/FontsAndPackaging.md) says how to ship fonts, colour emoji and ICU data; the test
  host has a packaged text smoke test, which the [release gate](#platforms) runs.

Everything that changed, version by version, is in [CHANGELOG.md](CHANGELOG.md); what to check when moving a project from
2.0 is in [Docs/Migration.md](Docs/Migration.md#from-20-to-21).

## Modules

The runtime is split into modules by layer. A module depends only on modules in the layers below its
own, never on a sibling in its layer, and `Tools/Tests/static_checks.py` fails an include that goes the
other way (rule `layering`). Every runtime module loads at `PostConfigInit`, as the core does, so its
types and `.dui` tags are in place before anything compiles.

```text
L4   DreamGUISamples
L3   DreamGUIControls      DreamGUIExtensions
L2   DreamGUIInput
L1   DreamGUI (core)
L0   DreamGUIRenderer      DreamTween
     ------------------------------------------------------------
     DreamGUIEditor, DreamGUIK2Nodes (uncooked only), DreamGUITests (editor only)
```

| Module | Layer | Holds |
| --- | --- | --- |
| `DreamGUIRenderer` | below the core | The view extension that draws DreamUI and the render command that draws a render-target canvas, its shaders, the vertex and index formats, the material proxies a canvas answers its parameters through, the post-process proxies with the screen reads and writes the effects share, and the stage timing behind `DreamUI.Stats`. It knows nothing of widgets: the core registers what it asks for |
| `DreamGUI` | core | Widgets, visuals, canvas batching, layout, text and `.dui`, animation, the event contracts, the render root that holds a canvas's sections for the renderer, and the PNG capture |
| `DreamGUIInput` | above the core | The input system: the event systems and their preset actors, the raycasters and input modules, the action router, navigation, drag and drop, tooltips and modals, the selectable base the controls are built on, and the game viewport client |
| `DreamGUIControls` | above the input system | The control library: the `Dream*` controls (button, toggle, slider, lists, dialog, tab view, ...), the `UI*` behaviours they are built from, the action bar, style sheets and the UMG interop |
| `DreamGUIExtensions` | above the input system | 2D lines, polygons and rings, the static-mesh visual, the retainer box and the render-target helpers, lyrics, the concrete mesh modifiers, and the background blur, pixelate and pixel sort effects |
| `DreamGUISamples` | above the controls | The showcase and the controls gallery |
| `DreamTween` | independent | Tweens |
| `DreamGUIEditor`, `DreamGUIK2Nodes` | uncooked only | The designer and the asset tools; the Blueprint nodes. Loaded by every process that runs uncooked content, a game started from the editor (`-game`, Standalone Game) included: an editor build drops a Blueprint's saved bytecode on load and rebuilds it from the Blueprint, and the widget Blueprint class and its compiler live here. Outside the editor only the compiler starts |
| `DreamGUITests` | editor | The automation suite |

C++ that uses a type from a split-off module adds that module to its `Build.cs`. Assets need nothing:
every type that moved still loads under its old name (see [below](#if-you-have-assets-authored-against-lgui--lexui-or-from-before-an-in-fork-rename)).

### C++ written against the single module

Besides the `Build.cs` line, a few includes and calls changed. Every other header kept its path.

| Include that was | Is now |
| --- | --- |
| `Core/DreamUIRender/*`, `Core/DreamUIMesh/DreamUIGizmoMesh.h`, `Core/DreamUIMeshVertex.h`, `Core/DreamUIMeshIndex.h`, `Core/DreamUIBlendMode.h`, `Core/DreamVisualPostProcessRenderProxy.h` | `DreamUIRender/` and the same file name |
| `Extensions/DreamGameViewportClient.h` | `Event/DreamGameViewportClient.h` |
| `Extensions/DreamUMGWidget.h`, `Extensions/DreamUMGWidgetInteraction.h` | `UMG/` and the same file name |
| `Core/DreamUIEachAdapter.h` | `Binding/DreamUIEachAdapter.h` |
| `Core/Components/DreamBackgroundBlur.h`, `Core/Components/DreamBackgroundPixelate.h`, `Core/Components/DreamPixelSort.h` | `Extensions/Effects/` and the same file name |

| Call that was | Is now |
| --- | --- |
| `UUITextInput::RouteCharacterInputToActiveInput` | `DreamUITextInputRouter::RouteViewportCharacter`, called [before the base class](#keyboard-layouts-give-dreamgui-the-game-viewport-client) |
| `UDreamCanvas::CalculateRenderScaledSize` | `FDreamUIRenderer::CalculateRenderScaledSize` |
| `DreamPixelSort::ResolveRegionSize` | `DreamUIPostProcessEffects::ResolvePixelSortRegionSize` (`DreamUIRender/DreamUIPostProcessEffects.h`) |
| `UDreamUIManagerWorldSubsystem`'s event-system registry and player interaction: `GetEventSystemByUserIndex`, `GetMapUserIndexToEventSystem`, `AddEventSystem`, `RemoveEventSystem`, `EnsureInteractionForPlayer`, `GetInteractionHost` | The same names on `UDreamUIInputSubsystem` (`Event/DreamUIInputSubsystem.h`); `UDreamUIInputSubsystem::Get(WorldContext)` finds it |
| `UDreamUIManagerWorldSubsystem::AddSelectable`, `RemoveSelectable` and `GetAllSelectableArray`, with `UUISelectable` | The same, with `UDreamUIBehaviour` |
| `UDreamGUISettings::DefaultStyleSheet` as a `UDreamUIStyleSheet` | A `TSoftObjectPtr<UDataAsset>`; `UDreamUIStyleSheet::GetProjectSheet()` does the cast |

What changed for C++ since the split — the material callback a visual overrode, the input and renderer
calls that went with the old code paths — is in [Docs/Migration.md](Docs/Migration.md#6-c-of-your-own).

The renderer logs to `LogDreamGUIRenderer`; `stat DreamGUI` still shows its counters.

## Install

Requires **Unreal Engine 5.8**. Clone into your project's `Plugins/` directory:

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

Regenerate project files and build. That is the whole install for a fresh project.

> [!IMPORTANT]
> **Engine 5.8, a launcher install included.** The plugin compiles against the engine's public headers
> only: the renderer reads the scene's depth through the public scene-texture API, and the static check
> `engine-private-path` fails any include path into `Runtime/Renderer/Private` or `Internal`.
>
> msdfgen, which the glyph rasteriser compiles into its own translation unit, used to need a source
> build: upstream generates its single-file copy rather than committing it, so a launcher install has
> only `Engine/Source/ThirdParty/msdfgen/msdfgen.tps`. The plugin now carries its
> own generated copy under `ThirdParty/` — see `ThirdParty/README.md`. A plain clone or a zip download
> builds it as-is; the `ThirdParty/msdfgen` submodule is only what `Tools/UpdateMsdfgen.ps1` regenerates
> that copy from.

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
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

**2. Keep your own viewport client.** Either derive it from `UDreamGameViewportClient` instead of
`UGameViewportClient`, or keep its base and hand the character to DreamGUI from its `InputChar`
override (module `DreamGUIInput`, header `Interaction/DreamUITextInputTarget.h`) — after the console,
and before the base class:

```cpp
bool UMyGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
    FString CharacterString;
    CharacterString += Character;
    // An open console takes every character.
    if (ViewportConsole && ViewportConsole->InputChar(FInputDeviceId::CreateFromInternalId(ControllerId), CharacterString))
    {
        return true;
    }
    // Before the base class: in a play-in-editor viewport it answers true for every character, so a
    // field asked after it never sees one there.
    if (!IgnoreInput() && DreamUITextInputRouter::RouteViewportCharacter(this, ControllerId, Character))
    {
        return true;
    }
    return Super::InputChar(InViewport, ControllerId, Character);
}
```

`DreamUITextInputRouter::RouteViewportCharacter` is the entire contract — it hands the character to
the field the typing player (the one `ControllerId` is) is editing, or, when no field takes it, to
what that player has focused as a key character, and returns whether either took it. From the first
character that arrives this way, the `FKey` table stops synthesising printable characters in that
world, so the two roads never double-type.

### Input in every input mode: the Slate input source

By default DreamGUI hears input through its preset event system actor's bindings on the player
controller, so it hears nothing in the engine's own UI-only input mode: `SetInputMode(FInputModeUIOnly())`
makes the game viewport ignore input, and the controller never sees it. Turn on **Project Settings →
Plugins → Dream GUI → Input → Use Slate Input Source** and DreamGUI hears the mouse, touch, keys and
sticks from Slate itself instead — an input pre-processor, ahead of the game viewport — in every input
mode. The preset actors stand down while it is on, so nothing arrives twice.

**Slate Input Consume Policy** decides what the UI keeps from the game: `Never` (the default; the game
hears everything, as it always has), `WhenOverUI` (a press, release or wheel turn over DreamGUI UI,
and any key the UI took) or `WhenHandled` (only a press on a widget that handles presses, and a key the
UI took). A key typed into a field being edited is always kept. The source is off by default for now,
and becomes the default in a later version.

### If you have assets authored against LGUI / LexUI, or from before an in-fork rename

The whole move — what to take out first, what loads by itself, what no longer exists, what to check
afterwards and what changed for C++ — is written up in [Docs/Migration.md](Docs/Migration.md). In short:

They reference the old class names and the old `/LGUI/` mount, so they need CoreRedirects — and
**the plugin ships them**: the `[CoreRedirects]` block in
[`Config/DefaultDreamGUI.ini`](./Config/DefaultDreamGUI.ini) is mounted as the plugin's own config
branch, and the engine applies every branch's redirects before the first asset loads. Nothing to copy.
It covers the LGUI/LexUI rename, the prefab-vocabulary rename that the class model replaced, the
control renames (`UIButtonComponent` → `UIButton` and its siblings), and the module split: a type that
moved out of the core into another of the plugin's runtime modules is still found under its old
`/Script/DreamGUI` name.

**If you copied the block into your project's `Config/DefaultEngine.ini` for an earlier version,
delete that copy.** Earlier versions shipped it as a template, `Config/DefaultEngine.ini`, on the
belief that a plugin's config is read too late for redirects; it is not. Two redirects for one old
name with different new names are an error, and the copy is older than the file that ships.

**Removed in 2.0.** These have no redirect, because there is nothing left to point at:

- the root Blueprints `WorldSpaceRoot_DreamRenderer`, `WorldSpaceRoot_UERenderer`, `ScreenSpaceRoot`
  and `DreamWorldSpaceRaycasterSource_Mouse`;
- `UDreamWidgetPresenterComponent` — the abstract base `UDreamWidgetPresenterComponentBase` stays, and
  `UDreamWorldWidgetComponent` is what you place now;
- the `UDreamWorldSpaceRaycasterBase`, `UDreamWorldSpaceRaycasterForWorldTrigger` and
  `UDreamWorldSpaceRaycasterSource` family — `UDreamWorldSpaceRaycaster` absorbed all of it;
- the plugin settings' `ScreenSpaceRootClass`, `WorldSpaceRootClass`, `WorldSpaceUERendererRootClass`,
  `WorldSpaceRaycasterSourceClass` and `bLegacyTouchPointerIds`;
- the console variables `r.DreamUI.MaterialWrappers` and `r.DreamUI.RTDrawer`, and the renderer
  behaviour they put back.

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

Forked from upstream `LexUI/5.7` at `765efeaf1` (2026-07-13), and the upstream commits up to `97d281376` (2026-07-21) were rebased in afterwards: `97d281376` is the upstream this code starts from, and its content is this repository's `5b48c42a`. Diff against that, not against `765efeaf1` or a merge base, or the rebased commits count as this fork's changes. Upstream fixes after it are ported by hand.

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

**Input**, per player. Each local player has its own pointers, focus and text target, so a split
screen's second player hovers, focuses and types on its own; the mouse, fingers and scripted pointers
have id ranges of their own and no longer share an id. An optional Slate input source hears input
before the viewport, so the UI keeps working in the engine's UI-only input mode.

**Rendering.** A canvas walks only the widgets that asked to change, patches in place the sections
whose geometry changed rather than rebuilding them, and answers its materials' parameters through
render-thread proxies instead of a material instance per draw call. A render-target canvas is drawn
by a render command of its own, so it updates whether or not anything renders its world. The screen
effects share one way to read, crop and write back the screen, on the render graph's textures.

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
> the redirects in [`Config/DefaultDreamGUI.ini`](./Config/DefaultDreamGUI.ini).

### Try it

[`Content/Samples/HelloDreamGUI.dui`](./Content/Samples/HelloDreamGUI.dui) is the smallest `.dui`
that is still a real screen — an anchored root, an overlay, a card, a vertical column of text. The
file's own header says what to do with it: make a Dream Widget Blueprint, point it at the file with
*Set Source File...* in the designer toolbar, compile, and add it to the viewport with
`UDreamUIBPLibrary::AddWidgetOfClassToViewport`. That is the whole path from text to a UI on screen,
and nothing else needs configuring — the screen root, the raycaster and the event system are created
on demand.

Copy it into your own project before editing it; a plugin update overwrites the copy in the plugin
folder. Everything the file can say is in the language reference,
[Docs/DuiLanguage.md](Docs/DuiLanguage.md).

### Components

A widget Blueprint is a node type in another `.dui`, and the instance's lines set its properties.
Name the class once with `use … as`, from its `.dui` or by its path, and write the name as the type:

```
use "UI/Components/Row.dui" as Row        // the class the file compiles into
use /Game/UI/WBP_Slider as Slider          // a class with no .dui

VerticalBox Root {
    Spacing = 29
    Row Audio { Label = "Audio" }
    Row Video { Label = "Video" }
}
```

A family of components is named in a library -- a file with styles, resources and `use … as` lines
and no root -- and every screen that uses it gets the names. Used under a namespace, the library's
names stay apart from the screen's own:

```
// UI/Library.dui
use "UI/Components/Row.dui" as Row
style Wide { AnchorData.SizeDelta = (1100, 48) }

// UI/Settings.dui
use "UI/Library.dui" as lib
VerticalBox Root {
    lib.Row Audio : lib.Wide { Label = "Audio" }
}
```

A component written in `.dui` declares what its hosts set and hear -- `props { Text Label }`,
`events { Picked(Number Index) }`, raised with `OnClick -> emit Picked(Index)` -- and opens slots a
host fills by nesting (`slot Rows default`) or by name (`slot Detail { … }`). The older spelling still
works: an `Asset` entry of a `resources` block names the class and `@Row Audio { … }` uses it, as does
the class's asset path written as the type.

Inside a panel, animate a component through its render transform (`RenderOffsetTo`,
`RenderScaleTo`, `RenderAngleTo`, `RenderTranslationTo`): the panel writes its anchored position
and size back on its next pass.

The whole language -- nodes, values, styles, `use`, components, bindings, `if`, `for` and `each`,
timelines, and every diagnostic -- is in [Docs/DuiLanguage.md](Docs/DuiLanguage.md).

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

### Seeing what it drew, and what it cost

Two console commands answer both questions without a debugger:

- `DreamUI.Capture [Directory]` writes a PNG of the world's viewport and of every root canvas that
  renders into a target — by default into a new folder under `Saved/DreamUI/Captures`. The same
  thing is a Blueprint library, `UDreamUICaptureLibrary`: `SaveViewportToPng`, `SaveCanvasToPng`,
  `SaveRenderTargetToPng`, `CaptureAll`. A canvas drawn straight onto the screen has no picture of
  its own; capture the viewport it is on.
- `DreamUI.Stats` prints what the frames since the last `DreamUI.Stats` cost, stage by stage — the
  UI manager's tick, canvas updates, batching, draw-call submission, the render thread's recording —
  and how many batches, vertices and bytes went to the GPU. Each stage is also a named scope in
  Unreal Insights (`DreamUI_*`).

A canvas answers the parameters it gives its own materials through render-thread proxies of the
material — it makes no material instance per draw call — and a render-target canvas is drawn by a
render command of its own, whether or not anything renders its world. The switches that put the old
ways back for a while are gone with them.

For a report of something drawn wrong or not drawn, `r.DreamUI.VerifyPartialPrepare 1` checks every
prepare a canvas makes from its last one against a prepare of every widget, and a difference is an
ensure that names the canvas. `r.DreamUI.VerifyKeptPointers 1` does the same for the objects DreamGUI
keeps instead of looking them up every frame -- a widget's canvas, a canvas's render layers, the UI
manager's canvases, an animated property's object: each use looks the object up as well, and a
disagreement is an ensure that says which. The test suite runs with both on.

`Tools/Bench` holds the benchmarks the performance work was measured with -- a screen of 5000 turning
buttons and a level of 2688 world-space panels -- in PIE or in a `-game` process, with scripts that
read the CSV profiles and traces they leave (`Tools/Bench/README.md`).

## Platforms

What is *claimed* and what has been *run* are different lists, so both are here. Only Win64 has been built or run.

| | Compiled | Run |
| --- | --- | --- |
| **Win64** | the editor; the game target in Development and Shipping | **the editor with the whole automation suite**; a packaged Development game, once the release gate has run the packaged text smoke test |
| Mac, Linux | never | never -- there is no machine or toolchain for them here |
| iOS, Android | never | never -- no SDK here, and never on a device |
| Consoles | no | not in any module's `PlatformAllowList` |
| Dedicated server | never -- no server target has been built | no |

**The release gate** is what a version goes through before it is tagged: BuildPlugin, which compiles the plugin for the
editor and for the game target in Development and Shipping, and the test host's packaged text smoke test, which cooks
and packages a Development game, runs it, and holds what it draws to the same build run on uncooked content
(`Tools/TestHost/README.md`). Until the gate has run for a version, no packaged game of that version has been run.

The per-module `PlatformAllowList` and the descriptor's `SupportedTargetPlatforms` name the five platforms the code is
written for, and a test keeps them in step with each other. They say what the build tools will try to compile and what
the cooker will cook, not that anyone has compiled, run or shipped on those platforms: the shaders, the engine's
third-party libraries the text uses and the C++ (which clang, unlike MSVC, builds with warnings as errors) have only
ever been built for Win64.

**Mobile** has code paths that have never run: touch is routed end to end (`BindTouch` → the standalone input module),
the virtual keyboard is implemented (`FPlatformApplicationMisc::RequiresVirtualKeyboard` → `ShowVirtualKeyboard`), and
the one live platform branch in the renderer flips culling for Android GLES. MSAA is *not* available on GLES, and the
renderer falls back rather than pretending -- see `AntiAliasingMethod`.

**Dedicated server.** There is no server target in this project, so the server configuration has never been compiled.
What has been done is the part that can be checked by reading: the `#if !UE_SERVER` blocks are balanced, and -- the thing
that actually breaks a server build -- *no member or function referenced outside a server guard is declared inside one*.
`UDreamUMGWidget` is the only class with `UE_SERVER` blocks, and every field they touch (`SlateWindow`, `SlateWidget`,
`WidgetRenderer`) is declared unconditionally. `WITH_FREETYPE=0` / `WITH_HARFBUZZ=0` are handled the same way: every
function the text path exposes is *defined* unconditionally with the body guarded, so nothing goes undefined at link time.
At run time a dedicated server draws nothing, and since 2.1 the UI manager does not start in its worlds
(`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`), a play-in-editor server included: no canvas is drawn, and no
renderer or paint rows are made. Six interaction subsystems already declined to exist in a server process
(`ShouldCreateSubsystem` → `!IsRunningDedicatedServer()`); in a play-in-editor server's worlds they find no manager and
stand down.

## Status

The automation suite is part of the plugin -- upstream had none. Run it with `Automation RunTests DreamGUI`, or by
preset with `Tools/Tests/Invoke-DreamGUITests.ps1`, which says how many tests ran; `Tools/Tests/README.md` describes the
presets. A packaged game is checked by the test host's packaged text smoke test (`Tools/TestHost/README.md`), which the
release gate runs (see [Platforms](#platforms)).

Known gaps:

- `LineHeightPercentage` and `WrapTextAt` are only reachable through a real font asset, so they are
  not covered by tests.
- A panel that offers a wrapping text no width -- a horizontal box measuring along its own axis -- is
  answered with the paragraph's one-line width; the text wraps once it is arranged narrower than that.
  (Offered a width, as a vertical box offers its own, a wrapping text answers with its height at that
  width.)
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
