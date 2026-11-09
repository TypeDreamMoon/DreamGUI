---
title: Niagara particles
description: The NiagaraSystem visual -- a Niagara system simulated by a component the scene never draws and drawn by the canvas, one draw call per sprite or ribbon renderer, clipped, masked, blended and grouped like any element; its space, its time, its shading, the parameters it is given, bursts and flights, the trigger, and what it does not draw.
---

`UDreamNiagaraSystem` (the `NiagaraSystem` visual, in the `DreamGUINiagara` module) puts a Niagara system on a widget. The
element owns a Niagara component that it registers with its world but attaches to nothing: the component **simulates**, and
the scene is given no proxy for it -- its `CreateSceneProxy` returns none --
so nothing of it is ever drawn in the world, not even the frame it is activated in. Niagara's scalability leaves it alone
(`SetAllowScalability(false)`), so an effect is never culled for not being seen. Every frame the canvas sends its sections, the element waits for the
simulation's tick to finish, reads each renderer's particles through its attribute bindings, and writes them into a segment
of its own. The particles are DreamGUI geometry from there on: clipped, masked, blended and put into layer groups like any
element.

```text
NiagaraSystem Sparks {
    System        = /DreamGUI/Samples/Niagara/NS_Sample_ClickSparks
    bAutoActivate = false
}
```

`Content/Samples/NiagaraEffects.dui` shows five effects -- click sparks, a pointer trail, sparks round a hovered button, a
bar that bursts when full, and coins flying to a counter.

## What is drawn

| Niagara | DreamGUI |
| --- | --- |
| An emitter simulated on the CPU | Read |
| An emitter simulated on the GPU, or a lightweight (stateless) one | Not drawn, reported |
| Sprite renderer (particle sprites) | A segment of quads |
| Ribbon renderer | A segment of strips |
| Mesh, light, component and decal renderers | Not drawn, reported |

Each drawn renderer is a **segment**: a section and a draw call of its own, in Niagara's renderer order (sort hint, then
the emitter's and the renderer's place). The details panel lists every renderer -- what it is drawn as, its texture and
blend, or why it is not drawn -- and the same report is `GetRendererInfo()`.

**Sprites** are written as Niagara's sprite vertex factory places them, flat on the widget: size about the pivot
(`PivotInUVSpace`, or the particle's pivot offset), turned clockwise by the rotation, aligned to the velocity or to a custom
alignment, the UV scale, and the sheet's frame (`SubImageSize`) with, when the renderer blends sub-images, the blend towards
the next frame. Sorting: none, or by the custom sorting attribute ascending or descending; sorting by the view has no
depth to sort by on a flat UI and sorts by the custom attribute ascending.

**Ribbons** are grouped by their ribbon id and laid in link order (or by age, youngest first, without one), cut along a
Catmull-Rom curve -- a cardinal spline of the renderer's curve tension -- as finely as the renderer's tessellation asks
(a constant factor, or as many pieces as the bend needs), with both UV sets (stretched by point, by length, or tiled) and
their per-particle U overrides. A ribbon of another shape than a plane is drawn as a plane facing the reader; the UV edges'
smooth transition is drawn locked.

The vertices are written into arrays the section keeps, grown by tiers (64, then powers of two) and never shrunk, on
worker threads when there are enough particles. While a segment's count stays inside its tier, its buffers on the GPU are
updated in place rather than made again. One draw call indexes at most 65,535 vertices (16,383 sprites); past that the rest
are left out, with a warning once.

## Space

The simulation's units are the canvas's: one unit is `PixelsPerUnit` canvas units (1 by default), on the plane `Plane`
names. With **XZ** (the default) the simulation's X is the widget's right and Z its up, so an effect made with gravity along
-Z falls down the screen; with **XY**, Y is up and Z comes out of the screen. `bFlattenDepth` (on by default) draws every
particle on the widget's plane, however far out of it the simulation put it.

The component stands, in the simulation's world, where the widget does on the canvas. A **local-space** emitter's particles
move with the widget; a **world-space** emitter's stay on the canvas where they were spawned and trail behind a widget that
moves. `SimToCanvas` and `CanvasToSim` convert between the two.

## Time

| Property | |
| --- | --- |
| `bAutoActivate` | Start when the widget is registered |
| `bPaused` / `SetPaused` | Hold the simulation where it is |
| `TimeDilation` | How fast its time runs against the world's |
| `bTickWhenGamePaused` | Run while the game is paused, as UI does (on by default) |
| `FixedTimeStep` | Steps of this many seconds instead of the frame's, as many as the frame holds (at most 8): the same particles at every frame rate |
| `WarmupTime` | Run this far ahead when it starts |
| `Advance(Seconds, Step)` | Run it forward now, whatever its pause: a warm-up, a scrub, a test |

In a game world with no fixed step, the component ticks itself (solo, with its own time dilation and pause). With a fixed
step, and in a world that does not tick components -- the designer's preview, the level editor -- the element steps the
simulation itself as its canvas draws, so the designer previews the effect. A simulation only runs where the application
can render: under `-nullrhi` Niagara runs none.

## Shading

Each renderer is shaded one of two ways (`DefaultShading`, or a renderer's override):

- **Built-in** (the default). DreamGUI's own shader, no material. The texture is the particle material's `Texture`
  parameter, else its first texture parameter, else the first texture its graph samples; a material that samples none --
  the engine's default sprite and ribbon materials draw procedurally -- gets a soft disc (a soft band for a ribbon). The
  blend is additive for an additive material, multiply for a modulating one, alpha otherwise. Each can be set:
  `bOverrideBlendMode` and `BlendMode` for every renderer (`SetBlendModeOverride` from a Blueprint), or a renderer's own override (`RendererOverrides`: emitter name,
  renderer index, shading, texture, blend mode, material). A particle's colour may be brighter than 1: the vertex keeps it
  divided by a scale and the shader multiplies it back. Every blend mode works, the ones that read what is behind included
  (see [Blend modes and layer groups](blend-modes-and-groups.md)). An additive draw leaves the alpha behind it alone, as
  any element's does: light alone in an isolated group shows nothing when the group is composited by a mode that weighs
  by alpha, Multiply for one -- blend such particles by alpha.
- **Material**. A UI material: the override's, else the one `MaterialMap` gives for the particle material. **Convert
  Particle Materials** in the details panel makes them: each drawn renderer's particle material copied beside it as
  `<name>_DreamUI` (an engine or plugin material into `/Game/DreamUI/ConvertedParticleMaterials`), saved, put into the
  map, and the shading set to Material. The copy is a surface, unlit, two-sided material keeping the source's blend
  (opaque becomes translucent), whose particle inputs read what DreamGUI writes, and whose opacity goes through DreamGUI's
  clip:

  | In the particle material | In the conversion |
  | --- | --- |
  | Particle Color | The vertex colour, taken to linear light, times its scale |
  | Particle SubUV | The frame and the next one, blended |
  | Dynamic Parameter | The spare texture coordinates the vertex carries it in |
  | Texture coordinate 1 | Texture coordinate 2 (1 is the widget record's) |
  | Opacity | Times `MF_DreamWidgetProperty` into `MF_DreamUI_Clip`; an additive material's emissive too |

  What it cannot convert is listed, with why: the other particle attributes (relative time, random, size, speed and the
  like) and the scene's reads (depth fade, scene depth and texture) are left as they are and draw with their defaults. A
  material instance is converted by converting its parent and parenting a copy of the instance to that. A material that
  samples DreamGUI's widget data needs it drawn by DreamGUI's renderer.

A particle's vertex, for a material written by hand: UV0 the frame (a ribbon's first UV set), UV1.x the widget record and
UV1.y the dynamic parameter's x, UV2 the next frame (a ribbon's second UV set), UV3.x the colour's scale and the frame
blend packed (`floor` is the scale times 16, the fraction the blend times 255/256), UV3.y the dynamic parameter's y, UV4
its z and w. `Shaders/Private/DreamUIParticle.ush` and `DreamNiagaraVertexLayout.h` hold the same constants.

