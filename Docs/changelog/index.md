---
title: Changelog
description: What changed in each DreamGUI version — what is on main and not yet released, 1.0.0, the first public release, and back through the 2.1.0 and 2.0.0 development builds.
---

What changed in each version of DreamGUI, grouped by what it changes for a project. Moving a project from one version to
the next — what loads differently, which defaults moved, what C++ has to change — is in
[Upgrading](../guides/upgrading.md); a project coming from LGUI, LexUI or the early 1.x builds starts at
[Migration](../guides/migration.md). This page follows the repository's `CHANGELOG.md`:
https://github.com/TypeDreamMoon/DreamGUI/blob/main/CHANGELOG.md

**Note:**

**1.0.0 is the first public release.** The builds before it were the fork's development line: the single-module 1.x
builds, then 2.0.0 and 2.1.0 (commits `2416e3f8` and `3049561a` on `main`). Public numbering starts again at 1.0.0, so a 2.0 or a 2.1
below is a development build, and older than 1.0.0. The descriptor's integer `Version` keeps rising — 100 for 1.0.0,
after 3 for 2.1.0 — so the engine never takes 1.0.0 for the older of the two.

## Unreleased

What is on `main` after 1.0.0, and ships with the next version.

### New

- **Clip masks.** A widget that clips to its bounds takes a mask on its clip (`ClipMask`): an ellipse, polygon, star or
  ring fitted to the clip rect, a feathered edge, a texture's channel laid over it (stretched, fitted, filled or tiled) or
  a procedural pattern (linear, radial, angular, diamond, blinds, checker, noise), through a threshold and softness that
  make wipes and dissolves, inverted if asked — a spotlight. It clips the whole subtree on every path that draws it, and
  can be hit-tested by the mask. A chain of clips with no mask costs and uploads exactly what it did. See
  [Clip masks and clipping to the layer below](../concepts/clip-masks.md).
- **Clip to the layer below.** `bClipToBelow` shows a widget, and everything under it, only where the sibling drawn before
  it is drawn and through that sibling's opacity: a clipping mask. The layer below can be a simple or nine-sliced image,
  texture or sprite, or a rect block; anything else is not clipped to, with a warning.
- **Blend modes.** `EDreamUIBlendMode` grows from three modes to 27: dissolve, darken, colour burn, linear burn, darker
  colour, lighten, screen, colour dodge, lighter colour, overlay, soft light, hard light, vivid light, linear light, pin
  light, hard mix, difference, exclusion, subtract, divide, hue, saturation, colour and luminosity, by the W3C compositing
  formulas in encoded values. Screen, exclusion and dissolve are done by the blend hardware and batch as before; the rest
  blend over a copy of what is behind, made per draw, and elements of one such mode share a draw while they do not
  overlap. An element drawn through a material with one of the new modes is put down through a group of one. See
  [Blend modes and layer groups](../concepts/blend-modes-and-groups.md).
- **Layer groups.** `UDreamLayerGroup` on a container draws its subtree into a texture of its own and puts it down as one:
  `GroupOpacity` fades it without its children showing through each other, `GroupBlendMode` blends it as a whole,
  `GroupMask` masks the composite. Inside an isolated group the children's modes blend with the group alone. Groups nest;
  their textures come from the frame's pool. Clipping to a layer below that no clip level follows — a text, a tiled image,
  a container, a group — is done through such a group, and `UDreamRetainerBox` puts its retained texture down through
  the same composite (with a new `GroupBlendMode`). On every canvas DreamGUI's own renderer draws — world-space
  canvases in World Space - DreamUI Renderer and the designer's and the level editor's previews included, a group's
  texture occluded by the scene as its elements are; a canvas the engine's renderer draws passes them through, with a
  warning.

