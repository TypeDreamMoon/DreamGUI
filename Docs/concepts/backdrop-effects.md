---
title: Effects on what is behind
description: Adjustment layers, progressive blur, refractive glass and stylize effects -- visuals that take the pixels behind them and draw them back made over, as far as their rect, clips and masks cover; an adjustment or a stylize effect can work on its own widget's children instead. What each one does, how it is mixed, what it costs and where it works.
---

Four visuals work on what is already drawn behind them: they copy the pixels their rect covers (and the room their effect
reads round it), work on the copy, and draw the rect back over the target -- as far as their clips, their clip masks
(textures included) and their mask texture let them. Everything outside that is left exactly as it was.

| Visual | Does |
| --- | --- |
| `UDreamAdjustmentLayer` | Up to eight colour operations in order, mixed with what they were made from by a blend mode and an opacity |
| `UDreamBackgroundBlur` with a `StrengthMap` | A blur whose strength varies across the rect: frosted at the top and clear at the bottom, clear in the middle |
| `UDreamGlass` | A slab of glass: its edge bends what is behind, optionally frosted, tinted, lit at the edge |
| `UDreamStylizeEffect` | Up to eight layers in order: CRT, glitch, chromatic aberration, halftone, dither, film grain, vignette, posterize |

All four are post-process visuals (`UDreamVisualPostProcess`): put one on a widget like any visual; its rect is the
widget's. They are drawn by DreamGUI's own renderer -- screen-space, render-target and world-space (DreamUI renderer)
canvases, and the designer's and the level editor's previews. A canvas the engine's renderer draws (World Space - UE
Renderer) draws none of them. The post-process visual's `Tint` and its output render target are not used by these four.

Every post-process visual has a `bEnabled` switch: off, it draws nothing and what it covers shows as if it were not there
-- an adjustment or a stylize effect on its children draws them as they are -- every setting kept.

## Adjustment layer

The operations (`Adjustments`, an array of `FDreamAdjustment`), each working on what the one before made:

| Operation | Parameters |
| --- | --- |
| `BrightnessContrast` | `Brightness` shifts every channel; `Contrast` turns the line about middle grey (-1 flat grey, 1 a step) |
| `Levels` | `InputBlack`, `InputWhite`, `Gamma`, `OutputBlack`, `OutputWhite` |
| `Curves` | `Curve`: a colour curve asset's R, G and B, one curve per channel; `MasterCurve` (a float curve) before them |
| `Exposure` | `Exposure` in stops, `ExposureOffset`, `ExposureGamma`, in linear light |
| `HueSaturation` | `Hue` in degrees, `Saturation` and `Lightness` from -1 to 1; `bColorize` gives every colour the hue and saturation given |
| `Vibrance` | `Vibrance` raises saturation most where there is least; `VibranceSaturation` raises it alike |
| `ColorBalance` | `Shadows`, `Midtones`, `Highlights` (each -1 to 1 towards red, green, blue); `bPreserveLuminosity` |
| `BlackAndWhite` | A weight for each primary and pair of primaries (`Reds`, `Yellows`, `Greens`, `Cyans`, `Blues`, `Magentas`), a `TintColor` at `TintStrength` |
| `PhotoFilter` | `FilterColor` multiplied in at `Density`; `bPreserveLuminosity` |
| `ChannelMixer` | Each output channel's mix of the inputs (`RedMix`, `GreenMix`, `BlueMix`) plus `MixConstants`; `bMonochrome` |
| `Invert` | |
| `Posterize` | `PosterizeLevels` |
| `Threshold` | `ThresholdLevel`, by luminosity |
| `GradientMap` | `Gradient`'s stops along the luminosity (its type and angle are not used); `bReverse` |
| `LookupTable` | A texture in the engine's colour grading layout (256 by 16) at `Intensity` |

