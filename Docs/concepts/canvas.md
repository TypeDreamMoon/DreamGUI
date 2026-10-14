---
title: Canvas
description: What UDreamCanvas does — batch a subtree into draw calls — with root and nested canvases, the four render modes, scaling and projection, sort order, what per-widget perspective needs, the screen root made on demand, and what DreamUI.Stats measures.
---

A canvas (`UDreamCanvas`) is what turns a tree of widgets into a picture. It is a **behaviour** — a `UDreamUIBehaviour`
on some widget — and it answers for the whole subtree below it: it gathers the geometry of every visual, batches it in
hierarchy order into as few draw calls as it can, and hands those to the renderer. A widget with no canvas above it is
not drawn.

A canvas does not update every frame, only when it needs to. Three kinds of change can rebuild its draw calls: a material
or texture changing, or a widget's active state; a transform or a vertex position changing, so that draw calls may now
overlap; and a change of hierarchy order, which is the drawing order. Even then it checks whether the previous draw calls
can be reused. An update walks only the widgets that asked to change, patches in place the sections whose geometry
changed rather than rebuilding them, and keeps the draw calls when the widgets only moved.

## Root and nested canvases

The topmost canvas of a tree is its **root canvas**; DreamGUI's update starts there and goes all the way down. Canvases
deeper in the tree are nested canvases:

- A nested canvas inherits the root's render mode (`GetActualRenderMode`) and render target (`GetActualRenderTarget`).
  `SetRenderMode` only takes effect on a root canvas.
- `OverrideParameters` is a bitmask (`DefaultMaterial`, `RequireNormalAndTangent`, `BlendDepth`, `DepthFade`) that says
  which of these a nested canvas takes from its parent and which it sets itself.
- With `bOverrideSorting` on, it uses its own `SortOrder`; otherwise it sorts by hierarchy order.
- `bForceRenderToTarget` makes the canvas render to a texture whatever the root's mode is — which breaks its link to the
  canvases above and makes it a root of its own.

A nested canvas draws into its parent's surface, and a canvas is not a clipper, so its own rect says nothing about what is
visible. `bCullElementsOutsideCanvas` (leave out elements entirely outside the canvas rect; on by default) therefore acts
only on a root canvas or one forced to a render target.

## Render modes

`RenderMode` (`EDreamRenderMode`) says where a root canvas draws and who draws it:

| Mode | Name in the editor | Drawn by | Notes |
| --- | --- | --- | --- |
| `ScreenSpaceOverlay` | Screen Space Overlay | DreamGUI's renderer | On the screen; several screen-space roots in one world sort by `SortOrder` |
| `WorldSpace_DreamUI` | World Space - DreamUI Renderer | DreamGUI's renderer | In the level, untouched by post process |
| `WorldSpace` | World Space - UE Renderer | The engine's pipeline | In the level, as ordinary primitives that take lighting, fog and post process |
| `RenderTarget` | Render Target | DreamGUI's renderer | Into a `UTextureRenderTarget2D` |

Choosing between the two world-space modes, and placing and clicking such a canvas, is
[World-space UI](world-space.md); the two renderers are compared in
[Rendering](rendering.md).

## Screen space: scaling and projection

A screen-space root takes its size from the local player's viewport, and the canvas-scaler properties (category
`DreamGUI-CanvasScaler`) turn that into the root widget's size:

| Property | Does |
| --- | --- |
| `ScaleMode` | `ConstantPixelSize` (1 unit is 1 screen pixel; the default), `ScaleWithScreenSize` (scale against a reference resolution), `ScaleWithEngineDPI` (UMG's rule: the engine's UI Scale Rule and DPI curve), `Custom` (ask `CustomScale` — see below) |
| `ReferenceResolution` | The reference resolution, `(1280, 720)` by default |
| `MatchFromWidthToHeight` (shown as Match) | Under `MatchWidthOrHeight`, whether width or height is matched |
| `ScreenMatchMode` | `MatchWidthOrHeight`, `Expand`, `Shrink` |
| `ScreenSpaceRenderScale` | Draw the UI at this fraction of the viewport's resolution and upscale; below 1 trades sharpness for fill rate |
| `bFixedSizeInEditMode` / `SizeInEditMode` | A fixed size in edit mode instead of the editor viewport's |

