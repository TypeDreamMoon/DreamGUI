---
title: Custom meshes, sprites and atlases
description: The visual table says "what it draws"; this page says "where that geometry and image data comes from" — custom meshes, sprites and two kinds of atlas, data textures, the static mesh cache.
---

The visual table on [The widget model](widgets.md) answers **what a widget draws** once it is on. But:

- a `Sprite` needs **a piece of sprite data** to know which small part of the atlas to draw;
- a `StaticMesh` needs **baked mesh data** to draw anything;
- some geometry **is not a rect at all** — a cylinder surface, a plane lifted along a curve, a polyline — and it is
  **computed on the spot**.

**This page is about those "data side" classes.**

⚠️ Of all this, **only three have a shell you can write in a `.dui`** — the `Sprite` / `Texture` / `StaticMesh` tags
(already in the visual table). The rest is "the thing behind the tag", so **most of this page is on the C++ and asset
side, not the language side**.

## Custom meshes: geometry **outsourced** to a "source"

`UDreamCustomMesh` is a subclass of `UDreamVisualBatchMesh` that **does not draw a rect itself**: filling the geometry is
**outsourced** to a "mesh source" — the `CustomMesh` property (`Instanced`, can be created inline). The class comment is
one sentence: **use a mesh generator to make your own mesh instead of a simple rect**.

The source is `UDreamUICustomMeshSource` (an abstract base, `EditInlineNew`). Its entry point for filling geometry is
**`OnFillMesh`** — the `UDreamCustomMesh` being drawn hands the geometry object to the source in its own
`OnUpdateGeometry`, then calls `OnFillMesh`.

The source's default implementation **does nothing**; it is an empty shell **for Blueprints or subclasses to implement**:
three methods, `OnFillMesh` / `GetHitUV` / `SupportDrawcallBatching`, which forward to the Blueprint events
`ReceiveOnFillMesh` / `ReceiveGetHitUV` / `ReceiveSupportDrawcallBatching` when the class was compiled from a Blueprint.

Filling geometry uses the **`UDreamUIGeometryHelper`** toolkit (`AddVertexSimple` / `AddVertexStruct` / `AddTriangle` /
`SetMesh` / `AddVertexTriangleStream`) — it is **the base class** of `UDreamUICustomMeshSource`.

⚠️ **A source computes its geometry from the widget's width, height and pivot** (`Widget->GetWidth()` / `GetPivot()`),
**not from a rect of its own** — so the shape a source makes **scales with the widget's rect**.

**Two built-in sources**:

| Source (display name) | Main knobs | Notes |
| --- | --- | --- |
| `DreamCustomMeshSource Cylinder` | `CylinderArcAngle` (−180…180, default **45**) | Bends the rect into a stretch of cylinder surface: **radius = width / arc angle**, with the segment count interpolated between 4 and 32 by the arc angle |
| `DreamCustomMeshSource CurvyPlane` | `Segment` (1…200, default **10**), `ShapeCurve`, `CurveScale` (default **100**) | Raises / lowers along the width by a curve; the curve's default keys are `(0,0) → (0.5,1) → (1,0)` |

Both sources **should only be used unbatched** (the bend and the cylinder are 3D geometry, and batching would judge them
together with 2D elements wrongly).

### Why it deliberately has no `.dui` tag

`UDreamCustomMesh` has **no** registered tag. The source explains why at the end of the file:

> **It draws "what its source hands it", and the language has no way to hand it a source** — a `CustomMesh` tag would only
> make a widget that "**draws nothing and does not tell you why**".

Compare `UDreamUMGWidget`: it **derives** from `UDreamCustomMesh` but **does** have a tag — because the widget class it
hosts is **a property**, and a property is something a `.dui` can spell.

⚠️ This is **not an omission** but a choice an automated test **guards in both directions**:
`EveryVisualIsSpellableOrIsDeliberatelyNot` walks every concrete visual subclass — one without a tag **must** be on a
"deliberately not registered" list, and if a class on that list ever gets a tag, **the test fails too**.