- **View models.** A `.dui` file declares the view models its class holds in a `viewmodels { … }` block — by class,
  with where each comes from: the host (a creation pin, `SetViewModel`, a host's `.dui` line), `= new`, `= global` /
  `= global "Name"` from the new `UDreamViewModelSubsystem`, or `= parent` from the nearest enclosing widget. Bindings,
  routes and loops reach into them through member paths: `Text <- Player.Name`, `Value <-> Settings.MasterVolume`,
  `OnClicked += Settings.Apply()`, `OnValueChanged += Settings.SetVolume(Value)`, `for Item in Inventory.Items`. Any
  UObject class can be one; `UDreamViewModel` is the convenient base (with `DREAM_VM_SET` for C++ setters), and anything
  implementing `INotifyFieldValueChanged` — UE's `UMVVMViewModelBase` included — is subscribed to, with no dependency
  on the ModelViewViewModel plugin. A path whose objects are not all set is not evaluated, and the property keeps its
  value. See [View models](../language/viewmodels.md).
- **Bindings update when what they read changes.** The compiler records every variable and member a `<-` expression
  reads; when all of them announce their changes (FieldNotify), the binding is subscribed and costs nothing between
  changes. `Text <- Title` and `Shown <- Count > 0` on FieldNotify variables used to be read every frame.
  `DreamUI.Binding.Dump` lists which bindings are read every frame and why; `DreamUI.Binding.ForcePoll 1` reads them
  all every frame again. See [When a binding updates](../language/bindings.md#bindings-and-routes).
- **Rows update alone.** In a `for` or an `each`, a copy whose item announces a change of a member it shows is updated
  without refreshing the list, and `Event -> Item.Func()` routes each copy's event to its own item — moved along when a
  list view recycles a cell. With a typed source (`TArray<UItemVM*>`) the compiler checks `Item.Member` (DUI6021) and
  `Item.Func` (DUI6022).
- **A C++ `UDreamUserWidget` subclass can declare FieldNotify members.** `DreamUserWidget.h` now includes
  `FieldNotificationDeclaration.h`, which what UHT generates for them needs, as UMG's `Widget.h` does; such a class used
  to fail to compile. A `.dui` binding to the member subscribes through its descriptor, and `DREAM_VM_SET` is its setter.
- **`+=` and `=` on events**, beside `->`: `+=` adds a listener and takes only a multicast event, `=` is the one
  listener of a single-cast delegate — which routes can now name at all — and takes only that; the wrong one is DUI5025.
  `->` takes either.
- **Create Dream Widget, Add to Viewport and Add to Player Screen**, UMG's Create Widget node and the two user widget
  calls that go with it. Picking a class on the node gives it a pin for each Expose on Spawn property — every `props`
  entry of a .dui file is one — and an Owning Player pin, and types its result as that class. The pins are assigned
  before the widget initializes, so Pre Construct, On Initialized, On Construct and the first value of every binding
  already see them; UMG assigns after Create, which is too late for On Initialized. Add to Viewport puts the widget on
  its owning player's screen and, as in UMG, refuses one that already has a parent; Add to Player Screen also refuses a
  widget with no owning player. Remove from Parent was already there. C++ gets the two halves of the node as
  `BeginCreateDreamWidget` and `FinishCreateDreamWidget`.
- **Add Component lists only the components the selected widget supports** (`FDreamUIComponentSupport`). A mesh
  modifier needs a visual that builds a mesh, a sprite player a Sprite, a sprite-sheet player a Texture, a hyperlink or
  the text animation a Text, a UMG interaction a UMG Widget visual; a retainer or invalidation box needs the widget's
  canvas first; a canvas, navigation, animation component, named slot or content widget is one per widget, a widget is
  one kind of selectable and one scroll view, and a content widget is not offered on a widget with several children.
  Each of these used to be addable anywhere and then sat there doing nothing. Paste, duplicate and component assets
  dropped on the panel follow the same rules; a refused drop says why. A project adds rules for its own components
  from its editor module. See [Components](../tools/designer.md#components).
- **Adjustment layers.** `UDreamAdjustmentLayer` works on what is behind it, as far as its rect, clips and masks reach: up
  to eight operations in order — brightness and contrast, levels, curves, exposure, hue and saturation (or colorize),
  vibrance, colour balance, black and white, photo filter, channel mixer, invert, posterize, threshold, gradient map and a
  lookup table — mixed with what they were made from by any of the 27 blend modes and an opacity. With `Target` set to
  `Children` it works on its own widget's subtree instead, drawn as a group. See
  [Effects on what is behind](../concepts/backdrop-effects.md).
- **Progressive blur.** `UDreamBackgroundBlur` takes a `StrengthMap` — a texture's channel, a linear or radial ramp, or
  a clip mask's pattern — and blurs each pixel as strongly as the map says there, from a blur pyramid over what is
  behind. With no map it is the uniform blur it was.
- **Glass.** `UDreamGlass` is a rounded rect of glass whose edge bends what is behind it, red and blue a little apart,
  optionally frosted, tinted and lit along the edge; its corners are a rect block's, and a click outside them misses it.
- **Stylize effects.** `UDreamStylizeEffect` lays up to eight layers over what is behind it or over its own children: a
  CRT screen, a glitch (`TriggerBurst` flares it), chromatic aberration, a halftone screen of one ink or four, ordered
  dithering, film grain, a vignette, posterization.
- **Layer styles.** `UDreamLayerStyle` puts an image editor's layer style on any widget (`FDreamLayerEffects`): drop
  shadows, an outer glow, inner shadows, an inner glow, a bevel and emboss (inner, outer, emboss, pillow, stroke; smooth or
  chiselled), a satin, colour, gradient and pattern overlays and strokes (outside, inside, centred; colour, gradient or
  pattern), each with its own blend mode, opacity and the dialog's own settings — contours, noise, spread and choke,
  range and jitter, global light. `UDreamLayerStylePreset` keeps one to share. A rect block draws its style in its own
  pixels, batched as before; anything else — a text, an image, a container's subtree, a layer group — is drawn apart,
  measured by a jump flood of its alpha, and put down layer by layer, each outer effect by its own mode. A rect block's own
  shadows, border and gradient read and write as a layer style (`GetEffectsAsLayerStyle`, `SetEffectsFromLayerStyle`). See
  [Layer styles, shape blends and light](../concepts/layer-styles.md).
- **Shape blends.** `UDreamShapeBlendGroup` melts the rect blocks among a container's children into one shape by a smooth
  minimum — a union, a cut or an intersection each, by `ShapeBlendOperation` — drawn by the first of them in one quad, in
  the members' blended colours, with the group's layer style round it; a click lands on the nearest member, by the same
  formula.
- **Pointer light, tilt card and holo foil.** `UDreamPointerLight` follows the pointer and lights the bevels of the layer
  styles under it and the borders of the rect blocks under it (a reveal highlight); `UDreamTiltCard` tilts a widget towards
  the pointer on a spring; `UDreamHoloFoil` draws an image or a rect block with a trading card's foil, its angle of view
  the pointer's or a tilt card's.
- **Glass in a melted shape.** `UDreamGlass` with `Shape` set to `ShapeBlend` takes the shape a shape blend group melts its
  rect blocks into — the group on the widget `ShapeBlendGroup` names, or on its own — and is drawn over the whole of it,
  wherever the members reach, its edge bending what is behind along the melted outline; a click lands on it wherever the
  shape is. See [Effects on what is behind](../concepts/backdrop-effects.md#glass).
- **Mesh warps.** `UDreamMeshModifierWarp` bends what any batch-mesh visual draws — an image, a rect block, a text
  glyph by glyph — by one of fifteen styles named as an image editor's text warp names them (arc, arc lower and upper,
  arch, bulge, shell lower and upper, flag, wave, fish, rise, fisheye, inflate, squeeze, twist), with a bend and a
  horizontal and vertical distortion, or by a Bezier lattice of 2 to 5 points each way, dragged in the designer's
  viewport. The mesh is cut finer first along one grid shared by the whole mesh, every attribute interpolated, so rounded
  corners, glyph distance fields and pixels bend with it; at rest it costs nothing. See
  [Warps, jellies and text on a path](../concepts/warp-and-text-on-path.md).
- **Jellies.** `UDreamMeshModifierJelly` holds the mesh on a lattice of damped springs that lag behind the widget's
  moves, squash while it is pressed, bounce when it is let go and stretch after a drag, stepped in fixed steps so that
  the same frames give the same wobble; presses and drags pass on to the widget's button or scroll view.
- **Text on a path.** `UDreamMeshModifierTextOnPath` lays a text's glyphs along a circle, an arc or the path a
  `UDreamPath` widget draws (a new line visual through editable points, straight or smooth, open or closed, drawn or
  not): aligned to its start, middle or end, spaced, lifted off it, flipped to its other side, scrolled along it, each
  glyph turned whole or bent; a text animation's moves are kept.
- **Modifier stages.** Mesh modifiers run in stages (`GetStage`): those that work on characters, text on a path, the
  warps that cut the mesh, then the copies (shadow, outline, long shadow) — which now copy a bent mesh whichever
  component was added first.
- **Mask transitions.** `UDreamMaskTransition` reveals or hides a widget's subtree through a moving clip mask — wipe,
  clock, iris, diamond, blinds, checker, dissolve, ink, shatter or a texture's order — with softness, reverse, invert
  and a centre; its `Progress` takes tweens and property tracks. `CrossFade` swaps two widgets through one shape, each
  pixel shown by one of them, and `PlayScreenTransition` covers and uncovers the screen. Ink, shatter and checker read
  textures made in code. It drives the widget's clip (`UDreamWidget::SetDrivenClipMask`, new) without touching its
  `ClipMask`, and fully shown it costs nothing. See [Mask transitions](../concepts/mask-transitions.md).
- **Reversed clip masks.** `FDreamClipMask::bReverse` runs a texture's or pattern's value the other way before the
  threshold: a gradient from right to left, a radial one from the edge in.
- **Niagara particles.** A new module, `DreamGUINiagara`, draws Niagara systems on DreamGUI canvases: the `NiagaraSystem`
  visual simulates a system with a component that renders nothing and is out of the scalability's reach, reads each CPU
  emitter's sprite and ribbon renderers after the tick, and writes them as segments of its own — one draw call each --
  clipped, masked, blended and grouped like any element. Sprites keep their sheet frames and the blend between them, pivot,
  rotation, alignment and sort; ribbons their ids, link order, both UV sets and a curve through their points. One unit is a
  pixel on the widget's plane (XZ or XY); time can be paused, dilated, run while the game is paused, stepped at a fixed rate
  and advanced. The built-in shader draws them with the particle material's texture and blend, or a converted UI material
  does: **Convert Particle Materials** copies a particle material as a UI one, its particle inputs read from the vertex and
  its opacity through DreamGUI's clip. A system that declares `User.DreamUI_WidgetSize`, `User.DreamUI_PointerPosition`,
  `User.DreamUI_PointerInside` or `User.DreamUI_TargetPosition` is given it. `BurstAt` spawns the system again at a point,
  `BurstTo` sends a flight — the plugin's fly-to template by default — to another widget, and the `Niagara Trigger`
  component plays a system on clicks, the pointer coming and going, and a progress filling. GPU emitters and mesh renderers
  are reported and not drawn yet. The plugin now depends on Niagara. See [Niagara particles](../concepts/niagara.md).
- **Direct-mesh segments.** A `UDreamVisualDirectMesh` can draw in several segments, a draw call each, can leave its
  shading to the canvas — the built-in shader or a material, with the clip, the clip masks' textures and the blend mode, as
  a batched element is — and can write its vertices every frame the canvas sends its sections.

### Changed

- **A binding that reads only FieldNotify variables no longer reads them every frame** once its Blueprint is
  recompiled: it updates when they announce a change. C++ that assigns such a variable without broadcasting it — which
  FieldNotify never allowed — is no longer covered up by the poll; `DreamUI.Binding.ForcePoll 1` finds it.
- **Create Dream Widget Of Class is no longer in the Blueprint menu**: the Create Dream Widget node stands for it, as
  UMG's Create Widget node stands for `UWidgetBlueprintLibrary::Create`. Graphs that already call it keep compiling.
  Being BlueprintInternalUseOnly, it is also gone from Python.
- **The Content Browser's Add menu offers DreamGUI Widget at its top**, beside the engine's Blueprint Class and
  Material, rather than only one submenu down. The submenu itself, now labelled DreamGUI, is in sections — Basic
  (DreamGUI Widget, Widget Animation), Fonts, Graphics, Rich Text — with short labels and a one-line tooltip each,
  instead of one alphabetical list of type names with the widget last and the classes' code comments as tooltips. The
  bitmap font, which is no longer developed, reads Legacy Bitmap Font and points to the distance-field font. Asset type
  names, on tiles and in filters, are unchanged.
- **The designer's Palette is grouped the way UMG's is.** Basic, Panels, Common, Input, Lists, Scrolling, Containers,
  Primitive, Shapes, Effects, Components, Modifiers, Advanced, User Created, and last Legacy, which starts closed —
  where there was one Controls category of thirty-odd rows of every kind beside a dozen small ones that differed by a
  word. Rows are listed by name, the panels lose their "UMG " prefix, and the Tile View and Scroll Box behaviours moved
  to Legacy beside the controls that replaced them. User Created no longer lists the plugin's own preset folder. The
  hierarchy's Create menu uses the same groups. Registry keys are unchanged, so favourites keep their stars; a project
  extension that compared `FDreamUIControlDescriptor::Category` with "Controls", "Post Process", "Extensions" or a legacy
  category name reads the names from `DreamUIPaletteCategory` instead. See [The palette](../tools/designer.md#the-palette).
- **The details panel puts what a widget is right under where it sits.** Slot, Layout, then Visual, Panel and Self
  Layout, then Appearance and Behavior, then Render Transform and — collapsed — Perspective, Navigation,
  Accessibility and Localization; the visual used to come after Behavior, Appearance, Panel and Self Layout. A Text's
  rows lead with Text, Font, Size, Style, Color and alignment, with its wrapping, typography and rendering settings in
  groups that start closed, and a Sprite's or Texture's colour sits under its sprite or texture. The Transform and
  anchor-data categories at the bottom, which repeated the Layout rows raw, are gone, and Is Enabled joined Behavior.
  See [The details panel](../tools/designer.md#the-details-panel).

### Fixed

- **A built-in element in Multiply is exact when it is partly transparent.** Its transparent pixels darkened what was behind
  them to black; it now darkens by its colour as far as its alpha says, as the composite does.
- **A clip whose pivot is off its centre stays on its widget under a scaled or turned parent.** Its centre was moved by
  the pivot's offset in world units rather than through the clipper's own scale and rotation, so under a canvas scale or
  a rotated parent the drawn clip rect slid off the rect the hit test used. Both now place it through the clipper's
  transform.

- **Creating a widget of an abstract class logs an error** instead of stopping on NewObject's assert. A class picked
  from a list cannot be abstract, but a class pin or a Create Dream Widget Of Class call fed from a variable can be.
- **A DreamUI Widget Blueprint's thumbnail is the screen it authors.** The Content Browser tile was a wireframe of the
  authored anchor rects, which knows nothing of what a layout arranges, so most screens came out as a grey box with a
  line or two on it. The thumbnail now builds the widget the way the designer's preview does, lays it out on the design
  canvas and draws it, as UMG draws its widget thumbnails; the wireframe is kept for a Blueprint with no compiled class.
  It is drawn when an asset has no clean thumbnail and when it is saved, not on every frame the pointer rests on it, so
  a large screen does not stall the browser. A thumbnail already saved in an asset stays until the asset is next
  changed and saved. See [The Content Browser](../tools/designer.md#the-content-browser).
- **A post-process visual under a clip mask's texture is clipped by the texture.** A background blur, a pixelate or a pixel
  sort inside a clip whose mask reads a texture took every texel of it as white.
- **Additive materials draw under Substrate.** With Substrate on, the engine makes a legacy additive material an unlit
  slab of its emissive times its opacity and full transmittance, which DreamGUI's renderer took for no coverage: the
  material drew nothing. It now draws what it draws without Substrate, the emissive by its opacity.
- **Niagara sprites draw in a play session.** The built-in shader read a particle's frame blend off its packed colour
  scale with a bare floor. The value reaches the pixel interpolated, and in a play session's screen-space view a whole
  step could arrive a hair under itself: the sprite then blended wholly into its next frame, which for a sprite of one
  frame lies past the texture, where the soft default disc is transparent. Sprites drew in the designer and in render
  targets and not at all in a game's viewport; ribbons were not affected. The step is now read half the gap the blend
  leaves below the next one, in the built-in shader and in converted particle materials alike.
- **The Niagara effects sample is laid out, and its sparks spread.** Its row and cards measured by their captions, so
  the five cards came out a word wide; they now ask for 1080 by 300 and 200 by 300. Three of its systems read, in one
  Set Parameters module, attributes that module wrote, which Niagara reads as they were before it: the click sparks and
  the full bar's shards were thrown at no speed, and the border sparks all began at one corner. Each now writes what it
  reads in an earlier group.
- **A NiagaraSystem visual's system is no longer drawn in the world for a frame.** The component that simulates it had its
  rendering turned off, but Niagara hands that flag to a component's scene proxy only as the component ticks, and the
  proxy made when a system was activated or burst drew the frame before: the whole system, lit, in the world behind the
  UI -- a flash at every burst in a play session, and a system drawn all along in a world that does not tick its
  components. The component now gives the scene no proxy at all.

## 1.0.0

Everything of the 2.1.0 development build, and the `.dui` language grown into what screens are written in: components
written in `.dui` alone, `use … as`, `if`, `for`, `rows`, shorter layout; the reference for all of it; and the fixes the
first two showcase screens found. Projects on 2.1.0 open unchanged (see [Upgrading](../guides/upgrading.md), “From 2.1
to 1.0.0”), except for assets that still loaded through a redirect: the plugin ships none any more.

### Removed

- **The CoreRedirects.** `Config/DefaultDreamGUI.ini` and its 763 redirects — from LGUI and LexUI, from the prefab
  vocabulary, from the control and event renames and from the module split — are gone, and the engine no longer
  applies any of them to a project that mounts the plugin. The plugin's own assets and the test fixtures name the
  current types. An asset saved against an old name is resaved once with 2.1.0's block borrowed into the project's
  `Config/DefaultEngine.ini` ([Upgrading](../guides/upgrading.md#from-21-to-100)). The four `DreamGUI.Packaging`
  tests that held the redirect file went with it, and `DreamGUI.Packaging.ThePluginShipsNoCoreRedirects` keeps a
  `[CoreRedirects]` section from coming back unnoticed.
- **The old-asset fixtures saved before the module split.** They loaded only through the redirects; the test host's
  `DreamGUIFixtures` are saved by 1.0.0 now, the baseline later versions keep loading.
- **`Tools/ModuleSplit/generate_split_redirects.py`**, which appended a split's redirects to that file. It is
  `moved_types.py` now: it lists the types a split moved and, with `--redirects`, prints a block for a project's
  own config, and writes nothing into the plugin's. `retarget_script_paths.py` takes the names from it.

### New

- **`rows`: a table of instances in `.dui`.** `rows Row : ListRow (Label, Description) { "City Ruins", "…" … }`
  writes the type, the style and the property names once, then one line per instance — the same component N times,
  differing in a few values, which is what a settings page or a list of menu entries is made of. The table is read into
  the ordinary unnamed children its lines stand for, so nothing downstream changed. Each row is named from its first
  value (`Page_0__Row_City_Ruins`), so inserting or reordering rows moves no other row's id or localization keys. A line
  may end in a block for what that row needs beyond the columns. The designer writes a column's value back into its cell
  and refuses, with the reason, what a row's line cannot spell ([DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004)). New
  codes [DUI2020](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2020) MalformedRows and
  [DUI3023](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3023) DuplicateRowKey (a warning). See [`rows`](../language/rows.md).
- **Components by a short name in `.dui`.** `@Row Row1 { }` is a node whose type is named by the `Asset` entry `Row` of
  a `resources` block — this file's, or one a `use` brought in — so a family of components is named once, in the
  library that styles it, and each screen writes `@Row` instead of the asset path on every line. An entry that is
  missing, is not an `Asset`, or names no user widget is reported as such.
- **`use … as` in `.dui`.** `use "Components/Row.dui" as Row` names the class a component file compiles into — its
  `class` line, or the Blueprint whose Source File it is — and `use /Game/UI/WBP_Row as Row` a class with no `.dui`;
  `Row Row1 { }` is then an instance. `use "Lib.dui" as nier` on a library enters its styles, resources and component
  names under `nier.` (`: nier.Label`, `@nier.Ink`, `nier.Row`), so two libraries can be used side by side, and the
  `use … as` lines of a library travel with it. `@Row` keeps working. See [`use`](../language/use.md).
- **Components written in `.dui` alone.** A file declares what its class offers its hosts: `props { Text Label }`
  (Blueprint variables, with defaults, which a host sets or binds — `Label <- GetName()` — even though they have no
  setter; a C++ parent's property of the same name is used), `events { Picked(Number Index) }` (event dispatchers,
  raised with `OnClick -> emit Picked(Index)` and routed by a host like any event), and slots that carry a layout and a
  style and can be marked `default` — the slot nested content goes to, in place of a `GetDefaultSlotName` override. A
  host fills a named slot with `slot Detail { … }` inside the instance. See [Components](../language/components.md).
- **Shorter layout in `.dui`.** A layout container is a node type (`VerticalBox Column { Spacing = 29 }`), a node
  nobody refers to needs no id (`HorizontalBox { … }`), slot lines group into `@slot { … }`, `@fill` / `@fill 2` stand
  for the two written most, and a style can carry `+ Component` blocks and slot lines, so a kind of column is one name.
- **`if` and `for` in `.dui`.** `if Cond { } else if … { } else { }` shows one branch's widgets at a time (each gets a
  `Shown` bound to its branch; a hidden branch keeps its state), `Shown <- Expr` is visibility as a yes or no on every
  widget (`UDreamWidget::Shown`), and `for Item in Items { … }` makes one copy of its template per item inside any
  panel, where `each` keeps filling a virtualized list view. See [Control flow](../language/control-flow.md).
- **The language reference.** `Docs/DuiLanguage.md` — on this site, the [`.dui` section](../language/overview.md) —
  describes the whole of `.dui`: file structure, types, values, styles, `use`, components, bindings, the control
  statements, timelines, with a table of every diagnostic ([Diagnostics](../diagnostics/index.md)). The editor's symbol
  dump (`DUI/.dui-symbols.json`) lists the keywords, the container types and their properties for an editor's
  completion.
- **The designer writes the new syntax back.** An edit lands on the line that holds the value whatever the node's type
  is written as, inside a `@slot { … }` block when there is one, and on a node with no id by the id made for it; a node
  made in the designer is written by the file's alias for its class, or as its container; content dropped into a
  component's named slot is written into that slot's fill. What no line spells is said rather than written: a
  visibility an `if` decides and a shorthand that cannot take the change are
  [DUI7004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7004), a value from a style's component
  [DUI7005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui7xxx#dui7005). See [Write-back](../language/write-back.md).
- **Render-transform tweens.** `UDreamWidget::RenderTranslationTo`, `RenderOffsetTo` (on the canvas plane, depth
  kept), `RenderScaleTo` and `RenderAngleTo` move, scale and turn what is drawn, never the layout: the tweens for a
  widget a panel places, whose anchored position and size the panel writes back on its next pass.
- **A control can refuse the focus ring.** `UUISelectable::bUseFocusRing` (on by default; `SetUseFocusRing` at run
  time) turns the project's ring off for a control whose own look marks its focus. The focus moving onto such a control
  takes the ring away instead of leaving it on the control it came from, and a screen whose controls all refuse it
  never makes one. An unset `NavigationSelectionClass` now means no ring anywhere, without a warning at every focus
  change.
- **A cap on how far one frame moves the tweens.** `DreamTween.MaxStepSeconds` (console variable; 0, the default, is
  off): a frame longer than it — a screen loading its assets, the first draw of new text — moves the tweens the world
  ticks by the cap only, so an entrance started just before such a hitch plays on from where it was instead of
  appearing at its end. Manual ticks are never capped.

### Fixed

- **The focus ring fits the control it marks.** The plugin's ring drew its frame on a child authored at a fixed 100x100
  in the centre of the ring's root, so the frame stayed 100x100 over a full-width row and a small icon alike, while
  only the invisible root took the control's size. A ring class with no Blueprint handler of its own now has each child
  that covers its root (centre-anchored, at least the root's size) anchored to stretch with the root, its authored
  margin kept; smaller or corner-anchored children keep their own size.
- **A ring that has just appeared no longer stays transparent.** Moving the ring killed its running tweens, the fade-in
  of a ring shown a moment before among them, so a second key press inside a quarter second — or the same focus
  arriving as a select and then a navigation enter — left the ring at the opacity the fade had reached. Moving now
  finishes the fade.
- **Focus moved by code leaves the control it came from.** `SetFocus` moves the navigation cursor with the focus, but
  the control it left heard no exit until the next navigation step, so it went on drawing itself Focused — and
  answering Focused to `GetCurrentSelectionState` — beside the control that had the focus, through a screen opening a
  sub-list or a dialog, and through being hidden and shown again. A deselect now ends the navigation's hold on it.
- **What the built-in shader draws after an image drawn through a material is drawn by the built-in shader again.** On
  a screen or render-target canvas, a draw through a material bound the material's pipeline but left the pass's cache
  of the built-in pipeline saying it was still bound, so the next text or block drew through the material's shaders:
  text after a material-drawn image went blank or turned to boxes, and the RHI ensured on parameters set for a vertex
  shader that was not bound. World-space canvases already set the built-in pipeline again; screen ones now do too.
- **A brush material whose only parameter of the canvas's is the renderer flag is told the renderer.** The canvas asked
  a material for its texture parameters only, so `DreamUI_IsRenderByDreamUIRenderer`, a scalar, never counted: a
  procedural material that premultiplies by it was drawn with the flag at its default, and premultiplied a second time
  by DreamGUI's renderer.
- **A material instance given to an image shows the parameters its owner sets on it.** The canvas draws a material
  through a proxy of its own that caches the material's uniform expressions, and rebuilt that cache only when
  DreamGUI's own parameters changed: a `SetScalarParameterValue` on the instance after its first draw never reached the
  screen, so an animated material stood still. The proxy now rebuilds its cache, as the draw is gathered, whenever the
  instance's own render proxy has.

## 2.1.0 (development build)

Five passes of engineering after 2.0: the frame made cheap enough for thousands of animated widgets, every control and
editor path audited, text measured against Chrome and Slate and rebuilt where it differed, gradient text, keyboard and
gamepad navigation, and what it takes to ship. `DreamGUI.uplugin` is version 3. Assets saved by 2.1 cannot be opened by
2.0.

### New

- **Gradient text.** A text's face, its outline and an overlay over its face can each be painted with a gradient:
  linear, radial, conic, diamond or four-corner, with up to 16 stops, pad, repeat or reflect, mixed in sRGB, linear light
  or Oklab, measured across the text block, the content box, each line, each glyph or each rich-text run. Gradients are
  written as CSS writes them (`linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`) — in the details panel, in a
  `.dui`, or as a rich-text run (`<gradient=Name>...</gradient>`, the name being a custom style, a project preset or the
  CSS itself). Presets are `UDreamGradientAsset` assets or CSS strings in the project settings (`GradientPresets`); texts
  that paint with the same gradient share its row on the GPU. A paint's phase, angle, centre and scale animate without
  laying the text out or painting it again — keyable in Sequencer (`FacePaintPhase`, `OutlinePaintPhase`,
  `OverlayPaintPhase`, `PaintAngleOffset`) and tweened by `UDreamTextPaintLibrary` (`PaintPhaseTo`, `PaintAngleTo`,
  `PlayShimmer`). A material that does not shade through `MF_DreamUI_Shade` paints by vertex colour instead.
- **Gradients in the editor.** A gradient property shows a strip of the gradient whose stops can be dragged, a CSS box
  with Copy and Paste, and a presets menu that lists the project's presets and gradient assets and saves the gradient as
  either. A gradient asset has a thumbnail and *Copy as CSS*. A text's paint fields are one Paint group in its details.
- **Tab navigation.** Tab and Shift+Tab, with no Ctrl, Alt or Cmd held, move the focus in hierarchy order, siblings
  sorted by the new `TabIndex`; `bIsTabStop` and `TabNavigation` (Continue, Cycle, Contained, Once, None) shape the
  walk; lists, tiles and trees are one stop, entered at their selected row; a stop scrolled out of view is scrolled into
  it. A modal, a dimmed dialog and a popup that cycles keep Tab inside them, and another player's popups are never
  stops. Tab commits a text field's edit and moves on, and arriving by Tab starts editing; a dropdown commits its
  highlighted row on Tab and a menu closes its chain. Slate's own navigation from the bare game viewport — Tab, the
  arrows — never moves the focus into UMG while the player has a DreamGUI focus or Tab stop.
  `TabOrder = LegacyGeometric` keeps 2.0's geometric order for this release.
- **Gamepad and input modes.** A navigation scope says whether its screen takes menu or game input (`InputMode`: All,
  Menu, Game; Game turns built-in navigation, confirm and back off for that player). Keys and the pad act on the focus:
  Enter, Space and the pad's accept press the focused widget, never the hovered one, and nothing when nothing is
  focused. The focus look shows only when keys or a pad moved the focus, as CSS `:focus-visible` does
  (`bFocusVisibleOnlyFromKeys`), and in DreamGUI's UI-only input mode the cursor hides while a pad is in use
  (`bHideCursorOnGamepad`). The confirm, back, direction, page, extent and tab-switch keys are project settings
  (`ConfirmKeys`, `BackKeys`, `DirectionKeys`, `PageKeys`, `ExtentKeys`, `PreviousTabKeys`, `NextTabKeys`), read again
  when they change; the platform's accept and back buttons are read at run time (`bUsePlatformAcceptBack`); the
  triggers page, and the shoulder buttons switch the active tab view's tabs, with prompts in the action bar. Split
  screen: scopes, action triggers and the action bar belong to their widget's owning player unless given an index.
- **Text**, measured against Chrome and Slate and brought to them: grapheme clusters and bidi levels with ligatures and
  CSS line boxes; fallback faces chosen by Unicode range, culture and presentation, with real bold and italic faces;
  colour emoji (CBDT/CBLC, sbix, COLRv0), whose shadow, outline and long-shadow copies are silhouettes; justification,
  tab stops and a middle ellipsis; long texts laid out again only where an edit touched them; line breaking by the
  game's culture; a rich-text `<lang=xx>` tag; images in rich text sized and aligned.
- **Small text** — up to `SmallTextMaxPixelSize`, 20 px by default — is drawn from hinted coverage glyphs placed on the
  device pixel grid, as crisp as Slate's. Text with an outline, a glow or an underlay draws its face that way and its
  effects from the field (`SmallTextEffectFace`), and text in a render layer does once the layer has held still for 3
  frames. A font keeps the atlas cells of its coverage glyphs while any text draws from them; past twice
  `MaxCoverageCells`, new ones wait, drawn from the field meanwhile, with one warning per font.
- **The default font** is Slate's: Roboto in real regular, bold, italic and bold-italic faces, with DroidSansFallback
  for CJK, on the outline (multi-channel) distance field.
- **Panels** catch up with UMG: scroll box fill slots and front and back padding, wrap box fill, scale box modes and
  stated scales, grid spans, drag switches, the border's tint.
- **Popups**: a popup layer per player replaces the click blockers, gives the focus back to whoever opened a popup, and
  Back closes only the top one. A popup says what Tab does in it (`TabBehavior`: Cycle, or CloseAndContinue for
  dropdown lists and menus). A modal shown over an open popup closes it and takes the first click and the first Back; a
  press on a layer drawn in front of an open popup closes the popup and still reaches the layer, and Back goes to that
  layer first. A popup closed by its own focus-on-open ends closed, and a lifted panel menu keeps its slot's padding,
  nudge and size bounds.
- **`DreamGUI.Memory`**, a console command: each font's glyph atlas (slices, GPU bytes and the CPU copy, cells, glyphs,
  face bytes), the sprite atlas pages, and each world's canvas mesh sections and paint rows; `DreamGUI.Memory Json`
  prints it as JSON, and `File=<path>` writes it to a file.
- **A check on request**: `r.DreamUI.VerifyKeptPointers` looks up again every object DreamGUI keeps between frames — a
  widget's canvas, a canvas's render layers, the UI manager's canvases, an animated property's object — and says where
  the two disagree, as `r.DreamUI.VerifyPartialPrepare` does for a canvas's prepare.
- **Switches to measure with.** `DreamGUI.Text.SmallTextCoverage` and `DreamGUI.Text.SmallTextMaxPixelSize` override
  the small-text settings for every font, and a change repaints every text. `DreamGUI.Text.SmallTextOnMove`,
  `DreamGUI.Scroll.SnapToDevicePixels` and the `bFieldTextCorrection` setting try other ways to draw moving, scrolled
  and large text, and are off until measured. `DreamGUI.Text.IncrementalParse`, `IncrementalMeasure` and
  `InPlaceDisplayList` each turn part of the incremental layout off, and `DreamGUI.Text.VerifyIncremental` holds it to
  layouts made from nothing.

### Performance

- Widgets whose render transform keeps changing become **render layers** that their canvas moves on the GPU, any number
  of them sharing a draw call.
- World transforms are composed when they are read, and moves are announced once a frame.
- Widget animations made of plain property tracks are played by DreamGUI's own player rather than the sequencer's
  entity system.
- World-space canvases are drawn in shared render-graph passes, and the per-panel work moved to the workers. The two
  5000-button benchmark levels run at the 60 FPS cap.
- A canvas walks only the widgets that asked to change, patches in place the sections whose geometry changed, and keeps
  its draw calls when its widgets only moved.
- An edit inside a long paragraph reads and measures only a window around it, and the text's display list is edited
  where it stands rather than written again.
- A world repaints at most `SmallTextRepaintBudgetPerFrame` (512) texts a frame onto coverage glyphs, and a small text
  drawn from the field while it turns, scales or moves is looked at again once it has held still, not at every move.
- Incremental layout keeps every switch-off path exact: an edit near the start of a long paragraph that opens with
  punctuation, a digit or an emoji, or one just after an inline image, is shaped as the whole paragraph would shape it.
- In the editor, DreamGUI's own copy of ICU is given the engine's ICU data, so line breaking follows the game's culture
  there too, as it does in a packaged game; without the data it falls back to the engine's iterators and says so once.

### Fixed

- **Field text composited its layers premultiplied**: the underlay, glow, outline and face of a field glyph were mixed
  as straight colours, so over nothing a layer's colour was pulled towards black and came out at its alpha squared. A
  translucent or fading text darkened, a gradient's transparent stop greyed, glows fell off too fast, and light text's
  edges were thinner than Slate draws them in the viewport. Each layer now goes over the one under it.
- **Viewport capture in a game**: `UDreamUICaptureLibrary::SaveViewportToPng` and `ReadViewportPixels` read a game's
  viewport, which is drawn straight into its window and has no texture between frames: that fired an ensure and gave a
  black picture. They now return false there, and say to use the engine's screenshot request.
- **Chinese fallbacks in a packaged game**: the ICU data a game is cooked with (English or EFIGSCJK) has no likely
  subtags, so the engine expands `zh-CN` to `zh-CN, zh` with no `zh-Hans`, and fallback entries for `"zh-Hans"` or
  `"zh-Hant"` matched in the editor but never in the package. DreamGUI puts the script back for Chinese culture names.
- **Crashes on the render thread**: a renderer freed while its render commands were still waiting (the intermittent
  all-preset crash); render roots and render-graph passes that held a raw pointer to something that could go first now
  hold it by reference.
- **Extensions**: TextAnimation's properties pass over a character that has no vertices instead of reading past them;
  a cylinder render-target surface whose canvas has no target or that has no vertices, a static mesh set after its
  widget left its canvas, a 2D line whose children changed in number, and render-target input sent to a canvas that was
  replaced no longer crash or misdirect; a cylinder's arc of 0 is 1 degree, and a negative arc's bounds are on the side
  it bends to. The retainer box retains: it sets its canvas to render-target mode, puts it back after, leaves editor
  worlds alone, and warns once when it has no `DisplayVisual`. The static mesh visual refuses a mesh past the vertex
  budget, with an error, instead of truncating its indices, and its bounds are where the canvas draws it. The
  render-target surface (`UDreamUIRenderTargetGeometrySource`) in StaticMesh mode puts its material into the static
  mesh's slot 0 only in play: in the editor, the transient instance it put there made saving the level write null over
  the mesh's own material. Pixelate hands its target to its proxy; pixel sort keeps its pass count within 1 to 512; the
  lyrics' TTML reader stops at 256 levels of nesting; TextAnimation properties set at run time start, and the ones they
  replace stop.
- **DreamTween**: sequences survive children the collector took; a callback that restarts or seeks its own sequence no
  longer loops, and a seek nested 16 deep is refused with an error; `Kill` fires its callbacks once; finished and
  killed tweens retire and their handles read invalid — `Kill`, `ForceComplete` and `Goto` do nothing on a killed or
  retired tween, `Restart` on a retired one only warns, and `Restart` from a tween's own `OnComplete` keeps it running;
  `ForceComplete` on a tween that has not started starts it first; `Goto` to the end completes once, and `Goto(0)`
  applies the start value; an empty curve falls back to linear with a warning instead of freezing; colour and alpha
  tweens round and clamp rather than wrap on overshooting eases; InExpo ends exactly at 1; a backward yoyo cycle ends
  at 0; a spring at rest wakes on `SetTarget`; `RepeatCall` with a count of 0 calls nothing; time scale, delay and
  delta are checked; a second tick helper no longer doubles tween speed; a tween whose owner is gone is dropped without
  callbacks; layout animations and the embedded UMG widget follow the pause and time-dilation rule other widget tweens
  follow.
- **Controls**: about forty findings of an audit, each with a test — list and scroll view rows during item changes, a
  heap write before a text field's buffer, a dropdown holding a raw pointer, buttons and selections switched off while
  pressed, drags whose source is destroyed, the key selector, the text field editing by UTF-16 offset with the IME.
- **Text**: packed text-style values that made a denormal float — a zero first value of a pair, such as face softness 0
  beside a dilation, or an effect alpha of exactly 128 — are nudged to the nearest normal one, so a GPU that flushes
  denormals no longer turns them to 0; a rich-text tag name too long for an `FName` (1024 characters or more) is shown
  as text instead of asserting.
- **Rendering**: a mesh whose shadow copy would take it past the vertex budget is drawn without the shadow, with one
  warning, rather than not at all.
- **Fonts**: bitmap fonts reload on undo and redo; an embedded font file's bytes are read again at cook, and an empty one
  is a cook error; a font's atlas texture and its list of texts are no longer recorded in undo transactions.
- **Input**: a player index with no player no longer gets an input user made for it.
- **Widget Blueprints run their graphs in a game on uncooked content** (`-game`, Standalone): `DreamGUIEditor` is
  UncookedOnly.
- **The editor**: a designer gesture is one undo step; the designer writes its edits to the `.dui` the asset names, and
  never over a file changed on disk; a recompile, and the designer's own rebuild of its preview, no longer record into
  an open transaction.

### Platforms and servers

- Only Win64 has been built and run: the editor and the automation suite, and a packaged Development game once the
  release gate has run the test host's packaged text smoke test. Mac, Linux, iOS and Android are listed by the
  descriptor but have never been compiled (the README's Platforms section says so instead of “Builds: yes”; see
  [Platforms](../guides/platforms.md)).
- The UI manager no longer starts in a dedicated server's worlds (a play-in-editor server included): no canvases, no
  renderer, no paint rows.

### Documentation and tools

- `Docs/FontsAndPackaging.md` ([Fonts and packaging](../guides/fonts-and-packaging.md)): colour emoji fonts, three
  ways to package a font file, the ICU packaging preset and what degrades without it.
- `Docs/Migration.md` has a section for moving from 2.0 to 2.1 ([Upgrading](../guides/upgrading.md)).
- msdfgen's MIT licence ships with the plugin, beside its source.
- The test host has a packaged text smoke test, part of the release gate: a probe in its game module writes what a
  packaged game draws, and `Tools/Tests/compare_text_smoke.py` holds it to the same build run on uncooked content.
- `Tools/ModuleSplit/check_game_includes.py` also finds engine macros and globals a file uses without including what
  defines them, editor headers in code a game compiles, and runtime `Build.cs` files that depend on editor modules.
- The render benchmark measures small text with coverage on and off under seven kinds of motion; the text parity run
  measures stroke edges, ink in linear light and grey against Slate, and reports gradient fills beside Chrome's
  `background-clip: text`.
- `Tools/Bench` holds the benchmarks and the scripts that read their profiles ([Benchmarks](../tools/bench.md)); the
  test runner has presets, a nightly script and a coverage table of which control is tested with which input
  ([Tests](../tools/tests.md)).

### Changes to know about

Upgrading from 2.0 changes some defaults and behaviour: the default font (texts reflow), small-text rendering, Tab, keys
and the pad acting on the focus rather than the hover, the focus look, player indices, scroll bars and lists as Tab
stops, the effect alpha of faded outlined text, how field text composites its layers, MSAA blending, line breaking by
the game's culture, and the 64-byte vertex with its new `UV4` channel for anyone with custom shaders or mesh modifiers.
They are listed in [Upgrading](../guides/upgrading.md), “From 2.0 to 2.1”.

## 2.0.0 (development build)

The modular release (2026-09-29).

- **Modules.** The single runtime module is split by layer: `DreamGUIRenderer` (the view extension, shaders and
  proxies), `DreamGUI` (widgets, visuals, canvases, layout, text, `.dui`), `DreamGUIInput` (event systems, raycasters,
  navigation, the viewport client), `DreamGUIControls`, `DreamGUIExtensions`, `DreamGUISamples` and `DreamTween`, with
  `DreamGUIEditor` and `DreamGUIK2Nodes` uncooked-only and `DreamGUITests` editor-only. A lower layer never includes a
  higher one.
- **Redirects ship with the plugin**: `Config/DefaultDreamGUI.ini` carries every redirect from LGUI and LexUI, from
  the prefab vocabulary, from the control renames and from the module split (763 of them).
- **Input per player**: each local player has its own pointers, focus and text target; pointer ids have ranges (mouse
  0, fingers 100 and up, scripted 1000 and up); an optional Slate input source hears input in every input mode;
  characters reach text fields through the game viewport client.
- **Rendering**: a canvas makes no material instance and answers its materials' parameters through render-thread
  proxies; a render-target canvas is drawn by a render command of its own; the screen effects read and write the
  screen through one path on the render graph's textures.
- **Widget Blueprints** replace the prefab asset model; the designer was rebuilt against UMG's; a widget's hierarchy can
  be authored as text (`.dui`), with timelines, routes and styles.
- **Layout** along the lines of Blink and Yoga, with the UMG-shaped panels; the Lex layout family is gone.
- **Removed**: the root Blueprints, `UDreamWidgetPresenterComponent`, the world-space raycaster source family, and the
  settings and console variables that put old renderer behaviour back. See [Migration](../guides/migration.md),
  “What no longer exists”.