The operations work on encoded (sRGB) colours, as the blend modes do. What they made is mixed with what they were made
from by `EffectBlendMode` (all 27 modes, by the same formulas as an element's) and `Opacity`, times the visual's own alpha.
The mix keeps each pixel's alpha: an adjustment over a transparent part of a render target leaves it transparent, as an
image editor's adjustment layer adds no pixels. Over an opaque backdrop that is the W3C composite exactly; over a partly
transparent one an element of the same mode would add alpha where the adjustment does not.

`Target` decides what it works on:

- `Backdrop` (the default): what is behind its rect.
- `Children`: its own widget's subtree. The widget is drawn as a group -- its children into a texture of their own -- and
  the group's picture goes through the operations before it is put down; the backdrop is not touched. A
  `UDreamLayerGroup` on the same widget makes it the same group, its opacity, mode and mask applied after the effect.

## Progressive blur

`UDreamBackgroundBlur::StrengthMap` (`FDreamBlurStrengthMap`) multiplies the blur's `BlurStrength` at each point of its
rect:

| Kind | Strength |
| --- | --- |
| `None` | The same everywhere: the uniform blur, drawn as it always was |
| `Texture` | A texture's channel stretched over the rect |
| `Linear` | From `StartStrength` at `Start` to `EndStrength` at `End` (fractions of the rect from its top left) |
| `Radial` | `StartStrength` out to `InnerRadius` from `Start`, `EndStrength` from `OuterRadius` (fractions of the rect's height) |
| `Procedural` | One of the clip masks' patterns (`Pattern`, `Frequency`, `Seed`) from `StartStrength` to `EndStrength` |

A map other than `None` builds a blur pyramid over the copy (the dual filter, up to `MaxLevel` levels, each twice as wide
as the one before) and each pixel takes the level its strength asks for, mixing the two levels on either side. It is not
drawn full size or into an output render target; those fall back to the uniform blur.

## Glass

`UDreamGlass` is a rounded rect of its widget's size, its corners a rect block's (`CornerRadius`, in units or as a fraction
of half the smaller side), or a shape blend's melted shape (below). A click outside the shape misses it.

| Group | Properties |
| --- | --- |
| Refraction | `EdgeWidth` (the band along the edge that bends), `Refraction` (how far a pixel at the very edge looks further in, in units), `Profile` (squircle, circle or linear rise), `Dispersion` (red and blue bend that much less and more) |
| Frost | `Frost`: 0 clear, 1 the blur pyramid's strongest level |
| Tone | `GlassTint` at `GlassTintStrength`, `Saturation`, `Brightness` |
| Highlight | `HighlightColor`, `HighlightStrength` and `HighlightWidth` along the edge facing `LightAngle` (0 from above, clockwise, and a little on the far side); `ShadowWidth`, `ShadowStrength` inside the edge |

Its flat middle -- further in than `EdgeWidth` -- shows what is behind as it is (frost and tone aside). Its alpha fades it.
In the designer, its **Glass** handles drag its corners, its edge's width and its light in the viewport (see
[Viewport handles](../tools/designer.md#viewport-handles)).

With `Shape` set to `ShapeBlend` the glass takes the shape a shape blend group melts its rect blocks into (see
[Shape blends](layer-styles.md#shape-blends)): the group on the widget `ShapeBlendGroup` names, or on the glass's own widget
when it names none. The glass is drawn over the whole melted shape, wherever its members reach, its own rect only the frame
the shape is measured in; a click lands on it wherever the shape is. The members still draw the shape in their own colours,
so rect blocks with a transparent body leave the glass alone. Move them and the glass follows by the next frame. While the
group is off, or has no rect block among its children, the glass is its rounded rect.

Measuring the melted shape is a pass over every member, three times a pixel (the distance and its slope), so a large glass
over many members costs more than a rounded rect; plain glass does not carry the shape's 2 KB of constants, which only its
own shader permutation reads. A quad that grows or shrinks as members move rebuilds the canvas's draw calls that frame.

## Stylize effect

`UDreamStylizeEffect::Layers` (`FDreamStylizeLayer`), each a pass over what the layers before it made:

| Layer | Parameters |
| --- | --- |
| `CRT` | Scanlines (`ScanlineStrength`, `ScanlineSpacing`), the screen's `Curvature` (black past its edge), a phosphor mask (`PhosphorStrength`, `PhosphorSize`), flicker (`FlickerStrength`, `FlickerFrequency`) |
| `Glitch` | `GlitchIntensity`; blocks shifted (`BlockSize`, `BlockShift`), lines jittered (`LineJitter`, `LineDensity`), red and blue split (`ColorSplit`), changing `Rate` times a second (0 holds still), `Seed`. `TriggerBurst(Duration, Intensity)` flares it, fading over the duration |
| `ChromaticAberration` | `AberrationAmount`, `Radial` from the middle or `Directional` along `AberrationAngle` |
| `Halftone` | `Dots` or `Lines`, one ink on `PaperColor` or four (`bCMYK`), `CellSize`, `ScreenAngle` |
| `Dither` | A 4 by 4 or 8 by 8 Bayer `MatrixSize`, `Levels`, `PixelSize` |
| `FilmGrain` | `GrainIntensity`, `GrainSize`, `bAnimateGrain` (a new grain 24 times a second) |
| `Vignette` | `VignetteIntensity`, `VignetteRadius`, `VignetteSoftness`, `VignetteRoundness`, `VignetteColor` |
| `Posterize` | `Levels` |

Sizes are in units of the canvas, so the look keeps its size with the UI's scale. Scanlines, the halftone screen, the
dither and grain are laid on the target's pixels and stay put as the element moves; the CRT's curve, the vignette and
radial aberration are laid on the element's rect. What moves moves with the world's time. `Opacity`, `EffectBlendMode`
and `Target` work as the adjustment layer's.

The pixel sort stays the visual of its own it always was.

## Inside a layer group

A `UDreamBackgroundBlur` or any other post-process visual inside a container that is drawn apart -- a `UDreamLayerGroup`
with a `GroupOpacity` below 1, a `GroupBlendMode` other than Normal, a `GroupMask` or `Isolation = Isolated`, and a widget
clipped to a layer below that is not an image or a rect block -- reads the **group's texture**, which holds only what the group has drawn so
far. It cannot see the scene behind the group, so a blur there has nothing to blur and comes out flat grey.

Put the effect outside the group: a sibling behind it, covering the area (the whole screen, for a full-screen panel), and
shape it where the group's mask would have: a `StrengthMap` of `Kind = Texture`, with the mask's texture as the strength.
A `ClipMask` on a container's own clip is another matter: it is applied per pixel while the container is drawn, not by
drawing it apart, and an effect inside it still sees the backdrop, clipped by the mask. A container's `RenderOpacity` is not
a group either (each child fades by itself); only a group's own `GroupOpacity` is.

A material of your own on a visual inside such a clip, or under a mask transition, is clipped only if it shades through
`MF_DreamUI_Shade` (take the alpha it returns as the material's opacity) or calls `DreamUI_CalculateClipData`; one that
does neither draws whole, as if there were no clip. See [Clip masks](clip-masks.md).

## Pixelate strength

`UDreamBackgroundPixelate::PixelateStrength` runs from 0 to 100 and is not a block size. The block it draws is

```text
block = (strength / 100)^2 * 100 + 1      pixels, a strength of 0 or less draws nothing
```

| Strength | 10 | 20 | 30 | 50 | 70 | 100 |
| --- | --- | --- | --- | --- | --- | --- |
| Block, pixels | 2 | 5 | 10 | 26 | 50 | 101 |

The square keeps the low end fine, and it makes a **linear fade of the strength look fast and then slow**: the first tenth
of the range doubles the block from 1 to 2 and the last 30% doubles it again from 50 to 101, and the eye reads the ratio.
For an even fade tween your own value `t` from 0 to 1 and set the strength to `10 * sqrt(101^t - 1)` from the tween's update
callback, which grows the block geometrically. With `ApplyAlphaToStrength` on (the default) the strength is also multiplied
by the visual's alpha, so fading the alpha fades the strength as well.

## What it costs, and where it works

- Each of these copies the rect it covers on the target, plus what its effect reads round it (a blur's reach, a glass's
  refraction, a glitch's shift), and draws the rect back in one pass. An adjustment adds one pass over the copy, a
  stylize effect one per layer (and one to mix, below full opacity or in another mode), a progressive blur or frosted
  glass the pyramid's passes at half the size and below.
- `Target = Children` costs what a layer group drawn apart costs, plus the effect's passes over the group's texture.
- On a multisampled target the copy is the whole target, resolved.
- A post-process visual inside a clip whose mask reads a texture is clipped by that texture too -- the uniform blur, the
  pixelate and the pixel sort included.
