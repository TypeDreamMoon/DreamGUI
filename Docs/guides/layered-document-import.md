---
title: Importing layered documents
description: Turning a .psd or .psb into a DreamUI widget -- a .dui and its textures, compiled into a widget Blueprint -- what each kind of layer becomes, the naming conventions, fonts, and importing again after the document changes.
---

A layered document -- a `.psd`, or a `.psb` for the large ones -- imports as a DreamUI widget: a `.dui` that lays its
layers out as widgets, the textures they draw, and a widget Blueprint whose *Source File* is that `.dui`. Masks,
clipping, blend modes, groups, live shapes, fills and text become the DreamGUI features that do the same thing, so the
screen stays editable: the `.dui` is an ordinary one, and the designer edits it like any other.

The importer is the editor module `DreamGUIPsd`. It reads the file itself -- no third-party library -- and never needs
the application that made it.

## Importing

- **Content Browser:** right-click a folder, **DreamGUI > Import Layered Document as DreamUI Widget...**, pick one or more
  files. A notification says what was imported and what was not; the Output Log has the whole report.
- **The Content Browser's Import**, or a drag onto the browser, takes `.psd` files as textures, as the engine always has.
  Turn on *Handle Content Browser Import* in **Project Settings > Plugins > DreamGUI Layered Document Import** to have it
  import them as DreamUI widgets instead.
- **A commandlet**, for scripts and continuous integration:

  ```
  UnrealEditor-Cmd.exe <project> -run=DreamPsdImport -Psd=<file> [-Folder=/Game/UI] [-Name=<name>] [-Dui=<file>]
      [-NoBlueprint] [-NoSave] [-DryRun] [-Dump] [-Report=<file>]
  ```

  `-Dump` prints the document as the reader sees it -- every layer, its kind, blocks and masks -- and imports nothing.
  `-DryRun` maps the document and reports what an import would write, writing nothing. The exit code is 0 when the
  import succeeded. `-run=DreamPsdImport -WriteSample=<file>` writes a small sample document that uses what the
  importer maps -- a group fading as one, a rounded panel with a stroke, a text, clipping, a nine-sliced `@Button`, a
  masked picture, a screen-blended glow, an adjustment layer and a drop shadow -- to try it on.

An import of `Hud.psd` into `/Game/UI` makes:

| What | Where |
|---|---|
| The widget Blueprint | `/Game/UI/WBP_Hud` |
| Its textures | `/Game/UI/Hud/` -- `T_Hud_<layer>_<id>` for pixels, `TM_…` for masks, `TG_…` for gradients, `TP_…` for patterns |
| The `.dui` | `<Project>/DUI/UI/WBP_Hud.dui`, the path the Blueprint converter uses for the same Blueprint |
| The manifest | `<Project>/DUI/UI/WBP_Hud.dui.psdimport.json` -- what the import wrote, for the next one |

## What each layer becomes

The root is a `Widget` the canvas's size, centred; the layers are its children in the document's stacking order, the
bottom layer first, which is the order a `.dui` draws its children in. Each widget is placed at the pixels its layer
covers, anchored to its parent's top left.

| Layer | Becomes |
|---|---|
| Pixels | `Image` of a texture of the layer's pixels, cut to the part that is not transparent |
| Smart object | The same, from the pixels it renders to |
| Group | `Widget` filling its parent. A group that blends by a mode other than pass-through, or fades, carries a `+ DreamLayerGroup` (isolated, its opacity as `GroupOpacity`, its mode as `GroupBlendMode`) -- see [Blend modes and layer groups](../concepts/blend-modes-and-groups.md) |
| Artboard | `Widget` at the artboard's rectangle, clipping its content to it |
| Text | `Text`: its string, its first style run's face (through the font map), size, colour, tracking, leading, underline, strikethrough and faux bold or italic, its first paragraph's alignment. Box text keeps its box; point text is placed at the pixels it draws |
| Live rectangle, rounded rectangle, circle | `RectBlock`: the shape's box, its radii, its fill as the body, its stroke as the border (grown for a stroke that is centred or outside) |
| Live ellipse that is not a circle | `RectBlock` with an elliptic clip mask |
| Any other shape | `Image` of the pixels it renders to, or -- when the document stored none -- of its path, filled |
| Solid or gradient fill | `RectBlock` over the canvas; a gradient is drawn into its body texture |
| Pattern fill | `Image` tiling the pattern |

Then, for every layer:

