---
title: World-space UI
description: Putting the same widget Blueprint in a level — ADreamWorldWidgetActor and the properties of UDreamWorldWidgetComponent, the two render backends, interaction that needs no setup, attaching to a scene component from code, and what a Level Sequence can key.
---

The same class can be a surface in the level instead of a layer on the screen. World space is a first-class render mode
of a canvas here, not "render to a texture and put it on a quad", and hit testing is right at any angle.

## Putting it in a level

Two ways, one result:

- drag a widget Blueprint from the Content Browser into a level;
- or place a **DreamUI World Widget Actor** from the Place Actors panel.

Either way you get an `ADreamWorldWidgetActor`, whose entire content is a single `UDreamWorldWidgetComponent` (its root
component). Move it, rotate it and attach it like any other scene component. The component can also be added to any actor
of your own (it is **DreamUI World Widget** in the add-component list).

The component **hosts** the tree; it does not redefine it. The tree is instanced from `WidgetClass` unchanged and the
Blueprint's own root canvas is kept — what the designer set there is what the level gets; a tree authored with no canvas
is given one. The component writes render mode, sort order and trace channel down onto that canvas and leaves the rest
alone. Its properties are live: changing the size, the pivot, the backend or the sort order re-applies to the tree already
loaded; only a change of class rebuilds it.

## The component's properties

| Property | Default | Does |
| --- | --- | --- |
| `WidgetClass` | — | The widget Blueprint class to load. Changing it is the one edit that rebuilds the tree |
| `Backend` | `DreamUIRenderer` | Who draws it; see the next section |
| `bUseDesignSize` | on | Take the size from the class: the canvas size the Blueprint was designed at |
| `DrawSize` | `(1920, 1080)` | The root's size, in world units, when `bUseDesignSize` is off |
| `Pivot` | `(0.5, 0.5)` | Where on that rectangle the component's origin sits; `(0.5, 0.5)` centres the panel on the actor |
| `SortOrder` | `0` | Draw order among world-space canvases, written through to the root canvas |
| `TraceChannel` | `TraceTypeQuery1` (Visibility) | The channel a raycaster must be set to for its rays to reach this tree, and the channel its occlusion trace uses |

At run time: `SetWidgetClass`, `SetBackend`, `SetUseDesignSize`, `SetDrawSize` (an explicit size, which turns the
design size off), `SetPivot`, `SetSortOrder`, `SetTraceChannel`. `GetDrawSize` answers the size the root **actually** gets.

Why the design size is the default: a screen stretches to its viewport, and a world panel has no viewport to stretch to.
The size it was designed at is the only size anyone actually chose for it, and a host that lands at that size looks in
the level the way it looked in the designer. So the design size rides the class and survives into a cooked build
(`UDreamWidgetGeneratedClass::GetDesignSize`).

## The two backends

`Backend` (`EDreamWorldWidgetBackend`) maps to the root canvas's render mode:

| Backend | Render mode | Character |
| --- | --- | --- |
| `DreamUIRenderer` | `WorldSpace_DreamUI` | DreamGUI's own renderer: drawn by the view extension after the scene, with DreamUI's sorting. Sharper text and exact draw order; no scene lighting, untouched by post process |
| `UERenderer` | `WorldSpace` | The meshes are ordinary primitives in the scene: they light, cast, fog and occlude like anything else and sort by translucency rules rather than DreamUI's; post process applies |

With the DreamUI renderer, `BlendDepth` on the root canvas says how far scene depth occludes it: `0` occluded by scene
depth, `1` all visible, `0.5` half transparent; `DepthFade` adds a depth fade. World-space canvases are drawn in shared
render-graph passes, with the per-panel work on worker threads; `Tools/Bench` has a level of 2688 world-space panels for
measuring that (see [Benchmarks](../tools/bench.md)).

## Interaction: no setup

Pressing Play is enough to click a button hanging in the world. On `BeginPlay` the component asks for an event system and
a `UDreamWorldSpaceRaycaster` for each local player and supplies whichever is missing
(`UDreamUIInputSubsystem::EnsureInteractionForPlayer` puts the raycaster on a transient `DreamInteractionHost_P%d` actor;
the event system is spawned from `UDreamGUISettings::EventSystemActorClass`).

There is **one raycaster per player**, not one per panel: the ray is aimed by that player's view, and which canvases it can
hit is answered by asking the manager for the world-space roots whose `TraceChannel` matches its own. Adding, moving or
destroying a panel is nothing it has to be told about, and a panel needs no interaction component of its own.

