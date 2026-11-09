---
title: Warps, jellies and text on a path
description: Bending what a visual draws -- the fifteen warp styles, a Bezier lattice dragged in the designer, a jelly on springs -- and laying a text's glyphs along a circle or a drawn path; how the mesh is cut to bend, what follows it, and what each costs.
---

Three mesh modifiers reshape what a visual draws after the visual has made it, and a visual draws paths for them to
follow:

| Component | Does |
| --- | --- |
| `Warp` (`UDreamMeshModifierWarp`) | Bends the mesh by a named style or a Bezier lattice |
| `Jelly` (`UDreamMeshModifierJelly`) | Bends it by a lattice on springs, which lag, squash and bounce |
| `Text On Path` (`UDreamMeshModifierTextOnPath`) | Lays a text's glyphs along a circle, an arc or a path |
| `Path` (`UDreamPath`, a visual) | A path through points, drawn as a line or not drawn at all |

They are modifiers like the others (see [Mesh modifiers and text animation](mesh-modifiers.md)): they go on a widget
whose visual builds a mesh -- an image, a texture, a sprite, a rect block, a text, a line. All four are in
`DreamGUIExtensions`.

## Cutting the mesh

A rect is two triangles, and two triangles do not bend. Before it moves anything, a warp or a jelly cuts the mesh into
pieces no longer than **Cell Length** (8 units by default):

- A quad that is an axis-aligned rectangle -- every rect writer's, every glyph's -- is cut along **one grid of lines for
  the whole mesh**. Neighbours sharing an edge (the nine parts of a nine-slice) cut it at the same points, so no crack
  opens between them when they bend.
- Any other triangle (a ring's, a polygon's, a line's) is cut into a regular triangular grid, the same number of pieces
  for each.
- Every new vertex takes **every attribute** as drawing the coarse mesh would have interpolated it: position, normal and
  tangent, colour, the four texture coordinates and UV4. So a rect block's rounded corners and outline, a glyph's
  distance field and an image's pixels bend with the mesh. A value all the corners share is copied as it is, not
  blended: a text quad's code and a rect block's data row are read back bit for bit.
- The vertices there were keep their indices and the new ones go after them, so a text's character ranges still name
  their glyphs' corners (a hyperlink is still hit where its glyphs are).
- **Max Vertices** (8192) caps the cut mesh: the cells grow until it fits, and a mesh that cannot fit is bent uncut.

## Warp

| Property | Default | |
| --- | --- | --- |
| `Mode` | Style | `Style` or `Lattice` |
| `Style` | Arc | One of the fifteen below |
| `Orientation` | Horizontal | `Vertical` runs the style along the height |
| `Bend` | 50 | -100 to 100 percent; negative bends the other way |
| `HorizontalDistortion`, `VerticalDistortion` | 0 | -100 to 100 percent: a perspective that makes the right end (the top) the larger |
| `Phase`, `PhaseSpeed` | 0, 0 | Turns of the Flag and Wave styles' waves, and turns a second while the game runs |
| `LatticeColumns`, `LatticeRows`, `LatticePoints` | 4, 4, even | The lattice: 2 to 5 control points each way |
| `Bounds` | Geometry | The box bent: what the visual draws, or the widget's rect |
| `CellLength`, `MaxVertices` | 8, 8192 | The cut |

The styles, by the names an image editor's text warp gives them and in its order, so that a document's warp maps by
name (`EDreamWarpStyle`, with `None` first): **Arc**, **Arc Lower**, **Arc Upper**, **Arch**, **Bulge**, **Shell Lower**,
**Shell Upper**, **Flag**, **Wave**, **Fish**, **Rise**, **Fisheye**, **Inflate**, **Squeeze**, **Twist**. Each is a
formula of this plugin's own (`FDreamMeshDeform::WarpPoint`, its comment says what each does) that looks like the
editors' style without matching it pixel for pixel. Bend, the two distortions and the phase can be animated (widget
animation property tracks, tweens, Blueprint).

A **lattice** is a tensor-product Bezier patch: `LatticePoints` row by row from the top, each in the box's own measure --
0,0 its top left corner, 1,1 its bottom right, as an image's pixels go. Evenly spread, it moves nothing. In the designer,
select the widget and drag the points of its control net in the viewport (magenta); a drag is one undo step, Esc puts
the points back.

**At rest it costs nothing**: no bend and no distortion, or an unmoved lattice, and the warp neither cuts nor moves the
mesh -- a small text even keeps drawing from coverage glyphs. Bent, a text switches to its distance field, as any
modifier that moves vertices makes it.

### On a text