| Property | Becomes |
|---|---|
| Opacity | `RenderOpacity` (a group's: `GroupOpacity`) |
| Fill opacity | The body's alpha -- the image's tint, the text's colour, the block's body and border |
| Blend mode | `BlendMode`, all 27 (Linear Dodge is `Additive`) -- see [Blend modes and layer groups](../concepts/blend-modes-and-groups.md) |
| Clipping | `bClipToBelow` -- see [Clip masks and clipping to the layer below](../concepts/clip-masks.md) |
| Hidden | `Visibility = Hidden` (turn *Import Hidden Layers* off to leave hidden layers out) |
| Pixel mask | A clip mask reading a one-channel texture of the mask, its feather and density baked in; the clip is narrowed to the mask's rectangle by `ClippingMargin` when the mask is black outside it |
| Vector mask | When it is a live rectangle, rounded rectangle or ellipse: a clip shape (`ClippingCornerRadius`, or `ClipMask.Shape = Ellipse`) over its box, its feather as the mask's. Otherwise the path is filled into the mask texture |
| A group's mask | On a pass-through group, the container's clip mask (each child is masked, which is what passing through means); on a group that isolates, its `GroupMask` |

Turn on *Bake Layer Masks* to multiply a pixel layer's masks into its texture instead: fewer textures, nothing to edit
afterwards.

## Naming conventions

Words in a layer's name the importer reads (turn *Naming Conventions* off to read names as they are):

| In the name | Does |
|---|---|
| `!skip` | Leaves the layer -- and a group's whole content -- out |
| `@Button`, `@Toggle`, `@Slider` | Adds that behaviour (`+ UIButton`, `+ UIToggle`, `+ UISlider`) to the layer's widget |
| `#9(l,t,r,b)` | Draws a pixel layer nine-sliced, with these margins in pixels; `#9(n)` is one margin for all four, `#9(h,v)` horizontal and vertical |

The words are taken out of the name the widget's id is made from: `Play @Button #9(8,8,8,8)` is the widget `Play_psd12`.

## Fonts

A text's face is named by its PostScript name, which is not a DreamGUI font. **Font Map** in the importer's settings
maps a face (`Roboto-Bold`) or a family (`Roboto`) to a DreamGUI font asset; a face with no entry takes **Default Font**
(DreamGUI's default when empty), and the import lists every such face. *Rasterize Text* imports every text layer as an
image of its pixels instead.

Text is placed by its pixels and drawn by DreamGUI's text engine, so a face that is not the document's, or a line height
the two engines measure differently, moves it by a pixel or a few: check the texts after an import.

## Importing again

Right-click the Blueprint, **Reimport Layered Document**, or import the same file into the same folder again. The
import does not write a new `.dui`; it edits the one it wrote, through the same text patcher the designer writes back
with, so the file keeps its formatting, its comments and everything you added.

A node whose id ends in `_psd<n>` was made from layer *n* (its layer id, which survives renaming and restacking in the
document). On a reimport:

- A layer that is gone takes its node with it. Anything you nested inside that node is first moved up to its parent, and
  listed.
- A renamed layer renames its node with `(was: OldId)`, so graph references, bindings and animation tracks follow it.
- A moved or restacked layer moves its node; a new layer's node is inserted where the document puts it. Nodes you added
  keep their places.
- On a generated node, the lines the importer writes (placement, opacity, visibility, clipping, blend mode, the clip
  mask, the brush, the block's body, border and radii, the text's properties, the layer group's) are rewritten to the
  document's new values; the ones it wrote last time and does not write now are taken out. Every other line is yours and
  is kept. A line of the importer's that you changed is overwritten and listed; one you turned into a binding (`<-`) is
  kept, and listed.
- A generated node you deleted comes back, and is listed. Put `!skip` in the layer's name to leave it out for good.
- The root's placement is written by the first import only, so a screen can anchor its root as it needs.
- Textures the last import made and this one does not are listed, and left for you to delete.

## Not imported yet

Read from the document and listed in the import's report, but not turned into DreamGUI features yet:

- **Layer effects** (drop and inner shadows, glows, bevel and emboss, satin, colour, gradient and pattern overlays,
  stroke). The layer is imported without them.
- **Adjustment layers** (levels, curves, hue/saturation, brightness/contrast, colour balance, black and white, photo
  filter, channel mixer, gradient map, exposure, vibrance, invert, posterize, threshold). No node is made.
- **Text warps.** A warped text is imported as an image of its pixels.

## Limits

- **Colour.** RGB and grayscale import as they are; an indexed document through its colour table; CMYK and Lab are
  converted to sRGB by formula, without their colour profile, so colours come close rather than exact -- convert the
  document to RGB first for an exact import. 16- and 32-bit documents are reduced to 8 bits (32-bit is linear, and is
  encoded to sRGB).
- **Normal blending on render targets.** DreamGUI blends normal layers in encoded values on the screen, as the
  document's own merged image does, but in linear light into a render target. Half-transparent layers drawn into a render
  target come out a little different from the document.
- **Text runs.** A text with several style runs takes the first run's style throughout.
- **Shapes.** Only an untransformed rectangle, rounded rectangle or ellipse is a live block; a turned or skewed one, a
  line, a custom shape or a combined path is drawn from its pixels.
- **Old documents** with no layer ids name nodes by the layers' places, so a reimport after layers are added or removed
  may match the wrong nodes.

## Settings

**Project Settings > Plugins > DreamGUI Layered Document Import** (`UDreamPsdImportSettings`, in `DefaultEditor.ini`):

| Setting | Default | |
|---|---|---|
| Font Map | empty | Face or family name to DreamGUI font |
| Default Font | DreamGUI's default | The font of an unmapped face |
| Rasterize Text | off | Text layers as images |
| Import Hidden Layers | on | Hidden layers imported, hidden |
| Trim Transparent Pixels | on | Textures cut to their opaque part |
| Naming Conventions | on | `!skip`, `@Button`, `#9(...)` read |
| Bake Layer Masks | off | Masks multiplied into textures |
| Mask Feather Scale | 1 | A mask's blur sigma is its feather times this |
| Max Gradient Texture Size | 512 | The largest side of a drawn gradient |
| Handle Content Browser Import | off | The Content Browser's Import takes `.psd` as a widget |
| Blueprint Prefix | `WBP_` | The Blueprint's name: the prefix and the document's |
