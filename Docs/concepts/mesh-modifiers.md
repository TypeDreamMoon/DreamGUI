---
title: Mesh modifiers and text animation
description: Adding to the geometry a visual hands over — the seven modifiers, the three dimensions of the text-animation family and its twenty classes, and their vertex budget and UV channel constraints.
---

Once a visual has handed its geometry over, can you still touch it? That is what this page is about — **without changing
the visual or the layout**, moving only vertices, colours and UVs.

- A visual is "what it draws" → [The widget model](widgets.md)
- How text is laid out → [Text](text.md)
- **Whether it can still be touched once handed over** → this page

## What a modifier is

It is a **behaviour component** (`UDreamMeshModifierBase : UDreamUIBehaviour`), `Abstract` and Blueprint-spawnable, and
**it goes on a visual that builds a mesh**.

Three members define everything it can do:

| Member | Answers |
| --- | --- |
| `ModifyUIGeometry(geometry, four flags)` | Changes the mesh itself; the four flags say **which** of "triangles / vertex positions / UVs / colours" **changed this time** |
| `ModifierWillChangeVertexData(out four flags)` | **Which ones I will touch**: the Blueprint layer always answers all `true`; native subclasses should answer precisely |
| `GetDuplicatesMesh()` | **Whether I append a whole copy of the mesh** (outline / shadow / long shadow → `true`) |

**The second declaration is not decoration**; it has two consequences: **it saves work** (what is not declared is not
recomputed); and **text uses it to choose its draw path** — a modifier that declares it moves **vertex positions or UVs**
makes text **stay on the font's distance field**. The reason is that the small-text coverage glyphs in
[Text](text.md) are sampled 1:1 on the device pixel grid, and **move a vertex and they are off**.

So **the answer may err on the true side but must never leave anything out**: declaring too much only costs one more
recompute; **declaring too little loses writes**.

**The third declaration decides the order**: modifiers that append copies **always run after the ones that modify in
place, however you arrange the components**. A copy is **a photograph** — it freezes what the modifiers before it
produced, and its vertices are **outside every character's vertex range**, out of text animation's reach.