`Custom` hands the job to a **custom scale object** (`UDreamCanvasCustomScale`): two virtuals, `Init` and
`CalculateSizeAndScale`, work out the canvas size and scale, and `ConvertPositionFromViewportToCanvas` /
`ConvertPositionFromCanvasToViewport` convert positions in both directions.

The positions it converts are pixel positions on **the part of the viewport the canvas fills** (the whole viewport, or one
player's region in split screen, with zero at the top left). **The canvas has already taken that region's corner into
account**, so the object measures from **its own region's corner**. These functions **only hold on a root canvas**; a
non-root canvas returns failure.

Under `ScaleWithEngineDPI` the layout rect keeps the curve's design size at every resolution, so a layout authored for it
never runs out of room and gets clipped — it only renders smaller — and a project that tunes its DPI curve moves UMG and
DreamUI together.

A canvas has a virtual camera. `ProjectionType` (`Perspective` by default) and `FieldOfView` (horizontal, 60 degrees by
default) define it: the eye stands at `Width * 0.5 / tan(FOV / 2)`, the distance at which the canvas rect exactly fills
the frame. A wider field brings the eye closer and deepens every perspective scope below. `NearClipPlane` and
`FarClipPlane` are advanced properties; `SetProjectionParameters` sets all four at run time.

## Perspective per widget

`bPerspective` on a widget foreshortens its descendants toward an eye, as CSS `perspective` does (the properties are on
[The widget model](widgets.md)). What it needs is **a screen-space canvas with a perspective projection**,
and it is inert otherwise: an orthographic canvas's eye is at infinity, which no affine map can reach. That is why
`ProjectionType` and `FieldOfView` are deliberately not advanced properties — an author who cannot find them cannot
calibrate the feature.

It is cheap because the canvas **already** draws through a perspective projection. A subtree needs no projection or pass of
its own; its geometry is re-aimed so that the canvas's projection produces the picture the subtree's own eye would have
seen. The re-aiming is a plain affine map: no vertex format change, no shader change and no extra draw call. Nested scopes
hand off from the innermost outward, and only the outermost maps onto the canvas's eye.

## Sort order

- `SortOrder` is stored as an `int16`, -32768 to 32767; a larger value draws over a smaller one. A nested canvas uses its
  own only with `bOverrideSorting` on.
- `SetSortOrder(Value, PropagateToChildrenCanvas)` keeps the child canvases' order relative to it when it propagates;
  `SetSortOrderToHighestOfHierarchy` and `SetSortOrderToLowestOfHierarchy` put it above or below every canvas of the same
  hierarchy.
- Screen pages: `AddToViewport`'s `InSortOrder` orders pages against each other. 1000 (`StackBaseSortOrder`) and up are
  reserved for the page stack, which rewrites its pages' sort orders on every refresh; a free-standing page placed there
  draws in an undefined order against the stack, so asking for one logs a warning.
- Among world-space canvases, `UDreamWorldWidgetComponent::SortOrder` is written through to the root canvas.

## Render-target canvases

In `RenderTarget` mode a canvas draws into `RenderTarget`, or into one it makes for itself when none is assigned
(`AutoRenderTarget`, never saved or copied).

| Property | Does |
| --- | --- |
| `RenderTargetClearColor` | The clear colour, transparent by default |
| `RenderTargetUpdateMode` | `Automatic` (draw when something changed; the default), `Always` (every frame), `WhenRequest` (only on `RequestUpdateForRenderTarget`) |
| `RenderTargetSizeMode` | `RenderTargetFitToCanvas` (the target follows the canvas; the default), `CanvasFitToRenderTarget` (the other way), `None` |
| `RenderTargetResolutionScale` | The target's resolution scale when it follows the canvas |

A render-target canvas is drawn by **a render command of its own** after its sections change, not inside one of its
world's views — so it updates whether or not anything renders its world. To show what it drew, put a
`CanvasRenderTargetPreviewer` visual in another tree (`SetPreviewCanvas` names the canvas), or have a
`UDreamUIRenderTargetGeometrySource` make geometry for it in the level — a `Plane`, a `Cylinder`, or a `StaticMesh` it is
mapped onto — with a `UDreamUIRenderTargetInteraction` beside it to make it clickable.

## The screen root, made on demand

Nothing has to be placed in a level. Screen UI hangs under a screen-space root canvas that `UDreamScreenUISubsystem` keeps
**per local player** and creates the first time one is needed:

- `UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, WidgetClass)` makes an instance of a widget Blueprint and puts it
  on the screen; its return pin takes the shape of the class, so no Cast is needed;
- `UDreamScreenUISubsystem::AddToViewport`, `AddToPlayerScreen`, `CreateWidgetOnScreen` and `RemoveFromViewport` do the
  same with a player named;
- `GetOrCreateScreenRoot` and `UDreamUIBPLibrary::GetOrCreateScreenSpaceUIRoot` hand you the root itself.

Split screen is the ordinary case rather than a mode: players 1 and 2 each get a root canvas, a screen-space raycaster
carrying their own `UserIndex` and a page stack, so a full-screen page pushed by one never covers the other. Unlike UMG
there is no shared layer to fall back to: `AddToViewport` is `AddToPlayerScreen` for whoever owns the widget. The
raycaster and the event system are made on demand too (see [Input](input.md)).

The root Blueprints placed in levels before 2.0 (`ScreenSpaceRoot`, `WorldSpaceRoot_DreamRenderer`,
`WorldSpaceRoot_UERenderer`) are gone, with no redirect; see [Migration](../guides/migration.md).

## Other canvas properties

| Property | Does |
| --- | --- |
| `DefaultMaterial` | The material default UI elements are drawn with |
| `bRequireNormalAndTangent` | Carry normals and tangents in the vertex data |
| `bEnableDepthTest` | Make a depth texture to test against, for a `StaticMesh` with an opaque material; screen-space and render-target modes only |
| `BlendDepth` / `DepthFade` | World Space - DreamUI Renderer only: how far scene depth occludes it, and a depth fade |
| `TraceChannel` | The channel interaction rays hit world-space UI on |
| `bAllowDropFrame` | Let the canvas skip a frame when its draw calls take too long (advanced) |
| `DefaultMeshType` | Draw with a `UDreamUIMeshComponent` subclass of your own (advanced) |

`SetDrawCallRebuildSuspended(true)` holds a canvas's draw-call list as it is. Vertex refreshes still run — a widget that
moves or changes colour keeps updating — and a rebuild asked for meanwhile happens the moment it is released. It is what
Slate's `UInvalidationBox` does for cached draw elements, done for DreamGUI's draw calls, and the `UDreamInvalidationBox`
behaviour drives it.

## What DreamUI.Stats measures

The console command `DreamUI.Stats` prints what the frames since the last `DreamUI.Stats` cost on average, and starts
counting again. Counting is always on — one clock read at each end of a stage and an atomic add, a handful per root canvas
per frame — so the numbers a benchmark reads, the ones this prints and the ones Unreal Insights shows describe the same
frames. The totals are **the process's**, not a canvas's or a world's: that is what a frame costs.

| Stage | Thread | What it is |
| --- | --- | --- |
| `ManagerTick` | Game | The UI manager's tick: behaviours, layout, clips, and every root canvas's update below |
| `CanvasUpdate` | Game, inside `ManagerTick` | A root canvas and its children bringing widgets, clips and geometry up to date |
| `Batching` | Worker | A canvas building its draw calls out of the geometry the game thread prepared |
| `DrawCallSubmit` | Game | Taking finished draw calls into mesh sections and materials, and sending them to the render thread |
| `RenderRecord` | Render | Recording the UI's passes into the frame's graph |

`CanvasUpdate` runs inside `ManagerTick`, so the two are not to be added; `Batching` and `RenderRecord` run beside the
game thread, not inside it. Every stage is also a CPU trace scope in Insights, named `DreamUI_` and the stage.

The counters say how much was done: batches and vertices recorded (`BatchesRecorded`, `VerticesRecorded`), sections and
bytes uploaded (`SectionUploads`, `UploadedBytes`), canvases whose draw calls were rebuilt (`DrawCallRebuilds`), canvases
whose widgets only moved and were refreshed in place (`InPlaceRefreshes`), sections patched (`SectionPatches`) and reused
as they were (`SectionReuses`), widgets a canvas update looked at (`WidgetsUpdated`), render layers moved, made and taken
back (`RenderLayerMoves`, `RenderLayerPromotions`, `RenderLayerDemotions`), and texts painted (`TextPaints`).

`stat DreamGUI` still shows its counters, and `DreamGUI.Memory` lists each world's canvas mesh sections and paint rows.
Capturing, profiling and benchmarking are covered in [Debugging](../tools/debugging.md) and
[Benchmarks](../tools/bench.md).
