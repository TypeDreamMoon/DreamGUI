---
title: Rendering
description: How the renderer works — the DreamGUIRenderer view extension, the two renderers for world canvases, draw-call batching, render layers, materials on visuals (render-thread proxies, DreamUI_IsRenderByDreamUIRenderer, blending on sRGB-encoded values with the renderer doing its own premultiply), render-target canvases, screen effects, and the r.DreamUI.Verify* checks.
---

## The renderer

DreamGUI's own renderer is `FDreamUIRenderer`, a scene view extension (`FSceneViewExtensionBase`) in the bottom-layer
`DreamGUIRenderer` module. It knows nothing of widgets: the view extension, the render command that draws a render-target
canvas, the shaders, the vertex and index formats, the material proxies a canvas answers its parameters through, the
post-process proxies the screen effects share, and the stage timing behind `DreamUI.Stats` all live in that module; the
core registers what it asks for and the renderer does it.

It compiles against the engine's public headers only: scene depth is read through the public scene-texture API, and a
static check refuses any include path into `Runtime/Renderer/Private` or `Internal`. Its order among other view
extensions is `PriorityInSceneViewExtension` (*Project Settings > Plugins > DreamUI*; higher comes first). It logs to
`LogDreamGUIRenderer`, and `stat DreamGUI` still shows its counters.

Who draws a tree is decided by its root canvas's render mode (see [Canvas](canvas.md)):

| Render mode | Drawn by |
| --- | --- |
| `ScreenSpaceOverlay` | DreamGUI's renderer, onto the viewport after the scene |
| `WorldSpace_DreamUI` | DreamGUI's renderer, after the scene and in DreamUI's order, occluded by scene depth as `BlendDepth` says; no lighting, no post process |
| `WorldSpace` | The engine's renderer: the canvas's meshes are ordinary scene primitives that take lighting, fog, occlusion and post process and sort by translucency rules |
| `RenderTarget` | DreamGUI's renderer, into the target, by a render command of its own |

Choosing between the two world-space backends is on [World-space UI](world-space.md). The renderers cannot
share render data: a canvas attached under another whose render mode is not compatible has its render data made again.

## Batching

A canvas collects its elements in hierarchy order and merges neighbours that can be merged — same material and texture,
same blend — into one draw call:

- **2D elements** are easy to test for overlap, so they batch without changing what is drawn. Whether an element is 2D is
  decided in the canvas's relative space: its Z position and its X and Y rotation must be under `AutoBatchThreshold`
  (*Project Settings > Plugins > DreamUI*).
- **3D elements** are almost impossible to test for overlap, so one may batch only into the last draw call of the list.
  That is how a widget whose render transform gains depth, yaw or pitch drops out of the 2D path — a flipping card costs a
  draw call.
- Elements that blend differently never share a draw call. Elements drawn by the built-in shader use their visual's
  `BlendMode` (`Alpha`, `Additive`, `Multiply`); an element with a material takes its blend from the material, and the
  enum still takes part in the batching decision.
- Text styles (outline, glow, …) are stored per widget in the canvas's widget property texture, so texts with different
  styles still batch into one draw.
- Direct-mesh visuals such as `StaticMesh` are a draw call each.

An update walks only the widgets that asked to change, patches in place the sections whose geometry changed, and when
widgets only moved keeps its draw calls and hands them the moved vertices and bounds. `DreamUI.Stats` counts which of
these each frame did (see [Canvas](canvas.md)).

## Render layers

A widget whose render transform keeps changing becomes a **render layer** of its canvas: the canvas keeps the geometry
under it relative to it and applies its transform on the GPU. While it turns or slides, nothing under it is transformed
again or uploaded again — only its row of the world's render layer table (`UDreamUIRenderLayerTable`) changes. Any number of
layers can share a draw call.

The price is batching: a layer's elements batch as 3D elements do, into the draw call just before them only, and are never
culled by the canvas rect. So a canvas makes layers only when it pays:

| `RenderLayer` on the widget | Behaviour |
| --- | --- |
| `Auto` (default) | Becomes a layer once its render transform has changed on `r.DreamUI.RenderLayerPromoteFrames` (2) frames in a row; stops being one after holding still for `r.DreamUI.RenderLayerDemoteFrames` (60) frames, so what is under it batches with the rest of its canvas again |
| `Always` | A layer whenever it can be, animated or not |
| `Never` | Never: what is under it is transformed on the CPU with the rest of its canvas |

A canvas makes at most `r.DreamUI.RenderLayerMaxPerCanvas` (8192) `Auto` layers, and `Always` ignores the count;
`r.DreamUI.RenderLayers 0` makes none and takes back those there are. Each promotion or demotion costs its canvas one
draw-call rebuild. Small text inside a layer draws from coverage glyphs only once the layer has held still for 3 frames
(see [Text](text.md)). The screen of 5000 turning buttons in `Tools/Bench` measures exactly this case (see
[Benchmarks](../tools/bench.md)).

## Materials on visuals