On a text every glyph is cut by itself and bent with the rest: the glyphs follow the box the whole text sits in (with
Bounds = Geometry, the box around its ink). A warp runs after the modifiers that work on characters, so a text
animation's moves are bent too.

## Jelly

| Property | Default | |
| --- | --- | --- |
| `Columns`, `Rows` | 3, 3 | The lattice the springs hold |
| `Frequency` | 4 | Oscillations a second: higher is stiffer |
| `DampingRatio` | 0.3 | 1 settles without overshoot, less wobbles longer |
| `Inertia` | 1 | How much the widget's own acceleration pushes the points |
| `Anchor` | Centre | What holds still: Centre, Bottom, Top, Left, Right, None |
| `MaxStretch` | 0.3 | How far a point may stretch, as a share of the box's larger side |
| `PressSquash` | 0.08 | How far the points draw in towards a press while the widget is held |
| `DragStretch` | 0.4 | How much of a drag the nearest points follow |
| `Bounds` | Widget | The box the lattice spans |

The springs step in fixed steps of 1/240 s whatever the frame time (semi-implicit Euler, the rest carried over), so the
same frames give the same wobble on any machine; a frame longer than 32 steps drops the rest instead of stalling.
`Simulate`, `Poke`, `SetPressed`, `DragTo` and `Settle` drive it by hand.

Presses and drags reach it as they reach any component of a widget the pointer hits -- the widget has to be hit -- and
it passes them on: a button under it still clicks, a scroll view above it still scrolls. Still and untouched it is at
rest, and costs what a warp at rest costs: nothing.

## Text on a path

| Property | Default | |
| --- | --- | --- |
| `Source` | Circle | `Circle` or `Path` |
| `Radius`, `CircleCentre`, `StartAngle`, `SweepAngle`, `bClockwise` | 100, (0,-100), -90, 180, on | The circle and the arc of it, angles clockwise from twelve o'clock; a sweep of 360 is the whole circle, which the text wraps around |
| `PathWidget` | none | The widget whose visual is the `Path` to follow |
| `Align` | Centre | Start, Centre or End of the path |
| `StartOffset`, `OffsetSpeed` | 0, 0 | Moves the run along; units a second while the game runs -- a marquee |
| `LetterSpacing` | 0 | Space added between glyphs |
| `BaselineOffset` | 0 | How far off the path the anchor line sits |
| `Anchor` | Bottom | Which line of the glyphs the path runs through: Bottom (they stand on it), Middle, Top |
| `bFlip` | off | Runs the text the other way, on the path's other side |
| `bBendGlyphs` | off | Bends each glyph along the path instead of turning it whole |
| `bHideOutside` | off | On an open path, hides glyphs past either end instead of carrying them on straight |

Each glyph goes to the point as far along the path as it was along its line, turned to the path's direction there. A
glyph that a text animation lifts is lifted off the path; one it turns is turned from the path's direction: text on a
path runs after the modifiers that work on characters and before a warp. On anything but a text, the whole mesh is bent
along the path.

A path widget can be anywhere in the tree: the text follows it when it moves or changes shape.

## Path

`UDreamPath` is a line renderer (its `LineWidth`, brush and colour) through `Points` in local space, `Linear` or
`Smooth` (Catmull-Rom, `SegmentsPerSpan` pieces between two points), open or `bClosed`. With `bDrawLine` off it draws
nothing and is only a path. Its `.dui` tag is `Path`. `GetLength` and `GetPointAtDistance` measure it for anything else. Its points are dragged
in the designer like a lattice's (cyan).

## The order modifiers run in

Modifiers run in stages (`EDreamMeshModifierStage`, a modifier's `GetStage`), and within a stage in the order their
components sit on the widget:

1. **Characters** -- text animation, gradient colour, position as UV, a Blueprint modifier;
2. **Layout** -- text on a path;
3. **Deform** -- warp and jelly;
4. **Copies** -- shadow, outline, long shadow: they copy the bent mesh.

## What it costs

- A warp or jelly at rest: nothing. Bent: the cut mesh -- for a 200 by 60 block at cells of 8, about 200 vertices instead
  of 4 -- rebuilt whenever the visual rebuilds, and a text off its small-text coverage glyphs.
- An animated phase, a moving jelly or a scrolling text rebuild the visual every frame they move.
- The arithmetic is on the CPU; nothing is added to a draw call, so the warped element batches as before.

## Limits

- The styles look like an image editor's, by name; they are not the same to the pixel.
- A widget hit by its rect is hit by its rect: set the visual's Raycast Type to **Mesh** (or Visible Pixel) to hit the
  bent shape.
- Clipping to the layer below cannot follow an element whose modifiers reshape it (see
  [Clip masks](clip-masks.md)).
