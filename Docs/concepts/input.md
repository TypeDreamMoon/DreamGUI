---
title: Input
description: The input system — event systems and preset actors, raycasters and input modules, each player's own pointers, focus and text target, the action router, navigation (Tab, navigation scopes, the focus look, the platform's accept and back, shoulder buttons switching tabs), popups, modals, tooltips and drag and drop, text input and keyboard layouts, and the Slate input source.
---

Input lives in the `DreamGUIInput` module, above the core. The one thing to know first: **input is per player**. Each
local player has its own pointers, focus and text target, so a split screen's second player hovers, focuses and types on
its own.

## The pieces

| Piece | Class | Does |
| --- | --- | --- |
| Input subsystem | `UDreamUIInputSubsystem` | One per world. Owns each local player's input (`UDreamUIInputUser`) and runs every player's frame, in player order, in `TG_PostPhysics` and while paused — after every player controller has run its input in `TG_PrePhysics`, and before the UI manager lays out and draws |
| Event system | `UDreamEventSystem` | One player's input as a level places it and a Blueprint reaches it: its details panel is that player's settings, its functions read and change the player's input, its events relay the player's |
| Preset actors | `ADreamEventSystemActor`, `ADreamStandaloneInputEventSystemActor`, `ADreamEnhancedInputEventSystemActor` | Carry an event system and the input bindings; spawned from `UDreamGUISettings::EventSystemActorClass` when none is placed |
| Input modules | `UDreamPointerInputModule`, `UDreamStandaloneInputModule` | Turn keys, pointers and navigation into events on widgets |
| Raycasters | `UDreamScreenSpaceRaycaster`, `UDreamWorldSpaceRaycaster` | Screen-space and world-space hit testing, one per player (world space is on [World-space UI](world-space.md)) |
| Action router | `UDreamUIActionRouter` | Which screen hears which key |
| Navigation stack | `UDreamUINavigationStack` | The stack of navigation scopes |
| Popups, modals, tooltips, drag and drop, virtual cursor | `UDreamUIPopupLayer`, `UDreamUIModalSubsystem`, `UDreamUITooltipSubsystem`, `UDreamUIDragDropSubsystem`, `UDreamUIVirtualCursorSubsystem` | Below |

All of it is made on demand: the screen root, the raycaster and the event system exist the first time they are needed,
and nothing has to be placed in a level.

## Pointers, focus and events

- **Pointer ids have ranges.** The mouse (and the virtual cursor that stands in for it) is `0`, a finger is `100` plus its
  index, and ids a script makes up start at `1000`. The first finger and the mouse used to be the same pointer and fought
  over hover and selection; to read a touch pointer by finger index, ask `DreamUIPointerIds::ForTouch`.
- **A click** needs its release on the pressed widget, and a release goes to whoever took the press.
- **Bubbling**: an event goes up the parent chain from the widget hit until a handler returns false.
  `bAllowEventBubbleUp` on `UDreamUserWidget` is on by default.
- **Focus**: `SetFocus`, `SetKeyboardFocus`, `SetUserFocus`, `ClearKeyboardFocus`, `HasKeyboardFocus`, `HasUserFocus` and
  `HasFocusedDescendants` all ask on behalf of the widget's **owning player** (`GetOwningPlayer`, `GetOwningPlayerIndex`)
  — the nearest user widget above it owns it, and a tree with no user widget in it belongs to the first local player.
  Turning `bIsFocusable` off gives up any focus the widget holds.
- **Pause**: the Enhanced Input preset binds the actions it is given, as they are. While the game is paused and
  `UDreamUISettings::bScreenSpaceUIAffectByGamePause` is set, the preset drops what arrives; for a paused frame's click to
  arrive at all its action must trigger while paused — the shipped `IA_*` actions do; set `bTriggerWhenPaused` on actions of
  your own.
- **Cursor**: a hovered widget can drive the hardware cursor (`Cursor`; `Default` means "no opinion" and lets what is
  underneath decide); `ResetCursor` goes back to no opinion.

## Raycasters and hit shapes

Hit testing is done by "raycasters": a base class and four kinds of hit, with **the hit shape decided by the visual's
`RaycastType`** (`Rect` / `Mesh` / `VisiblePixel` / `Custom`).