The built-in shader draws most things; a visual can carry a material of its own as well (a text's `OverrideMaterial`, an
image's `SetBrushFromMaterial`). The default materials are project settings (`DefaultUIMaterial`,
`DefaultRectBlockMaterial`), overridable by `DefaultMaterial` on a canvas, and *Use Built-in UI Shader*
(`bUseBuiltInUIShader`) decides whether elements with no material of their own go through the built-in shader. DreamGUI's
own materials are DreamShader output, built from the sources under the plugin's `DShader/`.

### A canvas answers parameters through render-thread proxies

A canvas makes **no material instance per draw call**. The parameters it gives a material — the main and font textures,
its data textures (clip data, widget properties, …), the renderer flag — are answered through a render-thread proxy of
that material (`FDreamUIMaterialProxy`). A material instance you give a widget is answered for the same way and never
written to, so the parameters you set on it stay yours, and ones you set after its first draw reach the screen. Nothing a
canvas draws with is an object in the level, and nothing of it is copied into a play session or a paste.

A visual of your own that needs parameters of its own overrides `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`,
and the canvas answers them for its proxy (this replaced the old `OnMaterialInstanceDynamicCreated`).

### Premultiplying, and `DreamUI_IsRenderByDreamUIRenderer`

DreamGUI's renderer **premultiplies itself**: the built-in shader emits premultiplied colour (rgb already scaled by
alpha), which is what makes the three blend modes three blend states rather than three shader permutations. The engine's
renderer does not. A material that may be drawn by either has to know which one is drawing it, so a canvas sets the scalar
`DreamUI_IsRenderByDreamUIRenderer` on every draw: `1` under DreamGUI's renderer, `0` otherwise. DreamGUI's own materials
use it to lerp between `rgb * opacity` (for the engine's renderer) and plain `rgb` (for DreamGUI's, which multiplies
itself).

**Note:**

  A canvas answers a material that uses any of its parameters — including a procedural material whose only one is the
  `DreamUI_IsRenderByDreamUIRenderer` scalar. Before 1.0.0 the canvas looked at texture parameters only, so such a material
  never got the flag and was premultiplied twice under DreamGUI's renderer, darker at its edges; a material that compensated
  for that now draws too light, so take the compensation out (see [Upgrading](../guides/upgrading.md)).

### Colour and blending

Colours are authored in sRGB (`#RRGGBB` in a `.dui` is sRGB), and the shader turns vertex colour linear internally. Which
values are blended depends on the target: in the viewport, where the shader gamma-corrects, DreamGUI blends
**sRGB-encoded values**; in a render target, whose sRGB format encodes after the blend, it blends in linear light. So a
material that composites over a known ground inside itself, and has to agree with DreamGUI's blending, does its maths on
sRGB-encoded values in the viewport and turns only the final straight colour linear. A multisampled canvas whose target is
sRGB blends in sRGB as well.

Anti-aliasing is `AntiAliasingMethod` (`None` or MSAA) and `MSAASampleCount`, for all three DreamGUI-renderer modes; where
the RHI reports no MSAA support the renderer falls back to none and warns once rather than pretending. Win64 is the only
platform built and run; see [Platforms](../guides/platforms.md).

## Render-target canvases

A render-target canvas is drawn by **a render command of its own**, with a graph of its own, after its sections change —
not inside one of its world's views — so it updates whether or not anything renders that world. The switches that put the
old ways back (`r.DreamUI.RTDrawer`, `r.DreamUI.MaterialWrappers`) are gone with them.

The `UDreamRetainerBox` behaviour is built on this. It is UMG's `URetainerBox`: it renders the widget's subtree into a
texture and reuses it on the frames in between (`Phase` / `PhaseCount` spread re-renders over frames, `RequestRender()`
forces one), using the canvas on the same widget with `bForceRenderToTarget` and the `WhenRequest` update mode. The
retained subtree is composited first and `GroupRenderOpacity` applied once to the result, so fading a group of overlapping
children does not show them through each other — which per-widget `RenderOpacity` cannot do.

## Screen effects

`DreamGUIExtensions` has three visuals that read and write the screen, all subclasses of `UDreamVisualPostProcess`:

| `.dui` tag | Class | Effect |
| --- | --- | --- |
| `BackgroundBlur` | `UDreamBackgroundBlur` | Blurs what is under it |
| `BackgroundPixelate` | `UDreamBackgroundPixelate` | Pixelates it |
| `PixelSort` | `UDreamPixelSort` | Pixel sorting (`SetMaxSortPasses` keeps the count within 1 to 512) |

They share one way to read, crop and write back the screen, on the render graph's textures. An effect of your own takes the
same road: in an `FDreamVisualPostProcessRenderProxy`, read the screen with `ReadScreen_RenderThread` and
`GrabRegion_RenderThread`, work in textures from `CreateWorkTexture`, and write back with `WriteBack_RenderThread`.

### Grabbing the result back: `PostProcessRenderElement`

The three above are visuals that **read the screen**. Two more go the other way — they **grab back the result image a
post-process computed** and show it as a piece of geometry where they stand:

