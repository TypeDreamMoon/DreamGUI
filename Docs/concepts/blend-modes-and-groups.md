---
title: Blend modes and layer groups
description: The 27 blend modes an element or a group can composite by, which of them the blend hardware does and which read what is behind, and layer groups -- a subtree drawn apart and put down as one, faded, blended and masked as one; what each costs and where it works.
---

Every element that draws through a batch mesh -- an image, a texture, a sprite, a text, a rect block -- has a
`BlendMode` (`EDreamUIBlendMode`): how its colour is combined with what is already drawn. A **layer group**
(`UDreamLayerGroup`, a behaviour on a container) draws a whole subtree into a texture of its own first, then puts that
down as one, by its own blend mode, at its own opacity, through its own mask.

## The blend modes

| Group | Modes |
| --- | --- |
| Normal | `Alpha` (Normal), `Dissolve` |
| Darken | `Darken`, `Multiply`, `ColorBurn`, `LinearBurn`, `DarkerColor` |
| Lighten | `Lighten`, `Screen`, `ColorDodge`, `Additive` (linear dodge), `LighterColor` |
| Contrast | `Overlay`, `SoftLight`, `HardLight`, `VividLight`, `LinearLight`, `PinLight`, `HardMix` |
| Inversion | `Difference`, `Exclusion`, `Subtract`, `Divide` |
| Component | `Hue`, `Saturation`, `Color`, `Luminosity` |

The formulas are the W3C Compositing and Blending ones, `Cs' = (1 - Da) * Cs + Da * B(Cb, Cs)` composited source-over,
and the image editors' own for the modes W3C does not define (darker and lighter colour compare the sum of the channels;
hard mix is white where the two add up to one or more; dissolve keeps each pixel or drops it, as often as its alpha says,
in a pattern that moves with the widget). They blend **encoded** (sRGB) values, as image editors do: a viewport holds
encoded values already; on a linear render target both colours are encoded for the blend and the result decoded.

### Three ways a mode is drawn

DreamGUI picks the way from the mode and from what draws the element; there is nothing to set.

- **By the blend hardware** -- `Alpha`, `Additive`, `Multiply`, `Screen`, `Exclusion`, `Dissolve`. These batch like any
  element and are exact for a partly transparent element over an opaque backdrop. They blend whatever the target holds,
  so on a linear render target they differ a little from an image editor's result. `Multiply` is exact at any alpha:
  a half-transparent multiply darkens half as much (it used to blacken transparent pixels).
- **Over a copy of what is behind** -- every other mode. Before such a draw, the part of the target it covers is copied,
  and the shader blends the element over the copy. Elements of one such mode that do not overlap share a draw and a copy;
  elements that overlap are drawn one after the other, each over what the last left. `Subtract` and `LinearBurn` are
  here rather than in the hardware because the hardware clamps a partly transparent element's result where the formula
  does not.
- **Through a group of one** -- an element drawn through a material (its own, or the default one when the built-in shader
  is off) with any of the modes added after `Multiply`: the element is drawn into a texture and the texture put down by
  the mode. An element with a material and `Alpha`, `Additive` or `Multiply` composites the way its material says, as it
  always has.

## Layer groups

| Property | Does |
| --- | --- |
| `Isolation` | `PassThrough` (the default: drawn as if there were no group) or `Isolated` (drawn apart). An opacity, a mode or a mask other than the default draws it apart whatever this says |
| `GroupOpacity` | Applied once to the composited subtree: children that overlap fade together, not through each other as they do when each takes the widget's `RenderOpacity` |
| `GroupBlendMode` | How the composited subtree blends with what is behind it, any of the 27 |
| `GroupMask` | An `FDreamClipMask` measured in the container's rect, applied to the composite as it is put down. It does not take part in hit tests |

Inside a group drawn apart, a child's blend mode blends with what the group has drawn so far, never with what is behind
the group: a white square in `Difference` inside an isolated group inverts its siblings, and is white over nothing. A
background effect (`UDreamBackgroundBlur` and the other post-process visuals) inside a group reads the group's texture.
Groups nest. Changing a group's opacity, mode or mask redraws the composite only; switching it between drawn apart and
passed through makes the canvas's draw calls again.

### Clipping to a layer below that is not an image

[Clipping to the layer below](clip-masks.md) follows an image's or a rect block's shape in the clip itself. For any other
layer below -- a text, a tiled or filled image, a ring or polygon, a custom mesh, a container (the layer its children
make) or a layer group -- DreamGUI makes a group: the layer below is drawn into a texture and put down as it is, what
clips to it into a second texture, put down only where the first has alpha. Hit testing of what clips to such a layer
follows the layer's widget rect.

### Retainer box

`UDreamRetainerBox` puts its retained texture down through the same composite: by its `GroupBlendMode`, at its
`GroupRenderOpacity`, through the clip its `DisplayVisual`'s widget is drawn with -- that widget's own clip mask
included. With an `EffectMaterial` the texture is drawn through that material instead, as it always was.

## What it costs, and where it works

- A group drawn apart costs a texture the size of what it draws on the target (from the frame's pool, given back when the
  frame is drawn), a clear, and one pass over that rect to put it down; a mode that reads what is behind adds a copy of
  that rect. Nothing batches across a group's edges.
- A draw of a mode that reads what is behind costs a copy of the rect it covers, and splits its pass.
- Groups, the modes that read what is behind, and groups of one are drawn on **screen-space canvases in a game and on
  render-target canvases**. A world-space canvas -- the level editor's and the designer's previews among them -- draws
  groups as if passed through and those modes as Normal, with a warning naming the canvas; the hardware's six modes work
  everywhere. A screen-space canvas with groups, or with elements of those modes, is not drawn at a reduced
  `ScreenSpaceRenderScale`, and inside a group the screen-space depth test is off.