⚠️ **Do not guess the module of any of them**: the base `UDreamBaseRaycaster`, the sector `UDreamRingSectorRaycast` and the
custom `UDreamVisualCustomRaycast` are in the **core `DreamGUI` module**; the screen- and world-space raycasters are in
`DreamGUIInput`; the visible-pixel subclass is in `DreamGUIExtensions`.

**The base** (a `UCLASS(Abstract)` `UActorComponent`) splits hit testing into three pure virtuals: `GenerateRay` (make the
ray), `Raycast` (the hit test) and `ShouldStartDrag` (whether this press becomes a drag); plus `IsWithinDoubleClickDistance`
(`true` by default; the screen and world raycasters measure it by the drag threshold). It carries `UserIndex` (which player)
and `PointerID` (`-1` applies to every pointer).
The base runs a hit test in three stages: `GatherRaycastCandidates` (a coarse pass that **can run on other threads**) →
`PrepareRaycastTree` (game thread; merges and solves the tree) → `RaycastCandidates` (the exact test, sorted **in draw
order**).

**Screen-space raycaster** defaults:

| Property | Default |
| --- | --- |
| `RayLength` | 100000 |
| `DragThreshold` | 5 (**measured in the target's local space**) |
| `bHoldToDrag` / `HoldToDragTime` | false / 0.5 |

⭐ "Moved far enough to be a drag" and "too far apart to be a double click" are **the same distance**.

**World-space raycasters are one per player, not one per panel** — the ray is aimed from that player's view, and which
canvases it can hit is answered by the manager (world-space roots whose `TraceChannel` matches), so **it never needs to be
told about panels being added, removed or moved**. `PointerSource` sets the aim point (`Mouse` / `ScreenCenter`, the latter
being a first-person crosshair); `bOccludeByWorld` is on by default, and **the nearest blocking hit is both the occluder and
the target**.

**The sector raycaster** (`UDreamRingSectorRaycast`) hits a **ring sector**, not the rect it was drawn into — every wedge of
a ring menu is a ring-sized square, and under the default `Rect` hit **the topmost one answers for the whole circle**.
Angles run **clockwise from twelve o'clock**; an `OuterRadius` of `0` means **unbounded** (a weapon wheel is exactly that).

**Visible pixel** (`UDreamVisualCustomRaycast_VisiblePixel`) decides a hit by the pixel under the hit point
(`VisibilityThreshold` defaults to `0.1`, `PixelChannel` to `3`).
⚠️ It only supports visuals whose **pixels can be read from a texture** — a `UITexture` on a `Texture2D` is perfect; **a
`UISprite` packed into a dynamic atlas is not**; **a `UIText` on a dynamic font is not** (the plugin's built-in fonts are all
dynamic). Where it is not supported it falls back to the geometry hit.

## Event data

All four are `UObject`s (Blueprint types), all derived from `UDreamBaseEventData`:

| Class | Answers |
| --- | --- |
| `UDreamBaseEventData` | The base (`SelectedComponent`, `EventType` — the latter an `EDreamUIPointerEventType`) |
| `UDreamPointerEventData` | **Where** — pointer and press positions, the enter and hover stacks, world point and normal, scroll axis, drag state, plus three timestamps and the click count |
| `UDreamKeyEventData` | **Which key, which chord, whose** — `KeyEventType`, `Key`, four chord bits, `AnalogValue`, `bHandled`, `FocusedWidget` |
| `UDreamGestureEventData` | **What the gesture concluded** — `GestureType` (`Pinch` / `Swipe`) and its components |

⭐ **All three timestamps are on the "pointer clock"** — the world's real time, which **does not stop for a pause or
stretch with time dilation**. UI must still work while the game is paused: a hold in a pause menu is still a long press,
and two clicks within a second are still a double click. Compare against `GetPointerClockSeconds`, **never the game
clock**.

**`UDreamKeyEventData`'s bubbling switch is called `bHandled`, not `EventType`**: the base already has an `EventType`
holding pointer vocabulary, and UHT does not allow shadowing; the shape is wrong too — a pointer event asks "may this keep
bubbling", a key asks "did anyone take it".

**A gesture is not a pointer**: it has no pressed widget, no drag and no hover — it is a conclusion drawn from **pointers
that have already dispatched their own events**, but it still goes down the same dispatch path.

## The action router

"Which screen hears which key" used to be a static array of `FKey`s in the input actor's cpp: a project could not add an
action, rebind one, or draw a prompt for one without editing the plugin. Now bindings live and die with the screen that
registered them, and **only the screen on top** is offered the key — which is what makes Delete mean the dialog's delete
and not the list's while that dialog is open.

- `UDreamUIActionRouter::RegisterAction(Scope, Action, Callback, UserIndex, bDisplayInActionBar)` registers one, the
  action being a data-table row; `UnregisterAction` removes it; `GetDisplayBindings` lists them for a prompt bar, already
  resolved for the device in use; `GetHoldProgress` reads a hold.
- The router ticks, and ticks while paused: hold-to-confirm ("hold to quit", "hold to restart") lives on pause menus, and a
  hold has to fire when the time is up rather than when the player lets go.
- The `UDreamUIActionTrigger` behaviour, put beside a button, says "this button is also what Confirm does" (CommonUI's
  `TriggeringInputAction`), so a screen's Confirm key and its Confirm button are one thing and cannot drift apart. It is a
  behaviour rather than a field on the button, so it works for any widget.
- The control library's `UDreamUIActionBar` is the row of "A: Confirm  B: Back" prompts along the bottom of a screen. It
  reads the router rather than being told what to show, so it cannot drift out of step with what the keys do, and it
  rebuilds only when bindings come or go, the player switches device, or picks up another model of pad.
- Bindings get every key first.

## Navigation

### Tab and Shift+Tab

With no Ctrl, Alt or Cmd held, Tab and Shift+Tab walk in **hierarchy order** (depth first, siblings by `TabIndex`). Three
properties on every widget shape the walk:

| Property | Does |
| --- | --- |
| `bIsTabStop` | Whether Tab stops here, when it is focusable and can be navigated to. Off leaves it to the arrows, the pad, a pointer and code — a browser's `tabindex="-1"` |
| `TabIndex` | Its place among its siblings: lower first, equal ones in hierarchy order; only siblings are compared |
| `TabNavigation` | How the stops inside it take part: `Continue` (walked where it stands, then on), `Cycle` (round and round inside), `Contained` (stays at its ends), `Once` (one stop as a whole), `None` (nothing inside is a stop) |

List, tile and tree views are `Once`: Tab enters at the selected row (a container of your own says where by overriding
`ResolveTabEntry`), and the next Tab leaves. A stop scrolled out of view is scrolled into it; tooltips, popup sheets and
scroll bars are never stops.

Tab is held inside a modal, a dialog whose dimmer is up, and a popup that cycles; another player's popups are never stops.
Tab commits a text field's edit and moves on, and arriving by Tab starts editing (`bTabStartsTextEdit`); a dropdown commits
its highlighted row on Tab (`bTabCommitsHighlightedRow`) and a menu closes its whole chain.

Project settings (*Project Settings > Plugins > Dream GUI*): `bTabNavigation`, `TabOrder` (`Hierarchy`;
`LegacyGeometric` puts 2.0's geometric order back for this release), `bTabWrapsAtScreenEnd`, `bTabStartsTextEdit`. While
the player has a DreamGUI focus or Tab stop, Slate's own navigation from the bare game viewport (Tab, the arrows) is
DreamGUI's, so none of it moves the focus off the viewport into UMG.

### Arrows, the pad, and navigation areas

Directional navigation moves the focus by the `DirectionKeys` table. A widget can carry rules of its own per direction
(`UDreamWidgetNavigation`, reached with `GetOrCreateNavigation`: `SetRule`, `SetExplicitTarget`, `SetCustomDelegate`,
`SetAllNavigationRules`), as UMG's Navigation does. `FDreamUINavigationScroll` handles something else: when the arrows move
the focus to a widget **scrolled out of view**, it **scrolls it in**.

`bRestrictNavigationArea` keeps navigation among a widget's children,
and `NavigationBoundaryRule` says what happens at the edge: `Stop`, `Wrap` (continue from the far side), or `Escape` (let
what encloses it answer, the way a list hands the move back to its page).

### Navigation scopes

A `UDreamUINavigationScope` is a screen, panel or dialog as far as navigation is concerned: the unit that is pushed in front
of what came before, takes focus, and gives it back on the way out. It remembers where focus should start and where it was
when it last handed control away — open a submenu and come back, and focus is where it was, not on whichever selectable
happened to register first. Push order is the stack, not the widget hierarchy, so a dialog opened from a page sits in front
of it wherever it is parented.

A scope has an input mode, `InputMode` (`EDreamUIScopeInputMode`):

| Value | Means |
| --- | --- |
| `All` | DreamGUI's built-in keys work (navigation, confirm, Back, paging, tab switching), and the game hears what DreamGUI does not keep |
| `Menu` | As `All`, and the game is expected to stand down — a project's gameplay input asks `UDreamUINavigationStack::GetEffectiveInputMode` and ignores the player while it says `Menu` |
| `Game` | Built-in navigation, confirm and Back are off for the player: a HUD's buttons are not walked onto by the D-pad, nor pressed by the jump button. Bindings registered by widgets, pointers and typing still work |

With no scope, the project setting `InputModeWithoutScope` (`All` by default) applies. With no scope and no focus, a
navigation press looks only at the player's own screen-space canvases.

### Keys act on the focus; the focus look is for keys

- Enter, Space and the pad's accept press the **focused** widget, never the hovered one, and nothing when nothing is
  focused.
- The focus look — the Focused state and the focus ring — shows only when keys or a pad moved the focus, as CSS
  `:focus-visible` (`bFocusVisibleOnlyFromKeys`). A mouse click no longer shows it, and a mouse hover no longer takes the
  ring away.
- The ring is drawn by the widget class the project setting `NavigationSelectionClass` names, fitted to the control it
  marks; with none set there is no ring. A control whose own look marks its focus refuses it with
  `UUISelectable::bUseFocusRing` (`SetUseFocusRing` at run time).
- The accept and back buttons are **the platform's**, read at run time (`bUsePlatformAcceptBack`; a Switch swaps them).
  The key tables are project settings, read again whenever they change: `ConfirmKeys`, `BackKeys`, `DirectionKeys`,
  `PageKeys`, `ExtentKeys`, `PreviousTabKeys`, `NextTabKeys`.
- The triggers page; **the shoulder buttons switch** the active tab view's tabs, passing over disabled ones and going
  round the ends, with "Previous tab" and "Next tab" prompts in the action bar. A control of your own answers them by
  implementing `IDreamUITabSwitchTarget`.
- In DreamGUI's UI-only input mode (`UDreamUIInputModeLibrary::SetInputModeUIOnly`) the cursor hides while the player uses
  a pad and comes back on the mouse or the keyboard (`bHideCursorOnGamepad`); elsewhere DreamGUI leaves the cursor alone.
  `bAutoVirtualCursorOnGamepad` (off by default) shows a virtual cursor whenever the physical device is a gamepad.

## Popups, modals, tooltips, drag and drop

**The popup layer** (`UDreamUIPopupLayer`) is one per player. A popup — a dropdown's list, a menu — stays the widget it
already was; the layer lifts it onto the player's screen root with its world position kept, so no ancestor's bounds clip it
and no ancestor's layout counts it, the way UMG's combo list lives in Slate's menu stack. The layer handles outside clicks,
focus, nesting of menus, and following a moving anchor while open. Back closes only the top one, and focus goes back to
the opener. A popup says what Tab does in it (`TabBehavior`): `Cycle` goes round inside; `CloseAndContinue` closes it and
takes the step from the opener (dropdown lists and menus). A press on a layer drawn **in front of** an open popup closes the
popup and still reaches that layer.

⚠️ "Lifted onto the screen root" is backed by **two APIs**; do not mix them up:

| | Does |
| --- | --- |
| **`Elevate` / `Restore`** | **Only moves it**: from its parent onto the screen root (world position kept to the pixel), and back again |
| **`Push` / `Pop`** | On top of the move, **four more things**: focus return (`FDreamFocusReturn`), close on outside click, Back, menu nesting — and **following the opener after every frame's layout** |

`FDreamPopupParams` holds `Push`'s parameters:

| Field | Default | Notes |
| --- | --- | --- |
| `Popup` / `Opener` | empty | The popup widget, and the one that opened it (where focus returns, whom it follows, who is the parent) |
| `UserIndex` | `0` | Which player it belongs to |
| `OutsideClick` | `Consume` | See below |
| `bFocusOnOpen` / `InitialFocus` | true / empty | Move focus in on open (to `InitialFocus` or the first navigable widget) |
| `bRestoreOnDismiss` | true | Move it back to its parent and place on close; false leaves it up to play a closing animation |
| `TabBehavior` | `Cycle` | The two settings described above |
| `Place` | empty | A delegate that places it after every frame's layout; unbound, it keeps its offset from the opener |
| `OnDismissed` / `OnClosing` | empty | Callbacks after closing / before closing (**while focus is still inside**) |

⚠️ **`Push`'s return value does not mean "it really opened"**: `true` only says it was open **when the call returned** —
if moving focus in ran some handler that closed the popup again, `Push` returns `false` as well. To keep track of "I am
open", watch the popup's own `OnDismissed`.

`EDreamPopupOutsideClick` has **three settings**: `Consume` (close it; the press stops here), `PassThrough` (close it; the
press **carries on to what is underneath** — what Slate's menu stack does) and `Ignore` (do nothing; wait for the owner /
Back / the parent to close it).

**There are eight dismiss reasons** (`EDreamPopupDismissReason`, passed to the owner's callback): `Explicit`,
`OutsideClick`, `Back`, `ParentClosed`, `Replaced` (pushed out by a new top-level popup of the same player / a modal in
front of them), `OpenerLost` (the opener was destroyed / left the tree / became invalid / hidden / non-interactive),
`WorldTeardown`, `Tab`.

There is also `DismissAll(UserIndex, Reason)` (close all of a player's popups **newest first** — exactly what a modal does
for its own player) and `GetHomeSlot(Widget)` (a **copy** of the panel slot a lifted widget has **at home**, **read-only** —
writing to it does not reach the widget).

**Modals** (`UDreamUIModalSubsystem`): `ShowModal(DialogClass, OnResult, UserIndex)` shows a dialog and awaits its result;
`CloseTopModal(Result)`, `CloseAllModals`, `IsModalActive`, `GetModalDepth`. The scrim eats every pointer event (its colour
is `ModalScrimColor`), the scope confines gamepad focus and restores it afterwards, and Back means "close with the Back
result". Modals **nest**: a second `ShowModal` while one is up puts its dialog on top with its own scrim and scope, and
closing it reveals the one underneath, still waiting for its own result. Showing a modal closes that player's open popups
first.

⚠️ **`UDreamDialogScope` and `ShowModal` split the work; do not stack them**: `UDreamDialogScope` only serves **the other
arrangement (standalone)** — a dialog dropped into a `.dui` as an ordinary widget, with no subsystem layer above it, where
it is the layer that lets Back close the dialog. **Opened through `ShowModal`, the scrim and Back belong to the modal
layer's scope**; stacking a `UDreamDialogScope` on top then **quietly changes which result Back answers with**.

Three promises not written down elsewhere: **`OnResult` fires exactly once** (failures such as "no screen root" or "the
class could not be made" included); **modals are per player** — in split screen, player one's "Are you sure?" **does not
cover** player two, **does not take** their focus and **is not closed** by their Back; and **why popups close first** —
otherwise a popup left behind the scrim would take the modal's **first click** and **first Back**.

**Tooltips**: every widget has `ToolTipText` and `ToolTipWidgetClass` (the latter wins, and is a **class**: instanced when
the bubble appears, destroyed when it goes). `UDreamUITooltipSubsystem` finds the nearest ancestor under the pointer that
offers one; a behaviour can implement `IDreamUITooltipSourceInterface` to offer a widget. Project settings:
`TooltipDelaySeconds` (0.5), `TooltipOffset`, `TooltipMaxWidth`, `TooltipFontSize`.

⚠️ **A finger never wakes a tooltip**: a tap, a hold past the delay, a finger dragged onto a widget — none of them brings
up a bubble; but a finger **press still dismisses a bubble already up**. A touch game that wants tooltips on long press has
to call `ShowTooltipFor(Source)` from its own long-press handler.

⚠️ **It reads, never grabs**: the bubble sits on a canvas of its own **with raycasts off throughout** — a tooltip that
could catch the pointer would hide itself for good.

**Drag and drop**: a `UDreamUIDragSource` creates a `UDreamDragDropOperation` when a drag starts and writes it onto the
pointer's event data; a `UDreamUIDropTarget` reads the operation off a drop, filters by `RequiredTag` and payload class,
and on acceptance marks it handled and broadcasts. A drop a target refuses keeps bubbling, so nested targets behave like
nested anything else. Without a source, a drag is pure geometry — exactly what a scroll view wants, and not what an
inventory item does.

Drag and drop reads clearly in **three layers**:

| Layer | Who | Looks after |
| --- | --- | --- |
| **Meaning** | `UDreamDragDropOperation` (core `DreamGUI`) | **What** this drag is: the payload, tags, drag visual |
| **Pipeline** | `UDreamUIDragDropSubsystem` (`DreamGUIInput`) | Follows each pointer's drag, lights up drop spots, moves the drag visual |
| **Ends** | `UDreamUIDragSource` / `UDreamUIDropTarget` | What gets dragged, where it can land |

The pipeline follows drags **per player and pointer** — **two fingers dragging two items on a touch screen is normal**, and
the early "one for the whole screen" version let the second finger **teleport** the first one's visual.

**The list-row versions** (`UDreamListRowDragSource` / `UDreamListRowDropTarget` / `UDreamListRowButton`, all in
**`DreamGUIControls`**) are **subclasses of the two ends above, with no detection path of their own** — they only add "which
row of whose list this is"; a drop also reports a **drop zone**, `EDreamItemDropZone` (`AboveItem` / `OntoItem` /
`BelowItem`). A list has three switches: `bAllowDragging` (default **false**), `bAllowDragDrop` (default **false**, **kept
separate from the former on purpose** — a palette you only drag out of wants the former, an equipment bar you only drop
into wants the latter), and `FingerDrag` (`EDreamListFingerDrag`: whether dragging a row with a finger **scrolls the list**
or **picks up the row**).

### Virtual cursor

`UDreamUIVirtualCursorSubsystem` (`DreamGUIInput`) makes a pointer for **a player who has none** — it solves "**four-way
navigation cannot work a map, a skill tree or a crafting grid**".

It integrates the player's **left stick** into a screen position, stands in for the OS mouse through **the override seam**
of that player's input module, and treats **the confirm button as the left button**. Everything downstream — raycasts,
hover, clicks, **drags**, tooltips — **cannot tell the difference**, and that is the whole design: pushing the stick while
holding confirm walks the mouse's drag path too (a slider's handle, for one). One cursor per player, each driven by its own
stick and drawn on its own screen.

Call `ActivateVirtualCursor` / `DeactivateVirtualCursor` (and their `ForUser` versions) from the screens that need it, or
turn on the project setting `bAutoVirtualCursorOnGamepad` (off by default) to let it follow the physical device.

## Text input and keyboard layouts

A DreamGUI text field is not a Slate widget, so it is never on the keyboard focus path, and the engine's only landing place
for a platform **character** in a game is the virtual `UGameViewportClient::InputChar`, which has no delegate to subscribe
to. With nobody owning that function, the field falls back to its own `FKey`-to-character table, which is only correct on
**US QWERTY**: AZERTY, QWERTZ, Dvorak, Cyrillic, dead keys and AltGr all type the wrong character (IME users are
unaffected; composition text arrives through TSF). The field logs one warning the first time it is edited if neither fix
is in place.

The simplest fix is the plugin's viewport client:

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

To keep a viewport client of your own, derive it from `UDreamGameViewportClient`, or call
`DreamUITextInputRouter::RouteViewportCharacter` from its `InputChar`, after the console and before the base class. The
code is on [Installation](../getting-started/installation.md).

## The Slate input source

By default DreamGUI hears input through its preset event system actor's bindings on the player controller, so it hears
nothing in the engine's own UI-only input mode: `SetInputMode(FInputModeUIOnly())` makes the game viewport ignore input,
and the controller never sees it.

Turn on *Project Settings > Plugins > Dream GUI > Input > Use Slate Input Source* (`bUseSlateInputSource`) and DreamGUI
hears the mouse, touch, keys and sticks from Slate itself — an input pre-processor, ahead of the game viewport — in every
input mode. The preset actors stand down while it is on, so nothing arrives twice. `SlateInputConsumePolicy` decides what
the UI keeps from the game:

| Value | What the UI keeps |
| --- | --- |
| `Never` (default) | Nothing; the game hears everything, as it always has |
| `WhenOverUI` | A press, release or wheel turn over DreamGUI UI, and any key the UI took |
| `WhenHandled` | Only a press on a widget that handles presses, and a key the UI took |

A key typed into a field being edited is always kept. The source is off by default for now, and becomes the default in a
later version.