On the Blueprint side it goes through `ReceiveModifyUIGeometry`, and the `UDreamVisualBatchMeshModifierHelper` it gets
(derived from `UDreamUIGeometryHelper`) carries a set of `UITextHelperFunction_*` that change a single character's
transform / colour / alpha **by character index**; positions are either `Relative` (to the character's origin) or
`Absolute` (to the text's pivot).

## The seven modifiers

Modules checked against the export macros: `G` = `DREAMGUI_API` (core), `X` = `DREAMGUIEXTENSIONS_API`.

| Class (display name) | Module | Does | Main knobs (with defaults) |
| --- | :--: | --- | --- |
| `UDreamMeshModifierBase` | **G** | The base (`Abstract`), not for putting on a widget | `bEnable` (true) |
| `GradientColor` | X | **Grades the vertex colours** | `DirectionType`, five values; `Color1`/`Color2`; `Color3`/`Color4` (`FourCorner` only); `bMultiplySourceAlpha` |
| `LongShadow` | X | **Long shadow**: draws the silhouette out in several segments along a direction | `ShadowSize` (0,1,-1); `ShadowSegment` **5**; `bUseGradientColor` |
| `Mirror` | **G** | Mirrors around **the centre of the widget's rect** | `bMirrorHorizontally` (true) / `bMirrorVertically` (false) |
| `Outline` | X | **Outline**: lays a silhouette copy on every side | `OutlineSize` (1,1); `bUse8Direction` (false = 4 directions) |
| `Shadow` | X | **Drop shadow**: one offset silhouette copy | `ShadowOffset` (0,1,-1); `ShadowColor` (black) |
| `PositionAsUV` | X | Writes **vertex positions into a UV channel** | `UVChannel` **0**; `Scale` 1 |

⚠️ **Do not guess the module by family**: of the seven, **only the base and `Mirror` are in the core module**; the other
five (and the whole text-animation family) are in `DreamGUIExtensions`.

The three "copy" modifiers (`LongShadow` / `Outline` / `Shadow`) all have `GetDuplicatesMesh() = true`; they differ in
**how many copies**: `Shadow` **1**, `Outline` **4 or 8**, `LongShadow` **one per segment** (5 by default). The copies
**do not carry the source text's gradient paint**: their `UV4` is (0,0), and text copies have their paint slot
cleared. They still draw the shadow or outline; a colour glyph is rendered as a **silhouette** in the copy.

Three of them work on quite different principles:

- **Gradient** changes **vertex colours** in place and leaves positions and UVs alone — so it is cheap, and it does
  **not** force small text off its coverage-glyph path and back onto the distance field.
- **Mirror** **reflects** vertices around the rect's centre but **keeps the texture coordinates**. Reflecting one axis
  **flips** every triangle, so it **rewinds the triangles** — otherwise a back-face-culled material would draw nothing.
  It exists for a specific reason: UMG flips a background for **right-to-left** cultures with a `scale(-1,1)` render
  transform, and `UDreamLayoutContainerBorder` makes one of these for `bFlipForRightToLeftFlowDirection` to match.
- **Position as UV** is **the only one that changes UVs**. ⚠️ Channel ownership is the biggest trap: **`UV1` belongs to
  the canvas** (see "Vertex format" on [Rendering](rendering.md#vertex-format)), so `UV1` is **refused by
  both the setter and the write**, and the default was moved to `UV0` — the previous default was the canvas's channel,
  **broken out of the box**.

## Text animation is a modifier family

`UDreamMeshModifierTextAnimation` (display name `TextAnimation`) **inherits `UDreamMeshModifierBase`** — **all** of the
above applies to it. It adds two things on top of the base:

- **A `Selector`** (`Instanced`): decides **which characters are chosen** and gives each a **0–1 interpolation**. The
  selector's `Offset` is **the axis you animate**; the modifier's `SelectorOffset` is its proxy, **there for keying in
  Sequencer**.
- **A `Properties` array** (`Instanced`): decides **what changes and how**.

It also **registers with the text**: the text then **lays out character by character** and stops joining letters into
ligatures ("fi" becomes one glyph by default, and per-character animation has to split it first); it unregisters when the
visual changes or on unregistration.

Its own `ModifierWillChangeVertexData` makes one precise call: **only when a property that moves glyphs is installed** does
it declare positions and UVs — the built-in `Alpha`, `Color` and `ColorRandom` **only change colour and move nothing**, so
**a pure fade-in / colour change does not chase text off the small-text path**.

## Twenty classes

**20 by count**, combined **orthogonally** from three dimensions — one three-way table says it far more clearly than a
list:

| Dimension | Classes | Role |
| --- | --- | --- |
| **Intermediate bases** (3) | `_Property` / `_PropertyWithEase` / `_PropertyWithWave` | The root of properties (`Init` / `Deinit` / `ApplyProperty`) / adds **easing** (`EaseType` defaults to `InOutSine`) / adds a **sine wave** (`Frequency` defaults to 1, `Speed` to 1) |
| **Selectors** (4) | `_Selector` (abstract) / `RangeSelector` / `RandomSelector` / `RichTextTagSelector` | Choose by a 0–1 **position range** / **at random** / by a range from a **custom rich-text tag** |
| **Properties** (12) | Alpha 1, Color 2, Position 3, Rotation 3, Scale 3 | Each spread over "plain / random / wave" |

**20 = 1 base + 3 intermediate bases + 4 selectors + 12 properties.** "Plain" and "random" inherit `_PropertyWithEase`,
"wave" inherits `_PropertyWithWave` — **easing / wave come from the intermediate-base dimension, not written once per
property**.

⚠️ `_PropertyWithWave` has **a fixed historical bug**: `Frequency` (higher values shorten the wavelength) and `Speed`
(wave speed) **used to be read and written crossed over** — `SetFrequency` actually moved `Speed`. With that fixed,
**calling `SetFrequency` from a
Blueprint silently behaves differently**: the node is still there and still compiles; it just moves a different property.
Assets are not affected (both are `EditAnywhere` and serialize separately).

## Using it

Add a modifier component to a visual widget that builds a mesh (`bEnable` is on by default). A selector plus properties:

```cpp
auto* Anim  = Text->AddComponent<UDreamMeshModifierTextAnimation>();
auto* Sel   = NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Anim);
Sel->SetStart(0.f);
Sel->SetEnd(1.f);
auto* Alpha = NewObject<UDreamMeshModifierTextAnimation_AlphaProperty>(Anim);
Anim->SetSelector(Sel);
Anim->SetProperties({ Alpha });
// Sequencer pulls SelectorOffset from 0 to 1, and the characters light up one by one
```

When `SetProperties` / `SetProperty` swap properties while **registered**, the old ones get `Deinit` and the new ones
`Init`.

## When it does not serve

- **Copy modifiers eat vertices**: `Outline` 4 or 8 copies, `LongShadow` one per segment. When there are more vertices
  than one draw call's indices can hold (see "Indices are 16-bit" on
  [Rendering](rendering.md#indices-are-16-bit)), the source **reports an error once per modifier and does
  not draw that copy** — when "the shadow suddenly went away", look at the vertex budget first.
- **The order is not yours to choose**: modifiers whose `GetDuplicatesMesh` is true **always run after the in-place
  ones**.
- **`PositionAsUV` cannot write `UV1`**; writing `UV0` **replaces the image**; whether `UV2`/`UV3` are free depends on
  **the specific visual**.
- **A modifier that moves vertices takes text off the small-text coverage glyph path** (back to the distance field);
  `Alpha` / `Color` / `ColorRandom` are the exceptions.
- **Mirror only works around the centre of the widget's rect**, and a reflection flips faces, fixed by rewinding the
  triangles — remember this especially with back-face-culled materials.
- **A modifier has to go on a visual that builds a mesh**, and text animation on a widget with `Text`.

## Not yet confirmed

- This area has **no `Docs/Reference/` page at all** (a search for MeshModifier is empty); all of the above comes from the
  source headers — for complete per-class property tables, see the generated reference pages.
- How the third component of `LongShadow`'s `ShadowSize` (Z = −1) relates geometrically to the segment count has **not
  been checked line by line**.
- The mapping of `GradientColor`'s `FourCorner` four colours to the four corners has **not been checked line by line**.