## What the system is told

A system that declares one of these user parameters is given it every frame, before it ticks (`bWriteUIParameters`):

| Parameter | Type | |
| --- | --- | --- |
| `User.DreamUI_WidgetSize` | Vector2 (or Vector) | The widget's size, in simulation units |
| `User.DreamUI_PointerPosition` | Position (or Vector) | The pointer, in the simulation's world |
| `User.DreamUI_PointerInside` | bool (or float) | The pointer is over the widget |
| `User.DreamUI_TargetPosition` | Position (or Vector) | A flight's target (below) |

`SetPointerPositionOverride` stands in for the pointer. The rest go through `SetVariableFloat`, `SetVariableInt`,
`SetVariableBool`, `SetVariableVec2`, `SetVariableVec3`, `SetVariableLinearColor` and `SetVariableObject`, and
`GetNiagaraComponent()` is the component itself.

## Bursts and flights

- `BurstAt(LocalPosition)` spawns the system once more at a point of the widget, beside the running one; its particles are
  drawn in the system's segments until they are gone.
- `BurstTo(Target, LocalPosition)` spawns the **flight system** -- `FlightSystem`, else the plugin's template
  `/DreamGUI/Niagara/NS_DreamUI_FlyTo` -- whose particles head for the target widget's centre, written to
  `User.DreamUI_TargetPosition` every frame so a moving target is caught. Its renderers are segments of their own, after
  the system's. A widget that only flies needs no `System`.
- `OnBurstFinished(BurstId)` says when a burst's particles are all gone.

The template's source is `DFX/Niagara/NS_DreamUI_FlyTo.dfs`: a handful of tokens that scatter, then sweep in an arc to the
target and shrink into it. Copy it to make your own flight; keep the parameter's name and the world space.

## The trigger

`UDreamNiagaraTrigger` (the **Niagara Trigger** component) plays a widget's system on its pointer events, without a
Blueprint. Put it on the widget that takes the pointer; the system is `SystemWidget`'s visual, else this widget's or the
first below it.

| Property | |
| --- | --- |
| `OnClick`, `OnEnter`, `OnExit` | Burst at the pointer, burst at the centre, burst to the flight target, activate, deactivate, restart, or nothing |
| `FlightTarget` / `FlightTargetName` | Where a flight goes, by reference or by display name in the same tree |
| `ProgressWidget` / `ProgressWidgetName` | A widget with a float `Percent` -- a progress bar -- watched: when it reaches 1, `OnProgressFull` is done |
| `ClickAddsProgress` | What a click adds to that progress: a charge bar |

`Fire(Action)` does an action from code.

## Editor

The palette lists **Niagara System** under Effects and **Niagara Trigger** under Components. The details panel shows the
renderer report and the conversion button. The designer previews a system: it steps the simulation as the preview draws.

## Not drawn yet

GPU simulations, mesh renderers and lightweight emitters are reported and left out. A segment drawn through a material keeps
the material's blend; only the built-in shader takes `BlendMode`. Emitters reading the scene (collisions, the depth buffer)
read the simulation's world, which is the canvas, not the level.

*Reference: SourySK/NiagaraUIRenderer and liufei2008/LGUI_ParticleSystem (both MIT) were read when this was designed; the
code is DreamGUI's own.*
