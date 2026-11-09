---
title: Layer styles, shape blends and light
description: An image editor's layer styles on any widget -- drop shadows, glows, inner shadows, bevels, satin, overlays and strokes, each by its own blend mode; rect blocks melted into one shape by a smooth minimum; and a light that follows the pointer over bevels and borders, a tilting card and a holo foil.
---

## Layer styles

`UDreamLayerStyle` is a behaviour that puts an image editor's layer style on a widget (`FDreamLayerEffects`):

| Effect | Settings |
| --- | --- |
| Drop shadows (any number) | blend mode, colour, opacity, angle (or the global light), distance, spread, size, contour, noise, knocked out by the layer |
| Outer glow | blend mode, opacity, noise, colour or gradient, softer or precise, spread, size, contour, range, jitter |
| Inner shadows (any number) | as a drop shadow, with choke for spread |
| Inner glow | as the outer glow, from the edge or from the centre, with choke |
| Bevel and emboss | inner, outer, emboss, pillow emboss, stroke emboss; smooth, chisel hard or soft; depth, up or down, size, soften, angle and altitude, gloss contour, highlight and shadow modes, colours and opacities, contour and range |
| Satin | blend mode, colour, opacity, angle, distance, size, contour, inverted |
| Colour overlays (any number) | blend mode, colour, opacity |
| Gradient overlays (any number) | blend mode, opacity, gradient (colour and opacity stops, midpoints), linear, radial, angle, reflected or diamond, angle, scale, offset, reversed, dithered |
| Pattern overlay | blend mode, opacity, pattern texture, scale, offset |
| Strokes (any number) | size, outside, inside or centred, blend mode, opacity, filled with a colour, a gradient or a pattern |

The style also has a `Scale` for every size and distance, a `FillOpacity` that fades the widget's own pixels and leaves
its effects as they are, and a global light (angle and altitude) the effects that use it share. The values are the ones
an image editor's dialog shows -- angles in degrees from the right, counter-clockwise, sizes in units, percentages as
fractions -- so that a style read from an image editor's file maps onto it field for field.