## The mesh that holds the geometry

**`UDreamUIMeshComponent`** is not a visual for users to put on, but **the mesh the canvas holds its geometry in**: it
inherits `UMeshComponent`. Its render sections come in **four kinds**: `Mesh`, `DirectMesh`, `PostProcess`, `ChildCanvas`
— exactly the three draw paths plus a "child canvas".

**In-place patching** (`TryPatchMeshSection`) and **claiming pooled sections** (`ClaimPooledMeshSections`) both live here.
It is also where `DreamGUI.Memory` gets the byte counts of mesh sections.

**`UDream2DLineRendererBase`** (`DreamGUIExtensions`, abstract) is the shared base of the 2D line family (`Line2DRaw` /
`Line2DChildren`). It **inherits `UDreamImage`** — **lines are drawn with a brush too**; their vertices just are not a
rect.

| Property | Default | Notes |
| --- | --- | --- |
| `LineWidth` | **10** | Line width |
| `EndType` | `Cap` | `None` / `Cap` (a square on each end) / `ConnectStartAndEnd` (joins the ends) |
| `LineWidthOffset` | **0.5** | Pushes the line width from left to right, **for a sense of flow** |

⭐ One trade-off written in a comment: it **abstains** from "how big do I want to be", because **a brush cannot say how
long a line is** — a line is **computed from its points**.

## Sprite data

**A sprite is "a small piece of a big atlas".** `UDreamUISpriteData` stores that piece: `SpriteTexture`, `SpriteInfo`
(which part of the source image it takes, its border and padding), and packing settings. The base
`UDreamUISpriteData_BaseObject` only defines the interface.

Worth remembering in `FDreamUISpriteInfo`: `Width` / `Height`, `Border` / `Padding` (an `FMargin` each), and four UV
rects (the last two for the **nine-slice** border).

⚠️ **Colour is not on the sprite** — it is on the visual.

**Two kinds of packing**:

| `PackingType` | When it is packed | Mipmaps |
| --- | --- | --- |
| `Static` | In the editor and at cook | ✅ |
| **`Dynamic`** (default) | When used | ❌ |

⭐ **`PackingTag` is the key switch**, `"Main"` by default; sprites with the same tag are packed into the same set of
atlases.

⚠️ **Set it to `None` and the sprite is no longer treated as a sprite**, but as a plain `Texture` (the `Texture` tag in the
visual table).

⚠️ **When dynamic packing fails** (an image too big, say), the source **clears `PackingTag` to `None` automatically** so it
falls back to a texture of its own, **and logs a warning**.

## Two kinds of atlas

**Static atlases**: `UDreamUIStaticSpriteAtlasData` packs several sprites into one atlas texture (or an **array** of them)
**in the editor** and **at cook**. So **there is no packing cost at run time** and **mipmaps are supported**; the price is
that **nothing can be added at run time**. `MaxTextureSize` defaults to **2048×2048** — **beyond it, packing is given
up**. It is `NotBlueprintable` / `NotBlueprintType` and **can only be an asset**.

**Dynamic atlas packing**: a "dynamic atlas" is grouped by `PackingTag`, **each group being several pages** (one square
texture per page), with a **MAXRECTS bin packer** per page deciding where sprites go (the data is in
`FDreamUIDynamicSpriteAtlasData`). It is managed by **`UDreamUIDynamicSpriteAtlasManager`** — a **`UEngineSubsystem`**,
keyed by `PackingTag`.

**Why an engine subsystem**: the header comment says it **used to be** an object rooted in a static that nobody ever
unrooted — **it outlived every world and was only torn down at process exit**. It became an engine subsystem because the
engine then creates it, tears it down, and holds it across GC.

### Packing order: reuse first → reclaim next → add a page last