| `.dui` tag | Class |
| --- | --- |
| `PostProcessRenderElement` | `UDreamPostProcessRenderElement` |
| `PostProcessRenderElementText` | `UDreamPostProcessRenderElement_Text` |

⚠️ **One precondition you must know** (the source marks it `NOTE!!!`): **it only works when the target post-process's
`RenderType` is `RenderTarget` and its `bUseFullSize` is `false`**. Otherwise **it shows nothing, and reports nothing**.

`UDreamPostProcessRenderElement`'s UV convention: `UV0` is the rect UV ((0,0) to (1,1)); for `UV1` see
[Vertex format](#vertex-format) below. It hands the "world → post-process" transform to the material through four static
matrix parameters (`DreamUI_World2PostProcess_Row1` … `Row4`) — that is how the result image lines up with this piece of
geometry.

⚠️ `PostProcessRenderElementText` is **not its subclass** — it lives in a **separate file** and **inherits `UDreamText`**:
it is the version whose host is a text visual. Note that its tag has **one underscore fewer** than its class name.

## Vertex format

Writing your own shaders or mesh modifiers, this is the one struct you need: `FDreamUIMeshVertex`, **exactly 64 bytes**.
A `static_assert` in the source pins it — the vertex declaration, the upload and the byte-for-byte geometry comparison all
assume there is no padding, **so this is ABI**.

| Field | Attribute | Notes |
| --- | --- | --- |
| `Position` | 0 | `FVector3f` |
| `Color` | 1 | `FColor` |
| `TextureCoordinate[0..3]` | 2–5 | UV0…UV3 |
| `TangentX` / `TangentZ` | 6 / 7 | Packed normals |
| `UV4` | **8** | The fifth texture coordinate, **placed last** |

`UV4` is new in 2.0 (the vertex went from 56 to 64 bytes). It **sits at attribute 8 rather than in the middle** so that
not one earlier attribute moved. A material reads it as `TexCoord(4)`; **text uses it for gradient paints** (where a
painted glyph quad sits in its gradient box), and everything else writes `(0, 0)`. Vertices made by the constructor,
`SetNum` or a whole-struct copy are right automatically; **only code that grows the vertex array first and then fills it
field by field has to write `UV4` itself**.

The two default UV channels have meanings of their own:

- **`UV0` is the texture coordinate**;
- **`UV1` is the coordinate of the widget's property data** — x is the clip data's coordinate in the data texture, y is
  the slice index into the texture array, used mostly by font rendering.

⚠️ `UV1` belongs to the canvas (and the canvas writes x back over it later in the same rebuild), so **a mesh modifier that
writes vertex positions into `UV1` loses half of them and breaks glyph lookup**. The other channels are defined by each
visual (`DreamText`, `DreamRectBlock` and so on).

**Text quads carry a paint slot in `UV2.x`**: `UV2.x += 128 * slot`. A text slot with no paint is 0, and its vertices are
the ones 2.0 writes. **A shader that decodes `UV2.x` must strip the slot first** (`DreamUIText_UnpackPaintSlot` in
`DreamUIText.ush`); **a mesh modifier that rewrites a text quad's `UV2.x` must keep it**.

There are also **five global buffer resources**: the full-screen quad's vertex buffer, its index buffer and the sliced
quad's index buffer (**all three `TGlobalResource`, one of each globally** — a `static` would make **one per `.cpp` that
includes it**, each holding its own set of GPU buffers), plus the mesh vertex buffer `FDreamUIMeshVertexBuffer` and the
vertex declaration `FDreamUIMeshVertexDeclaration`.

### Indices are 16-bit

Indices are **16-bit** by default (`FDreamUIMeshIndex` = `uint16`): half the memory and bandwidth for indices, at the
cost of **at most 65535 vertices per draw call** (about 16k quads). A single element that really needs more has to
define `LEXUI_USE_32BIT_INDEXBUFFER`.

This is also where the "vertex budget" comes from: when there are more vertices than one draw call's indices can hold,
a mesh modifier **reports an error once per modifier and does not draw that copy**.

## Checks

For a report of something drawn wrong or not drawn, two console variables help (the test suite runs with both on):

| Variable | Checks |
| --- | --- |
| `r.DreamUI.VerifyPartialPrepare 1` | Every prepare a canvas makes from its last one is checked against a prepare of every widget, and a difference is an ensure that names the canvas. It costs a full prepare each time |
| `r.DreamUI.VerifyKeptPointers 1` | The objects DreamGUI keeps instead of looking them up every frame — a widget's canvas, a canvas's render layers, the UI manager's canvases, an animated property's object — are looked up as well at each use, and a disagreement is an error and an ensure that says which |

`DreamUI.Capture` writes a PNG of the viewport and of every root canvas that renders into a target, `DreamUI.Stats`
prints what each stage cost, and `DreamGUI.Memory` lists font atlases, sprite atlas pages and each world's canvas mesh
sections. The whole routine is on [Debugging](../tools/debugging.md).

For custom shaders and mesh modifiers, start from [Vertex format](#vertex-format) above.
