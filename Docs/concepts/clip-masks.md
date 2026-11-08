---
title: Clip masks and clipping to the layer below
description: A mask on a clipping widget's clip — a shape, a feathered edge, a texture or a procedural pattern, inverted if asked — and a widget that shows only where the layer below it is drawn; what each costs, how they batch, and how they are hit-tested.
---

A widget that clips (`Clipping` is `ClipToBounds` or `ClipToBoundsWithoutIntersecting`) clips its whole subtree to its
rect, rounded by `ClippingCornerRadius`. Its **clip mask** (`ClipMask`, an `FDreamClipMask`) changes what that clip lets
through. A widget with **Clip To Layer Below** on (`bClipToBelow`) shows, with everything under it, only where the
sibling drawn before it is drawn. Both are evaluated per pixel by the same shader code wherever the UI is drawn — the
built-in shader, the UI materials, world space, render layers and perspective scopes — and both are hit-tested on the CPU
by the same formulas.

## The clip mask

| Property | Does |
| --- | --- |
| `Shape` | `Rect` (the clip rect itself, with the widget's corner radius), `Ellipse`, `Polygon` (`Sides`, `Roundness` in pixels), `Star` (`Sides` points, `InnerRatio`), `Ring` (`InnerRatio` is the hole) — each fitted to the clip rect, a corner or point straight up |
| `Feather` | Pixels the mask fades in over. A `Rect` fades each side over its own width, measured inwards; the other shapes use `Left` for the whole outline, centred on it |
| `Source` | `None` (the shape alone), `Texture`, or `Procedural` |
| `Texture`, `Channel`, `Fill` | The mask texture, the channel read (`Alpha`, `Red`, `Green`, `Blue`, `Luminance`), and how it covers the clip: `Stretch`, `Fit` (whole, aspect kept, nothing outside it), `Fill` (covering, aspect kept), `Tile` (its own size in pixels, repeated) |
| `Pattern`, `Frequency`, `Seed` | A procedural pattern: `Linear`, `Radial`, `Angular` (a clock wipe), `Diamond` run 0 to 1 across the clip; `Blinds`, `Checker` and `Noise` repeat `Frequency` times across it |
| `Rotation`, `Scale`, `Offset` | Turn (degrees clockwise, about the clip's centre), scale and move the texture or pattern. `Rotation` is a linear gradient's angle |
| `Threshold`, `Softness` | The source value `v` becomes `saturate((v - Threshold + Softness) / (2 * Softness))`. At `0.5` and `0.5`, `v` as it is; a small `Softness` cuts at `Threshold` — drive `Threshold` for wipes and dissolves |
| `bReverse` | Runs the texture's or pattern's value the other way (`v` becomes `1 - v` before the threshold): a gradient from right to left, a radial one from the edge in, a clock wipe anticlockwise. Unlike `bInvert` it leaves the shape, and what is outside the clip rect, alone |
| `bInvert` | Shows what the mask hides and hides what it shows — **outside the clip rect too**, which is what makes a spotlight: a veil clipped by an inverted ellipse covers everything but the ellipse |
| `HitTest` | `Bounds` (the default: hit-tested by the clip rect, as without a mask) or `Mask` (a point hits where the mask is at least one half) |

A mask at its defaults is no mask: the clip is the plain rect, and costs exactly what it always did. In the details panel
the mask is under **Appearance ▸ Clipping**; the designer outlines the selected widget's mask (the **Clip Masks** switch of
the viewport's toolbar).

A behaviour can drive a widget's clip mask in place of `ClipMask` while it runs (`UDreamWidget::SetDrivenClipMask`):
[mask transitions](mask-transitions.md) do. The driven mask is not saved, shown or written into `ClipMask`; while one is
driven, a widget that does not clip to its bounds does (Inherit as Clip To Bounds, Disabled as Clip To Bounds Without
Intersecting). `GetEffectiveClipMask` is the mask the clip is drawn and hit-tested with.

### Texture masks and draw calls

A draw call binds two mask textures, one per **slot**. The texture masks of a chain of clips take the slots from the
outside in: the outermost texture mask reads slot 0, the next slot 1, and a third one along the same chain is ignored
(its shape still clips) with a warning. Content under different mask textures is drawn in different draw calls; content
under the same mask texture, and content under no texture mask at all, batches as before.

`HitTest = Mask` reads the texture on the CPU when it can — a `Texture2D` whose pixels it has, uncompressed
(`UserInterface2D (RGBA)` or `VectorDisplacementmap`) — and otherwise counts it as white, with a warning. A render target
or a dynamic atlas cannot be read.

Colour channels are read as the texture is sampled: turn off sRGB on a mask that stores its values in red, green or blue.

## Clipping to the layer below

`bClipToBelow` is a clipping mask, as image editors have them:

- The **layer below** is the sibling drawn before the widget that does not clip to the layer below itself. Siblings that
  clip to the layer below in a row share one.
- The layer below draws as it always does. The widget and its subtree show only where it is drawn, through its opacity
  (its `Color` alpha and its own `RenderOpacity`). A hidden layer below hides them too; with no sibling before it, the
  widget is not clipped.
- Whatever clips the layer below — its parent's clip, its own mask — clips the widget as well. With
  `ClipToBoundsWithoutIntersecting` or `Disabled` the clips around are left out, the layer below is not.

The layer below can be a simple or nine-sliced image, texture or sprite (a nine-slice without its centre leaves its centre
empty), or a rect block (its body's rounded rect): the clip follows its shape. Anything else — text, a tiled or filled
image, a custom mesh, an element whose mesh modifiers reshape it, a container or a layer group — is clipped to through a
group on a screen-space canvas in a game and on a render-target canvas (see
[Blend modes and layer groups](blend-modes-and-groups.md)); on a world-space canvas it is not clipped to, with a warning
naming the widget.

Hit testing follows the drawn shape of the layer below: its quad (its texture's alpha is not read) or the rect block's
rounded body.

## What it costs

A clip level without a mask is five texels of the canvas's clip data, as before; a level with a mask or a layer below
four more, read only by the pixels it clips. A chain with no mask uploads exactly the bytes it did before. The material
path reads the mask textures as `DreamUI_ClipMaskTexture0` and `DreamUI_ClipMaskTexture1`, which the canvas sets on every
UI material (`MF_DreamUI_Shade`, `MF_DreamUI_Clip`); a custom material that calls `DreamUI_CalculateClipData` or
`DreamUI_ShadePixel` with the arguments it always passed still compiles, its texture masks taking every texel as white.
A background effect (`UDreamBackgroundBlur` and the other post-process visuals, and the [effects on what is
behind](backdrop-effects.md)) inside a masked clip is clipped by the mask's shape, feather, pattern and texture.