1. Try **the existing pages** first;
2. if it does not fit, **reclaim** sprites nobody draws to make room, and try again;
3. if there is still no room, **only then add a new page**.

The comment on step 2 is blunt: **reclaim before adding a page** — otherwise "**a long session keeps climbing in video
memory and never comes back down**". So these atlases **reclaim by themselves**, and **it happens without your calling
any API**.

### How memory goes back

**MAXRECTS has no "give a rect back" operation** — a rect the packer has handed out **cannot be taken back**. So the
**only** way to reclaim is: **rebuild the page with the sprites still alive**.

**What counts as "still alive" is not guessed**: `RenderSpriteArray` is every element currently **drawing from this
atlas** — **a sprite no one names is one that can be reclaimed**.

| Action | Does |
| --- | --- |
| `TouchSprite` | Keeps the **LRU order**; **reclaiming picks from the oldest** |
| `DropEmptyPages` | Only drops **trailing** empty pages, and **always keeps the first** |
| `NotifyRenderSpritesAtlasChanged` | After a rebuild, **notifies each** element still drawing — the rebuild moved their rects, so **all their UVs are stale** |
| (on rebuild) | **Does not clear pixels** — pixels a reclaimed sprite left behind are sampled by no one and will be overwritten by the next one placed there; **clearing means uploading the whole page, which is too expensive** |

There are three levels of explicit release: `DisposeAtlasByPackingTag` (⚠️ the default tag `"Main"` **may not** be
disposed), `TrimAtlasByPackingTag` / `TrimUnusedAtlases` (releases pages only when nobody in the whole group draws, but
**keeps the entry**), and `EvictUnusedSpritesByPackingTag` (only reclaims sprites **nobody draws**).

⚠️ **A dynamic atlas does not shrink by itself** (except through the reclaim the "add a page" step triggers) — to be sure
memory goes back, call `Trim*` yourself.

## Data textures: letting the GPU read what the CPU computed

The CPU computes a lot of **per-widget rows / blocks** of data (widget properties, clip data), and **shaders have to be
able to read it**. The way is to feed it to the GPU **as a texture**.

| Class | Role |
| --- | --- |
| `UDreamUIDataAsTexture` | A `UDataAsset` that hands the texture out **in blocks**: `RegisterBuffer` / `UpdateBlock` / `UnregisterBuffer` |
| `UDreamUIDataTexture` | The **real texture** underneath, a `UTexture` subclass that **grows in place** |

⚠️ **Do not confuse this with the "material proxies" on [Rendering](rendering.md)**: those are about **who
answers parameters**; this is about **which texture the data sits on**.

### What `UDreamUIDataTexture` buys

It replaces `UTexture2DDynamic`. The difference is **growing in place**:

> When the rows run out, the render thread swaps it for a **taller new texture**, **moves the same rows across**, and
> **the texture reference** (which materials, material instances and built-in shaders all bind) **points at the new one
> in the same command**.

→ So **whatever samples it follows along without being told** — no resetting parameters, no rebuilding draw calls, no
RHI flush.

The old `UTexture2DDynamic`, by contrast, **swapped in a new object**, so **every material instance and every
render-thread copy had to hear about it** — and one that did not would draw with **a texture GC had collected**.

The block format is `EDreamUIDataAsTexturePixelFormat` (`R8` / `R16` / `R32` / `R8G8B8A8` / `R16G16B16A16` /
`R32G32B32A32`). **By default it does not write block by block but batches**: `PrepareForBatchUpdate` → several
`UpdateBlock` → one `Flush` sends them all.

Writes go through `Upload`, a batch of `FDreamUIDataTextureUpdate` — each one **a run of pixels on one row**, or the same
run repeated over several rows.

⚠️ Its size is **deliberately not a property**: it is made in the transient package at run time and never saved or
duplicated. Size goes through `Initialize` / `Grow` (**it never shrinks**).

## The static mesh cache