An effect's light is read and written where it is kept with `DreamLayerStyleLight` (`Core/DreamLayerStyleTypes.h`):
`GetAngle` and `GetAltitude` answer the global light's for an effect that uses it and the effect's own otherwise, and
`SetAngle` and `SetAltitude` set them there -- setting the angle of an effect on the global light turns the global light,
and every effect that uses it, as an image editor does. `GetShadowOffset` turns a shadow's angle and distance into where
it lies from the layer (widget space, +Y up), the offset the renderer draws it at, and `GetAngleAndDistance` turns it back.
In the designer, the **Shadows** handles drag each shadow where it falls and the **Lights** handles drag the global light
(see [Viewport handles](../tools/designer.md#viewport-handles)).

From the bottom up, a styled widget is drawn: its drop shadows, its outer glow, the widget itself (at its fill opacity),
the pattern overlay, the gradient overlays, the colour overlays, the satin, the inner glow, the inner shadows, the strokes
and the bevel. The drop shadows and the outer glow lie behind the widget; everything from the overlays up lies on it,
clipped to it, and blends with it by its own mode. Where there are several of one kind, a later one is drawn over an
earlier one.

A **preset** (`UDreamLayerStylePreset`, a data asset) keeps a style to share: a layer style that names one draws the
preset's style, and follows every edit of it. `DetachFromPreset` copies it into the widget's own style. A preset can keep
the layer's blend mode and opacity too (`bIncludeBlending`, `Blending`): applied from the designer's preset library it
sets them on the widget once; a layer style following it draws only its effects. In the designer, styles are copied from
widget to widget and kept as presets from the widget menu and the Presets tab: see
[The widget designer](../tools/designer.md#layer-styles-colours-and-presets).

### How a style is drawn

DreamGUI picks the way; there is nothing to set (`UDreamLayerStyle::GetPath` says which).

- **In the element's own pixels** -- a rect block. The shader measures the rect block's rounded rect exactly, wherever it
  draws it, and draws every effect in the same pass, batched with what the rect block batches with; its quad grows by what
  the effects reach. The style's record rides in the rect block's row of rect block data, after the rect block's own, and
  is uploaded only when there is a style: a rect block without one costs what it did. The outer effects blend with each
  other by their modes and with what is behind as Normal -- a black shadow in Multiply looks the same either way. A
  style with a pattern is drawn apart instead, where the pattern can be bound.
- **Apart** -- every other widget: a text, an image, a sprite, a widget that draws nothing of its own -- no visual, an
  empty one that only takes clicks, an effect on its own children -- (its whole subtree, after the effect), a layer group
  drawn apart (its subtree). What it draws goes into a texture of its own, as a layer group's does; a jump
  flood of that texture's alpha gives every texel its distance to the edge; the style is drawn from the distances into
  the layers the widget is put down as -- each outer effect by its own mode over what is behind, then the widget with its
  effects by its layer group's mode, at its opacity, through its mask. This is exact against what is behind, and works for
  any shape; it costs the group's texture, the flood (a pass per power of two of the texture's size), a pass for the style
  and a composite per layer, every frame. Only where groups are drawn apart (see
  [Blend modes and layer groups](blend-modes-and-groups.md)); elsewhere the style is not drawn.

The shape a style drawn apart is measured round is where the widget's alpha is half or more: a widget that is all
see-through draws no shadow. Its gradients and patterns are laid across the widget's rect.

### A rect block's own effects

A rect block has had its own outer shadow, inner shadow, border and body gradient all along. They are a second way to
write four of a layer style's effects: `GetEffectsAsLayerStyle` gives them as a style (the outer shadow a drop shadow, the
inner shadow an inner shadow, the border an inside stroke with its gradient, the body gradient a gradient overlay of the
same stops and type), and `SetEffectsFromLayerStyle` writes a style's first drop shadow, inner shadow, stroke and gradient
overlay into them (a gradient's stops and type into the rect block's [multi-stop gradients](gradients.md)). What they
cannot say -- a blend mode, a contour, noise, a stroke outside the edge, a midpoint -- is dropped. The rect block keeps
drawing them itself; a layer style on its widget adds to them.

## Shape blends

`UDreamShapeBlendGroup`, a behaviour on a container, melts the rect blocks among its children into one shape: their
rounded rects joined by a smooth minimum, `Smoothness` units wide, so that two of them close together flow into each other.
Each joins by its rect block's `ShapeBlendOperation`: a union, a cut (subtract) or an intersection, in child order. The
colour across the shape is the members' body colours, each as much as it makes the shape there; their textures, their
own borders, gradients and shadows are not drawn.

The members are no longer drawn each on its own: the first of them draws the whole shape, in one quad over all of them,
with the built-in shader or its material, batched as any rect block is. A layer style on the group's widget is drawn round
the melted shape in the same pixels. Move the children and the shape follows. A click lands on the member nearest it,
wherever the melted shape covers the point -- the shader's formula, on the CPU. Up to 32 members.

`MakeShapeInFrame` gives the melted shape in another widget's space, for a shader to read (`DreamUIShapeBlend.ush`): glass
takes its shape from a group this way (`UDreamGlass`, `Shape` `ShapeBlend`; see [Glass](backdrop-effects.md#glass)).

## Light and foil

- **`UDreamPointerLight`** (on a container or a canvas's root) is a small light that follows the pointer, or stands where
  it is put and can be animated there: a colour, an intensity, a radius and a height above the widgets. It lights every
  bevel of a layer style under it from where it is, instead of by the style's own angle, and centres the border gradient
  of every rect block with a border under it on itself -- a reveal highlight that costs a rect block's row. Following the
  pointer it fades in while the pointer is over its widget's plane within reach and out when it leaves, and everything goes
  back to its own light. In a world that is not playing -- the designer's preview -- there is no pointer to follow, so it
  stands at its `LightPosition` there, which is where to put it to see what it lights. In the designer, the **Lights**
  handles drag where it stands and how far it reaches.
- **`UDreamTiltCard`** tilts a widget towards the pointer on a spring, the edge under the pointer dipping away, and grows it
  a little while the pointer is over it; through the render transform, so layout is untouched. Put it in a perspective scope
  (a parent with `Perspective` on) for the tilt to foreshorten.
- **`UDreamHoloFoil`** (on an image, a sprite, a texture or a rect block) draws the element with a material that adds a
  trading card's foil: rainbow bands that slide as the angle of view changes, a glare across the card and sparkles that
  twinkle as it turns, where its mask says. The angle of view is the pointer's offset from the centre, a tilt card's tilt
  on the same widget, or set by hand. The materials (`DreamUI_HoloFoil`, `DreamUI_HoloFoilRect`) are made by DreamShader
  from `DShader/`; a material is a draw call of its own.

Each of the three has a `bEnabled` switch: off, the light lights nothing, the card lies flat and the element draws with its
own material, every other setting kept. A layer style's switch is its style's own `bEnabled`, a shape blend group's its
`bBlendEnabled`.