| Raycaster property | Default | Does |
| --- | --- | --- |
| `PointerSource` | `Mouse` | `Mouse` aims through the pointer's own screen position (a mouse, a touch, a virtual cursor); `ScreenCenter` through the middle of the view, for a first-person reticle — on a split screen, the middle of that player's part of the viewport |
| `bOccludeByWorld` | on | Solid geometry blocks a click the way it blocks a line trace |
| `TraceChannel` | Visibility | Answers only for roots on the same channel; with occlusion on, also the occlusion trace's collision channel |
| `RayLength` | `100000` | Ray length |
| `DragThreshold` / `bHoldToDrag` / `HoldToDragTime` | `5` / off / `0.5` | The drag threshold (in the target's local space), and press-and-hold to start a drag |

Put a raycaster of your own on any actor with the same `UserIndex` and nothing is added on top of it — the test is for one
that exists, not for one this plugin made. That is how you get your own pointer source, ray length, occlusion or drag
behaviour.

### Occlusion by the world

With `bOccludeByWorld` on, each pointer does **one** single line trace a frame, along the ray the panels are hit-tested
with, on the collision channel `TraceChannel` stands for (the engine's Visibility unless changed). The hit carries no
widget, but it wins on distance, so a panel behind it stops being pointed at; and the actor it hit is sent the pointer's
events — enter, exit, down, up, click, double click, long press, scroll — on itself and on any of its components that
implement the pointer interfaces (see `FDreamUIPointerWorldTarget`). That is how a render-target surface's
`UDreamUIRenderTargetInteraction` gets its pointer: the surface is a mesh in the world, and this trace is the only thing
that reaches it. A wall implements none of the interfaces and is only an occluder.

What stops the pointer on Visibility is solid geometry: a static mesh component's default `BlockAllDynamic`, a placed
static mesh actor's `BlockAll`. What does **not**: a pawn's capsule and a character's mesh (the engine's Pawn,
CharacterMesh and Spectator profiles ignore Visibility), triggers, and bare box, sphere or capsule components (their
default `OverlapAllDynamic` only overlaps, and the trace is single, so overlapping never occludes).

**Note:**

  The actor the raycaster rides on is skipped, and nothing else is. A primitive that blocks the channel around the camera —
  a pawn given a profile that blocks Visibility, seen from inside — is hit at distance zero, where the ray starts, and takes
  every world-space panel away. Ride the raycaster on that pawn, or give the pawn a profile that ignores the channel.

To click through walls, untick it on a raycaster of your own, or call `SetOccludeByWorld(false)` on the player's. It is on
by default because off, a world pointer clicked straight through walls into panels the player could not see — one in the
next room, one behind a pillar — and never reached a render-target surface at all; and the raycaster made for a player is
a default one, so a project relying on it had both problems and no setting in sight to explain them. The price is one
trace per pointer per frame.

## From code

It is the two calls that were already there: `UDreamUIBPLibrary::ConstructWidget` for the root, then
`AttachWidgetToSceneComponent` on whatever scene component should carry the tree. The root must carry a canvas (the canvas
decides the render mode and owns the meshes) and must have no parent; otherwise the call returns `false` and changes
nothing. Attaching is also what takes the root out of the not-yet-added state, so it is drawn and hit.

```cpp
#include "DreamUIBPLibrary.h"
#include "Core/Components/DreamCanvas.h"

UDreamWidget* Panel = UDreamUIBPLibrary::ConstructWidget(this, TEXT("Panel"), nullptr);
Panel->SetWidth(800.0f);
Panel->SetHeight(450.0f);

UDreamCanvas* Canvas = Panel->AddComponent<UDreamCanvas>();
Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);

// ...build the children with ConstructWidget + AddChild...

UDreamUIBPLibrary::AttachWidgetToSceneComponent(Panel, GetRootComponent());
```

For a widget Blueprint class, adding a `UDreamWorldWidgetComponent` to the actor and calling `SetWidgetClass` is less work:
the component looks after the size, the pivot, the backend and the channel.

## Level Sequence

An `ADreamWorldWidgetActor` can be possessed by a Level Sequence directly, and `WidgetOpacity`, `WidgetOffset` and
`bWidgetVisible` on the component are keyable from it. Animation inside the tree is on [Animation](animation.md).

## What no longer exists

The world-space root Blueprints of before 2.0 (`WorldSpaceRoot_DreamRenderer`, `WorldSpaceRoot_UERenderer`,
`DreamWorldSpaceRaycasterSource_Mouse`), `UDreamWidgetPresenterComponent` (the abstract base
`UDreamWidgetPresenterComponentBase` stays; `UDreamWorldWidgetComponent` is what you place), and the
`UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` family (absorbed by `UDreamWorldSpaceRaycaster`) are gone,
with no redirect. A level that still places one of those Blueprints drops that actor on load and logs a warning naming it;
drag the widget Blueprint into the level again. See [Migration](../guides/migration.md).