To draw a static mesh in UI (the `StaticMesh` tag), **a `UStaticMesh`'s data cannot be read at run time** — so you first
make a **`UDreamUIStaticMeshCacheData`** that bakes the mesh **in the editor** into vertices and indices UI can draw.

It caches an array of `FDreamUIStaticMeshVertex` (position, two tangents, vertex colour, UV0–UV3), the indices, the
materials and the bounds. `EnsureValidData()` **does nothing in a cooked build** (the data was baked long before).

The baked data feeds `UDreamStaticMesh`, with three knobs: `MeshCache`, `VertexColorType` (default `NotAffectByUIColor`)
and `ReplaceMaterial`. Its **preferred size comes from the mesh's bounds**.

⚠️ When there are more vertices than one draw call's indices can hold, `HaveValidData()` returns `false`, **reports an
error once per mesh**, and **stops updating**.

## Sequence players

Sprites and textures can both be **played frame by frame**. Three classes:

| Class | Module | Needs which visual |
| --- | --- | --- |
| `UDreamImageSequencePlayer` (**`Abstract`**) | `DreamGUI` | — |
| `UUISpriteSequencePlayer` | `DreamGUI` | A `Sprite` visual |
| `UUISpriteSheetTexturePlayer` | `DreamGUIExtensions` | A `Texture` visual |

The base's knobs:

| Field | Default | Notes |
| --- | --- | --- |
| `Fps` | **24** | ⚠️ `ClampMin = 0.0001` — the source comment says it "must stay above zero" |
| `bLoop` | **true** | |
| `bPlayOnStart` | **true** | |
| `bAffectByGamePause` / `bAffectByTimeDilation` | **false** / **false** | The same defaults as `UDreamUIPlayTween` |
| `bPreviewInEditor` | **true** | Editor only |

Internally it advances with a `TWeakObjectPtr<UDreamTweener>` — **so it is built on the tween engine** (see
[Animation](animation.md)).

Each subclass has knobs of its own: `UUISpriteSequencePlayer` has `SpriteSequence` (an array of sprite data) and
`bSnapSpriteSize` (default **true** — whether to set the size to the sprite data's as well); `UUISpriteSheetTexturePlayer`
has `WidthCount` / `HeightCount` (how many cells across / down one texture holds, **8** each by default).

## When it does not serve

- **`CustomMesh` is not in `.dui`** — on purpose, and guarded by a test.
- **A custom mesh's source reads the widget's width and height**, so the shape scales with the widget's rect; to keep a
  fixed shape, control the widget's size yourself.
- **Cylinders and curvy planes are 3D geometry**; do not expect them to share draw calls with 2D elements.
- **A sprite with `PackingTag = None` is not a sprite**; a failed dynamic pack also clears it to `None` **automatically**.
- **Dynamic atlases do not shrink by themselves**; to be sure memory goes back, call `Trim*` yourself.
- **`DisposeAtlasByPackingTag` may not touch the default `"Main"`**, and before calling it make sure nothing on screen is
  drawing from those pages.
- **Static atlases cannot take sprites at run time**; beyond `MaxTextureSize`, **packing is simply given up**.
- **The static mesh cache cannot read mesh data at run time**; it has to be baked in the editor first.
- **`UDreamUIDataTexture` only grows** (`Grow` never shrinks).

## Not yet confirmed

- `UDreamUIDataTexture` / `UDreamUIDataAsTexture` carry a **2026 copyright** (`TypeDreamMoon`), while the sprite / atlas
  batch is 2019 (`LexLiu`); the `CHANGELOG` does not name them.
- Reserved `PackingTag`s have only been checked as far as the default `"Main"`; whether there are other reserved tags has
  **not been checked one by one**.
- When dynamic atlas reclaiming is triggered (other than "before adding a page fails") has **not been read through in the
  implementation**.
- This area has **no reference page at all** (searches for sprite / atlas / custommesh / mesh are all empty); everything
  above comes from the source headers.
