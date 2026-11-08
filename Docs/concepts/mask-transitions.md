---
title: Mask transitions
description: Revealing or hiding a widget and its subtree through a moving mask -- wipe, clock, iris, diamond, blinds, checker, dissolve, ink, shatter or a texture's order -- driven by a tween, a property track or Blueprint; cross-fades and whole-screen transitions; what it costs and what it leaves alone.
---

`UDreamMaskTransition` (`Mask Transition`, a behaviour in `DreamGUIExtensions`) shows a widget and everything under it
through a mask that moves as its `Progress` runs from 0 (hidden) to 1 (shown). It is the clip mask of
[Clip masks](clip-masks.md) driven for you: a procedural pattern or a texture, through a threshold that `Progress` moves.

## Presets

| Preset | Shows | Settings it reads |
| --- | --- | --- |
| Wipe | A straight edge sweeping from the left | `Angle` (degrees clockwise) |
| Clock | A hand sweeping clockwise from twelve o'clock | `Centre`, `Angle` |
| Iris | A circle opening from `Centre` (round on any widget) | `Centre` |
| Diamond | A diamond opening from `Centre` | `Centre` |
| Blinds | `Count` stripes, each opening from its left | `Count`, `Angle` |
| Checker | A board `Count` squares across: one colour of squares wipes in, then the other | `Count` |
| Dissolve | Specks all over, `Grain` pixels across | `Grain`, `Seed` |
| Ink | A blot spreading from `Centre`, its edge ragged | `Centre`, `Seed` |
| Shatter | `Count` shards appearing one by one, those near `Centre` first | `Centre`, `Count`, `Seed` |
| Texture | The order a texture's channel gives: dark first, light last, stretched over the rect | `Texture`, `Channel` |

And for every preset: `Softness` (the moving edge's width as a share of the sweep, 0 is hard), `bReverse` (the other
way: a wipe from the right, a clock anticlockwise, an iris closing in from the edge), `bInvert` (hides as Progress rises,
by the same shape: exactly what showing would have shown goes, so the two add up to the whole, pixel for pixel).

Ink, Shatter and Checker read a texture this component makes itself, in code -- noise and a jittered Voronoi of shards --
and makes again only when the widget's shape, `Centre`, `Seed` or `Count` changes. Nothing is copied from elsewhere.

## Driving it

- `Progress` is an animatable property: key it in a widget animation, tween it, or set it from Blueprint.
- `PlayForward`, `PlayBackward` and `PlayFromTo` tween it and return the tween, to chain on (`OnComplete`); `Stop`
  stops it where it is.
- `CrossFade(From, To, Preset, Duration)` gives `From` way to `To` through one shape: `To` is shown exactly where
  `From` is hidden. Each gets a transition if it has none; the one going takes the settings of the one coming, so that
  their shapes match.
- `PlayScreenTransition(Preset, Cover, Hold, Uncover, OnCovered, OnFinished, Colour)` covers the whole screen with a
  colour revealed by the preset, calls `OnCovered` (load the next level, swap the screen), holds, takes the cover away
  by the same shape and calls `OnFinished`. The cover sits above the pages (sort order 900) and blocks the pointer
  while it is up.

## What it touches

It does not touch the widget's `Clip Mask`: it **drives** the clip (`UDreamWidget::SetDrivenClipMask`) for as long as
it runs, and nothing of that is saved in the asset, shown in the details or written back to the `.dui`. While it drives
one, a widget that does not clip to its bounds does -- Inherit as Clip To Bounds, Disabled as Clip To Bounds Without
Intersecting -- so that there is a clip for the mask to cut; content outside the widget's rect is clipped meanwhile.

- **Fully shown, it lets go**: no mask, no clip of its own, no cost.
- **Fully hidden, nothing under it can be hit.**
- In between, the pointer follows the mask for Wipe, Clock, Iris, Diamond, Blinds and Dissolve (`bHitTestMask`); the
  presets that read a texture are hit across their whole rect until they are shown.

## What it costs

One masked clip level: what a clip mask costs (four more texels a level, see [Clip masks](clip-masks.md)), and for Ink,
Shatter, Checker and Texture one of the two mask texture slots a chain has -- content under different mask textures
draws in different draw calls. The textures are 256 by 256 (ink), 512 by 512 (shatter) and 64 by 64 (checker) half
floats.

## Limits

- A soft edge shared by a cross-fade's two halves lets a little of what is behind through, where each half-covers the same
  pixel: each is drawn over what is there, and two halves are not a whole. A hard edge (`Softness` 0) has none; so does a
  cross-fade inside an isolated [layer group](blend-modes-and-groups.md) on an opaque backdrop of its own.
- A transition on a widget clips its subtree to the widget's rect while it is not fully shown.
