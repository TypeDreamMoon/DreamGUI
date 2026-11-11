# Changelog

What changed in each version of DreamGUI, grouped by what it changes for a project. Moving a project from one version to
the next -- what loads differently, which defaults moved, what C++ has to change -- is in
[Docs/guides/migration.md](Docs/guides/migration.md).

**1.0.0 is the first public release.** The builds before it were the fork's development line: the single-module 1.x
builds, then 2.0.0 and 2.1.0 (commits `2416e3f8` and `3049561a` on `main`). Public numbering starts again at 1.0.0, so a 2.0 or a 2.1
below is a development build and older than 1.0.0. The descriptor's integer `Version` keeps rising -- 100 for 1.0.0,
after 3 for 2.1.0 -- so the engine never takes 1.0.0 for the older of the two.

## Unreleased

### New

- **Clip masks.** A widget that clips to its bounds takes a mask on its clip (`ClipMask`): an ellipse, polygon, star or
  ring fitted to the clip rect, a feathered edge, a texture's channel laid over it (stretched, fitted, filled or tiled) or
  a procedural pattern (linear, radial, angular, diamond, blinds, checker, noise), through a threshold and softness that
  make wipes and dissolves, inverted if asked -- a spotlight. It clips the whole subtree on every path that draws it, and
  can be hit-tested by the mask. A chain of clips with no mask costs and uploads exactly what it did. See
  [Clip masks and clipping to the layer below](Docs/concepts/clip-masks.md).
- **Clip to the layer below.** `bClipToBelow` shows a widget, and everything under it, only where the sibling drawn before
  it is drawn and through that sibling's opacity: a clipping mask. The layer below can be a simple or nine-sliced image,
  texture or sprite, or a rect block; anything else is not clipped to, with a warning.
- **Blend modes.** `EDreamUIBlendMode` grows from three modes to 27: dissolve, darken, colour burn, linear burn, darker
  colour, lighten, screen, colour dodge, lighter colour, overlay, soft light, hard light, vivid light, linear light, pin
  light, hard mix, difference, exclusion, subtract, divide, hue, saturation, colour and luminosity, by the W3C compositing
  formulas in encoded values. Screen, exclusion and dissolve are done by the blend hardware and batch as before; the rest
  blend over a copy of what is behind, made per draw, and elements of one such mode share a draw while they do not
  overlap. An element drawn through a material with one of the new modes is put down through a group of one. See
  [Blend modes and layer groups](Docs/concepts/blend-modes-and-groups.md).
- **Layer groups.** `UDreamLayerGroup` on a container draws its subtree into a texture of its own and puts it down as one:
  `GroupOpacity` fades it without its children showing through each other, `GroupBlendMode` blends it as a whole,
  `GroupMask` masks the composite. Inside an isolated group the children's modes blend with the group alone. Groups nest;
  their textures come from the frame's pool. Clipping to a layer below that no clip level follows -- a text, a tiled image,
  a container, a group -- is done through such a group, and `UDreamRetainerBox` puts its retained texture down through
  the same composite (with a new `GroupBlendMode`). On every canvas DreamGUI's own renderer draws -- world-space
  canvases in World Space - DreamUI Renderer and the designer's and the level editor's previews included, a group's
  texture occluded by the scene as its elements are; a canvas the engine's renderer draws passes them through, with a
  warning.

- **View models.** A `.dui` file declares the view models its class holds in a `viewmodels { … }` block -- by class,
  with where each comes from: the host (a creation pin, `SetViewModel`, a host's `.dui` line), `= new`, `= global` /
  `= global "Name"` from the new `UDreamViewModelSubsystem`, or `= parent` from the nearest enclosing widget. Bindings,
  routes and loops reach into them through member paths: `Text <- Player.Name`, `Value <-> Settings.MasterVolume`,
  `OnClicked += Settings.Apply()`, `OnValueChanged += Settings.SetVolume(Value)`, `for Item in Inventory.Items`. Any
  UObject class can be one; `UDreamViewModel` is the convenient base (with `DREAM_VM_SET` for C++ setters), and anything
  implementing `INotifyFieldValueChanged` -- UE's `UMVVMViewModelBase` included -- is subscribed to, with no dependency
  on the ModelViewViewModel plugin. A path whose objects are not all set is not evaluated, and the property keeps its
  value. See [View models](Docs/language/viewmodels.md#view-models).
- **Bindings update when what they read changes.** The compiler records every variable and member a `<-` expression
  reads; when all of them announce their changes (FieldNotify), the binding is subscribed and costs nothing between
  changes. `Text <- Title` and `Shown <- Count > 0` on FieldNotify variables used to be read every frame.
  `DreamUI.Binding.Dump` lists which bindings are read every frame and why; `DreamUI.Binding.ForcePoll 1` reads them
  all every frame again.
- **Rows update alone.** In a `for` or an `each`, a copy whose item announces a change of a member it shows is updated
  without refreshing the list, and `Event -> Item.Func()` routes each copy's event to its own item -- moved along when a
  list view recycles a cell. With a typed source (`TArray<UItemVM*>`) the compiler checks `Item.Member` (DUI6021) and
  `Item.Func` (DUI6022).
- **A C++ `UDreamUserWidget` subclass can declare FieldNotify members.** `DreamUserWidget.h` now includes
  `FieldNotificationDeclaration.h`, which what UHT generates for them needs, as UMG's `Widget.h` does; such a class used to
  fail to compile. A `.dui` binding to the member subscribes through its descriptor, and `DREAM_VM_SET` is its setter.
- **`+=` and `=` on events**, beside `->`: `+=` adds a listener and takes only a multicast event, `=` is the one listener
  of a single-cast delegate -- which routes can now name at all -- and takes only that; the wrong one is DUI5025. `->`
  takes either.
- **Create Dream Widget, Add to Viewport and Add to Player Screen**, UMG's Create Widget node and the two user
  widget calls that go with it. Picking a class on the node gives it a pin for each Expose on Spawn property -- every
  `props` entry of a .dui file is one -- and an Owning Player pin, and types its result as that class. The pins are
  assigned before the widget initializes, so Pre Construct, On Initialized, On Construct and the first value of every
  binding already see them; UMG assigns after Create, which is too late for On Initialized. Add to Viewport puts the
  widget on its owning player's screen and, as in UMG, refuses one that already has a parent; Add to Player Screen also
  refuses a widget with no owning player. Remove from Parent was already there. C++ gets the two halves of the node as
  `BeginCreateDreamWidget` and `FinishCreateDreamWidget`.
- **Add Component lists only the components the selected widget supports** (`FDreamUIComponentSupport`). A mesh
  modifier needs a visual that builds a mesh, a sprite player a Sprite, a sprite-sheet player a Texture, a hyperlink or
  the text animation a Text, a UMG interaction a UMG Widget visual; a retainer or invalidation box needs the widget's
  canvas first; a canvas, navigation, animation component, named slot or content widget is one per widget, a widget is
  one kind of selectable and one scroll view, and a content widget is not offered on a widget with several children.
  Each of these used to be addable anywhere and then sat there doing nothing. Paste, duplicate and component assets
  dropped on the panel follow the same rules; a refused drop says why. A project adds rules for its own components
  from its editor module.
- **A prompt on the action bar runs the action it shows** when it is clicked, tapped or confirmed with the pad, as
  CommonUI's bound action button does (`UCommonBoundActionButton::NativeOnClicked`): an entry class that is a button --
  `UDreamButton`, or a prefab built around a `UUIButton` -- is wired to `UDreamUIActionRouter::ExecuteAction`, new, which
  runs a binding by its handle while its key would. A disabled bar's prompts click nothing and the pad passes over
  them. An entry with no button still only shows its prompt.
- **`UDreamListViewBase::FingerDrag`** (List, Tile and Tree views): what a finger dragged along rows that can be picked up
  does. `ScrollList`, the default, scrolls the list, as UMG's does; `PickUpRow` picks the row up, as the mouse does, for a
  touch-first screen whose rows are dragged by finger. The mouse is the same either way.
- **`bMenusConsumeOutsideClick`** in the project settings (Dream GUI, Input): on, a press outside an open menu or
  dropdown list only closes it, as DreamGUI's menus and lists did before; off, the default, it closes it and goes on to
  what it landed on (see Changed).
- **`UUIButton::GetClickEventData`**: while a button's click listeners run, the pointer event the click came from --
  whose it is, which pointer, a finger or the mouse, navigation or not. The click delegates carry no arguments, and a
  C++ listener that has to tell a tap from a click asks this; null for a click raised by broadcasting the delegate.
- **The designer has a rotate handle**, which UMG's designer does not. A single widget selected in the 2D view gets a
  disc on a short stem above the middle of its top edge, standing off it along the widget's own up, so it turns with the
  widget. Dragged round, it turns the widget in its own plane about its pivot by the angle the pointer goes round,
  clockwise on screen being a positive roll, and shows the angle as it goes; with Shift held the angle lands on a whole
  number of 15 degrees, and grid snapping leaves angles alone. The angle is written to `RelativeRotationEuler`, in the
  asset and in the `.dui` the class names, as one undo step; Esc during the drag puts the angle back. The pointer is a
  hand over the handle. Two widgets selected have no handle, and the 3D view keeps the engine's transform gizmo.
- **Adjustment layers.** `UDreamAdjustmentLayer` works on what is behind it, as far as its rect, clips and masks reach: up
  to eight operations in order -- brightness and contrast, levels, curves, exposure, hue and saturation (or colorize),
  vibrance, colour balance, black and white, photo filter, channel mixer, invert, posterize, threshold, gradient map and a
  lookup table -- mixed with what they were made from by any of the 27 blend modes and an opacity. With `Target` set to
  `Children` it works on its own widget's subtree instead, drawn as a group. See
  [Effects on what is behind](Docs/concepts/backdrop-effects.md).
- **Progressive blur.** `UDreamBackgroundBlur` takes a `StrengthMap` -- a texture's channel, a linear or radial ramp, or
  a clip mask's pattern -- and blurs each pixel as strongly as the map says there, from a blur pyramid over what is
  behind. With no map it is the uniform blur it was.
- **Glass.** `UDreamGlass` is a rounded rect of glass whose edge bends what is behind it, red and blue a little apart,
  optionally frosted, tinted and lit along the edge; its corners are a rect block's, and a click outside them misses it.
- **Stylize effects.** `UDreamStylizeEffect` lays up to eight layers over what is behind it or over its own children: a
  CRT screen, a glitch (`TriggerBurst` flares it), chromatic aberration, a halftone screen of one ink or four, ordered
  dithering, film grain, a vignette, posterization.
- **Layer styles.** `UDreamLayerStyle` puts an image editor's layer style on any widget (`FDreamLayerEffects`): drop
  shadows, an outer glow, inner shadows, an inner glow, a bevel and emboss (inner, outer, emboss, pillow, stroke; smooth or
  chiselled), a satin, colour, gradient and pattern overlays and strokes (outside, inside, centred; colour, gradient or
  pattern), each with its own blend mode, opacity and the dialog's own settings -- contours, noise, spread and choke,
  range and jitter, global light. `UDreamLayerStylePreset` keeps one to share. A rect block draws its style in its own
  pixels, batched as before; anything else -- a text, an image, a container's subtree, a layer group -- is drawn apart,
  measured by a jump flood of its alpha, and put down layer by layer, each outer effect by its own mode. A rect block's own
  shadows, border and gradient read and write as a layer style (`GetEffectsAsLayerStyle`, `SetEffectsFromLayerStyle`). See
  [Layer styles, shape blends and light](Docs/concepts/layer-styles.md).
- **Shape blends.** `UDreamShapeBlendGroup` melts the rect blocks among a container's children into one shape by a smooth
  minimum -- a union, a cut or an intersection each, by `ShapeBlendOperation` -- drawn by the first of them in one quad, in
  the members' blended colours, with the group's layer style round it; a click lands on the nearest member, by the same
  formula.
- **Pointer light, tilt card and holo foil.** `UDreamPointerLight` follows the pointer and lights the bevels of the layer
  styles under it and the borders of the rect blocks under it (a reveal highlight); `UDreamTiltCard` tilts a widget towards
  the pointer on a spring; `UDreamHoloFoil` draws an image or a rect block with a trading card's foil, its angle of view
  the pointer's or a tilt card's.
- **Glass in a melted shape.** `UDreamGlass` with `Shape` set to `ShapeBlend` takes the shape a shape blend group melts its
  rect blocks into -- the group on the widget `ShapeBlendGroup` names, or on its own -- and is drawn over the whole of it,
  wherever the members reach, its edge bending what is behind along the melted outline; a click lands on it wherever the
  shape is. See [Effects on what is behind](Docs/concepts/backdrop-effects.md#glass).
- **Mesh warps.** `UDreamMeshModifierWarp` bends what any batch-mesh visual draws -- an image, a rect block, a text
  glyph by glyph -- by one of fifteen styles named as an image editor's text warp names them (arc, arc lower and upper,
  arch, bulge, shell lower and upper, flag, wave, fish, rise, fisheye, inflate, squeeze, twist), with a bend and a
  horizontal and vertical distortion, or by a Bezier lattice of 2 to 5 points each way, dragged in the designer's
  viewport. The mesh is cut finer first along one grid shared by the whole mesh, every attribute interpolated, so rounded
  corners, glyph distance fields and pixels bend with it; at rest it costs nothing. See
  [Warps, jellies and text on a path](Docs/concepts/warp-and-text-on-path.md).
- **Jellies.** `UDreamMeshModifierJelly` holds the mesh on a lattice of damped springs that lag behind the widget's
  moves, squash while it is pressed, bounce when it is let go and stretch after a drag, stepped in fixed steps so that
  the same frames give the same wobble; presses and drags pass on to the widget's button or scroll view.
- **Text on a path.** `UDreamMeshModifierTextOnPath` lays a text's glyphs along a circle, an arc or the path a
  `UDreamPath` widget draws (a new line visual through editable points, straight or smooth, open or closed, drawn or
  not): aligned to its start, middle or end, spaced, lifted off it, flipped to its other side, scrolled along it, each
  glyph turned whole or bent; a text animation's moves are kept.
- **Modifier stages.** Mesh modifiers run in stages (`GetStage`): those that work on characters, text on a path, the
  warps that cut the mesh, then the copies (shadow, outline, long shadow) -- which now copy a bent mesh whichever
  component was added first.
- **Mask transitions.** `UDreamMaskTransition` reveals or hides a widget's subtree through a moving clip mask -- wipe,
  clock, iris, diamond, blinds, checker, dissolve, ink, shatter or a texture's order -- with softness, reverse, invert
  and a centre; its `Progress` takes tweens and property tracks. `CrossFade` swaps two widgets through one shape, each
  pixel shown by one of them, and `PlayScreenTransition` covers and uncovers the screen. Ink, shatter and checker read
  textures made in code. It drives the widget's clip (`UDreamWidget::SetDrivenClipMask`, new) without touching its
  `ClipMask`, and fully shown it costs nothing. See [Mask transitions](Docs/concepts/mask-transitions.md).
- **Reversed clip masks.** `FDreamClipMask::bReverse` runs a texture's or pattern's value the other way before the
  threshold: a gradient from right to left, a radial one from the edge in.
- **Niagara particles.** A new module, `DreamGUINiagara`, draws Niagara systems on DreamGUI canvases: the `NiagaraSystem`
  visual simulates a system with a component that renders nothing and is out of the scalability's reach, reads each CPU
  emitter's sprite and ribbon renderers after the tick, and writes them as segments of its own -- one draw call each --
  clipped, masked, blended and grouped like any element. Sprites keep their sheet frames and the blend between them, pivot,
  rotation, alignment and sort; ribbons their ids, link order, both UV sets and a curve through their points. One unit is a
  pixel on the widget's plane (XZ or XY); time can be paused, dilated, run while the game is paused, stepped at a fixed rate
  and advanced. The built-in shader draws them with the particle material's texture and blend, or a converted UI material
  does: **Convert Particle Materials** copies a particle material as a UI one, its particle inputs read from the vertex and
  its opacity through DreamGUI's clip. A system that declares `User.DreamUI_WidgetSize`, `User.DreamUI_PointerPosition`,
  `User.DreamUI_PointerInside` or `User.DreamUI_TargetPosition` is given it. `BurstAt` spawns the system again at a point,
  `BurstTo` sends a flight -- the plugin's fly-to template by default -- to another widget, and the `Niagara Trigger`
  component plays a system on clicks, the pointer coming and going, and a progress filling. GPU emitters and mesh renderers
  are reported and not drawn yet. The plugin now depends on Niagara. See [Niagara particles](Docs/concepts/niagara.md).
- **Direct-mesh segments.** A `UDreamVisualDirectMesh` can draw in several segments, a draw call each, can leave its
  shading to the canvas -- the built-in shader or a material, with the clip, the clip masks' textures and the blend mode, as
  a batched element is -- and can write its vertices every frame the canvas sends its sections.
- **Layered document import.** A `.psd` or `.psb` imports as a DreamUI widget: a `.dui` laying its layers out as widgets,
  their textures, and a widget Blueprint whose source the `.dui` is -- from a folder's DreamGUI menu, a commandlet
  (`-run=DreamPsdImport`), or the Content Browser's Import once a setting says so. Pixel layers become images of trimmed
  textures, groups containers (a layer group where they blend or fade), live rectangles and circles rect blocks, fills a
  block's body, texts texts through a font map; masks become clip masks, clipping `bClipToBelow`, blend modes the 27
  modes. Layer names can say `!skip`, `@Button` and `#9(l,t,r,b)`. Importing again edits the `.dui` the last import
  wrote, keeping every node and line an author added and listing what it overwrote. Layer effects, adjustment layers and
  text warps are read and listed, not imported yet. The new editor module is `DreamGUIPsd`. See
  [Importing layered documents](Docs/guides/layered-document-import.md).
- **Multi-stop gradients on rect blocks.** A rect block fills its body and draws its border with `BodyGradient` and
  `BorderGradient`, each an `FDreamGradient` of up to 16 stops -- linear, radial, angle (conic), diamond, reflected or four
  corners, the gradients a text paints with, from the same rows of the world's paint rows -- in place of the two-colour
  radial gradient, by the built-in shader and through the rect block's material (`MF_DreamUI_RectBlock` reads the paint
  rows). The details panel shows each as a strip with its type and the presets; a `.dui` writes its CSS. A two-colour
  gradient from before carries over: drawn as two stops (to a level per channel) while the gradient has none, its `.dui`
  lines kept until the gradient is edited, then replaced by the gradient's line (`meta=(DreamGUI_Replaces)`); the
  old setters still work. `GetEffectsAsLayerStyle` and `SetEffectsFromLayerStyle` carry the stops. See
  [Gradients and viewport handles](Docs/concepts/gradients.md).
- **Gradient handles in the designer.** A selected rect block's gradient is drawn over it as an image editor's gradient
  tool draws one: a linear's or a reflected's start and end, a radial's, an angle's or a diamond's centre and end (and a
  radial ellipse's second radius) to drag; its stops beside the line, dragged along it or off it to take one out; a double
  click on the line adds a stop of the colour there. Shift keeps a turn to 15 degrees, Esc puts a drag back; a drag is one
  undo step and is written to the `.dui` when the button comes up.
- **Viewport handles for anything.** `IDreamDesignerHandleProvider`, registered with `FDreamDesignerHandles::Register`
  (DreamGUIEditor, `Designer/DreamDesignerHandles.h`), gives the selected widget points to drag in the designer's viewport;
  the viewport does the hit testing, drawing, the one transaction per drag, Esc, double clicks and the write to the asset
  and the `.dui`. A warp's lattice points and a path's points now go through it.
- **Handles a property asks for by its tag.** `meta=(DreamGUI_Handle="Kind")` (or `"Kind:Args"`) on a property of a widget,
  its visual or a behaviour gives it the handles of that kind in the viewport, from a provider the kind's factory makes for
  it (`FDreamDesignerHandles::RegisterKind`). A rect block's `BodyGradient` and `BorderGradient` are tagged `Gradient`. See
  [Property meta tags](Docs/guides/property-meta.md#viewport-handles).
- **Reflected gradients.** `EDreamPaintType::Reflected` (`reflected-gradient(…)`) runs both ways from a line through the
  gradient's centre, for texts as for rect blocks.
- **Handles for masks, glass, shadows and lights.** The designer's viewport draws handles for a clipping widget's clip and
  mask -- the clip rect's sides (its margins), a rect mask's corners, a polygon's roundness, a star's or a ring's inner size
  and the feather, along the outline the mask's own distance function cuts; a glass's corners, its edge's width and its
  light; each shadow of a layer style or a rect block, dragged where it falls to set its angle and distance together (a
  style's shadow on the global light turns the global light, and every effect on it with it); a pointer light's place and
  reach, and a layer style's global light as a disc. Each comes with its property's tag (`DreamGUI_Handle`: `MaskShape`,
  `GlassShape`, `Shadow`, `Light`). Corners and feathers move together, Alt one alone; Shift snaps an angle to 15 degrees.
  See [Viewport handles](Docs/tools/designer.md#viewport-handles).
- **An effect's light, read and written in one place.** `DreamLayerStyleLight` (`Core/DreamLayerStyleTypes.h`) answers and
  sets an effect's angle and altitude where they are kept -- the style's global light for an effect that uses it -- and turns
  a shadow's angle and distance into its offset and back; the renderer packs its shadows' offsets through it.
- **Copy and paste layer styles.** The designer's widget menu copies the selected widget's layer style -- and its blend mode,
  opacity, clip and mask when *Copy Includes* says so -- and pastes it onto every selected widget, in one undo step, written
  into the `.dui`. The clipboard holds lines of `.dui`, so a copy pastes into another Blueprint and into a text editor, and
  lines written by hand paste too. See [The widget designer](Docs/tools/designer.md#copying-and-pasting-a-layer-style).
- **An eyedropper and the project's palette.** Every colour row of the designer's details panel has an eyedropper that
  reads the designer viewport's own picture -- the pixel after its effects, style and blend mode -- or the screen's, and the
  project's palette (`UDreamGUISettings::ProjectSwatches`), a click away and added to from there. Both are widgets an
  editor of a project's own can use (`SDreamUIEyedropperButton`, `SDreamUIColorQuickPick`). See
  [The widget designer](Docs/tools/designer.md#the-eyedropper-and-the-projects-palette).
- **The preset library.** A Presets tab in the designer shelves the project's gradients, layer styles and effect settings
  -- text in the project settings (`GradientPresets`, and the new `StylePresets` and `EffectPresets`) and assets
  (`UDreamGradientAsset`, `UDreamLayerStylePreset`) -- applied to the selection with a double-click or by a drag onto a
  widget, followed or copied, and kept from a selection. A style preset asset can keep the layer's blend mode and opacity
  (`bIncludeBlending`); `DreamUIStylePresets` makes style presets for an editor of a project's own. See
  [The widget designer](Docs/tools/designer.md#the-preset-library).
- **A Layer Style dialog.** The designer opens an image editor's Layer Style dialog on the selected widgets -- *Layer
  Style...* in the widget menu, *Edit...* beside the layer style in the Layer section, or a double-click on a style mark
  in the hierarchy: the effects listed with their ticks, more than one of a kind added and reordered, each one's
  parameters beside them, every change previewed on the widgets at once, OK written into the `.dui` as one undo step,
  Cancel leaving the file as it was, and New Style keeping the style as a preset. See
  [The widget designer](Docs/tools/designer.md#the-layer-style-dialog).
- **Lists of structs are written back to a `.dui`.** A layer style's drop shadows, strokes and overlays, an adjustment
  layer's adjustments and every other list, set or map of structs with no short form is written the way it is read --
  `Style.DropShadows = ["(Angle=120,Distance=6)"]`, the fields that differ from the struct's defaults -- where the
  designer could read such a list from a file and never write one. See
  [Write-back](Docs/language/write-back.md#what-the-designer-writes-back).

- **The layer effects in the editor.** A widget's details open on a **Layer** category: opacity, its visual's blend
  mode, clipping to the layer below, the mask (a shape to pick, which turns clipping to bounds on), switches that add or
  turn off a layer style and a layer group, and every effect on the widget with an eye to turn it off and its name to open
  its settings. The hierarchy marks each widget's effects after its name -- style, group, mask, blend, effect, warp,
  particles -- with what they are in the tooltip and a click to turn them off and on. The palette has them all: Glass,
  Adjustment Layer and Stylize under Effects, Layer Style, Layer Group, Shape Blend Group, Mask Transition, Holo Foil,
  Pointer Light and Tilt Card under Components, Warp, Jelly and Text On Path under Modifiers, Path under Shapes, each
  arriving set up to be seen; a post-process Blueprint is listed by its name without `BP_` and `_C`. See
  [The widget designer](Docs/tools/designer.md).
- **Off switches on the layer effects.** `UDreamLayerGroup`, `UDreamMaskTransition`, `UDreamHoloFoil`,
  `UDreamPointerLight`, `UDreamTiltCard` and every post-process visual (`UDreamVisualPostProcess`: glass, adjustment layer,
  stylize, background blur...) have a `bEnabled` property, on by default and written to a `.dui` only when off: off, the
  effect is drawn as if it were not there and keeps its settings.
- **Details panels that cannot lose a property.** DreamGUI's details panels are laid out from `DreamGUI_` meta tags on
  the properties -- a section (`DreamGUI_Section="Layer"`), a group, an order, the advanced expander, a control, the
  switch of an effect -- and a property without them is shown under its own category. No panel hides a category whole any
  more, so a property cannot drop out of the editor for the category it was given; only `DreamGUI_Hidden` hides one. The
  Layer section and the effect switches in the hierarchy are built from the tags, and rows that apply only to some values
  (a fill, a transition's colours, a password character) show and hide with the value without the panel rebuilding. An
  extension's classes use the same tags. See [Property meta tags](Docs/guides/property-meta.md).

- **Editing controls.** Blend modes are pictures in their row and menu -- each drawn by the mode's own formula, all 27 in an
  image editor's order and groups -- and hovering one shows the widget drawn in it, recording and saving nothing until it
  is clicked. Angles tagged `DreamGUI_Editor = "AngleWheel"` (the layer style's, the gradient overlay's, the glass's light, a
  mask transition's rotation, a text path's start) are a dial beside the number, and an effect with Use Global Light on
  turns the style's global light from its own row. Contours are curves with the named shapes to pick and the curve to
  edit, and eases show their curves. In a `.dui` a contour is a named shape's word or `"custom(0 0, 0.5 1 corner, 1 0)"`.
  See [The widget designer](Docs/tools/designer.md).

- **A render offset, a rotation and a pose to tween.** `UDreamWidget::SetRenderOffset` and `GetRenderOffset` are the render
  translation on the canvas plane, with its depth left where it is. `RenderRotationTo` tweens the whole render rotation
  component by component -- a side panel swinging open on its hinge, a card turning over -- and `RenderPoseTo` tweens the
  translation and the rotation on one clock and one ease. See [Animation](Docs/concepts/animation.md).
- **Fonts for the expandable area and the ring menu.** `FDreamExpandableAreaStyle` and `FDreamRingMenuStyle` have a `Font`
  (none leaves the project's default font), as the text input, spin box and rich text styles do.
- **A warning when a character has no glyph.** A character in neither a font's own face nor any of its fallbacks is logged
  once for that font, with its code point, instead of showing a box with no word anywhere. The default font reaches
  Chinese, Japanese and Korean through `/DreamGUI/DefaultFont_DistanceField_CJK`. See [Text](Docs/concepts/text.md#fonts).
- **`.dui` classes compile from the command line.** The `DreamUICompileWidgets` commandlet compiles and saves every
  text-backed widget Blueprint, or the ones a manifest names, creating the missing ones, with no editor window and no
  Python. See [Compiling from the command line](Docs/tools/scripting.md#compiling-from-the-command-line) and [Workflow](Docs/getting-started/workflow.md).
- **A page of common pitfalls**, each with its cause and the page that explains it. See [Common pitfalls](Docs/guides/pitfalls.md).
- **C++ events on every control.** Each control's Blueprint event has a twin for C++ -- `GetOn<Event>Event()`, a
  `TMulticastDelegate` with the same arguments, raised with it -- so `AddLambda`, `AddWeakLambda` and `AddUObject` bind an
  expandable area's expansion, a list's selection or a ring menu's activation without a `UFUNCTION`.
- **A tap of navigation from code.** `UDreamStandaloneInputModule::TapNavigation` takes one step on the next frame, one per
  tap: a press and its release given on one frame took none. `UDreamEventSystem`'s *Navigation Repeat In Game Time* times a
  held direction's repeat on the game clock, for a recording made frame by frame.
- **Context for drag visuals and tooltips.** A drag visual class that implements `IDreamUIDragVisual` is told its operation
  (`OnDragVisualCreated`), and a tooltip class that implements `IDreamUITooltipContent` its source widget
  (`SetTooltipSource`), as soon as each is made -- before its first layout.
- **A world point as an anchored position.** `UDreamCanvas::ProjectWorldToAnchoredPosition` gives where a world point lands
  on a screen-space root, as the anchored position of a widget anchored to the root's middle: a marker over an actor.
- **One stylize layer's number by name.** `UDreamStylizeEffect::SetLayerParameter` and `GetLayerParameter` change a number
  of one layer (`AberrationAmount`, `ScanlineStrength`...) without building the whole list again for `SetLayers`.
- **Leave the mouse out until it moves.** `UDreamStandaloneInputModule::bIgnoreMouseUntilItMoves` keeps a mouse that rests
  over a menu as it opens from hovering a row on the first frame.
- **The designer's canvas size from scripts.** `UDreamWidgetBlueprint::GetDesignerCanvasSize` and `SetDesignerCanvasSize`.
- **`@tag`: find widgets by what they are.** A node carries names with `@tag Trim` or `@tag Glow, UI.Accent` (dotted and
  Chinese names allowed, on any widget node, in a slot declaration or a `rows` line's block, in a loop's template --
  every copy keeps them), and they land in the new `UDreamWidget::Tags` (`HasTag`, `AddTag`, `RemoveTag`; a `Tags = [...]`
  line on the same node adds to them). `UDreamUserWidget::GetWidgetsByTag` (Blueprint) and `ForEachWidgetWithTag` (C++) find
  the tagged widgets of a widget's own contents, parents before children, without going inside nested components --
  code no longer needs to match a decoration widget by a prefix of its id. A line that does not read, or sits in a `style`
  or directly in a `for` / `each` body, is the new DUI2024. The designer writes edits of `Tags` back into the `@tag`
  lines. See [Tags](Docs/language/nodes-and-values.md#tags-tag).
- **`@slot Size = (w, h)`** stands for `@slot MinDesiredSize` and `@slot MaxDesiredSize` given the same value, in a
  `@slot { }` block and in a style too. The designer keeps the shorthand while the two stay equal and splits it into the two
  lines when an edit makes them differ; the Blueprint converter writes it for a slot whose two values are one. Bound with
  `<-` it is DUI4001, naming the two properties to bind. See [`@slot Size`](Docs/language/slots-and-components.md#slot-size).
- **A struct is a map of its fields.** Any struct property, or struct inside an array, set or map, that has no short form
  reads `{ Distance: 12, Angle: 45 }` -- the field named as a property is, the rest left at the struct's defaults, text
  fields localized under their own keys -- where only the engine's own text in a quoted string was possible. A struct can
  name a primary field (`DreamGUI_PrimaryField`) that the map's first entry may give without its key. `FDreamRingMenuItem`
  does, with `Label`, so a ring menu is written `Items = [{ "Lamp", Tag: Lamp }, { "Door", Tag: Door, bEnabled: false }]`
  instead of being set from C++. The quoted form still reads; the designer and the Blueprint converter write the map for
  structs with a primary field and keep the quoted form for the rest. See
  [Structs as field maps](Docs/language/nodes-and-values.md#structs-as-field-maps).
- **Several styles.** A style, a node and a `rows` header take `: A : B` (`Text Title : Heading : Glow`), applied left to
  right, bases first, each style once at its first appearance, a later one over an earlier one, the node's own lines last.
  `: A : B` used to be a syntax error (DUI2001). Naming a style that is already applied changes nothing and is the new
  DUI3025, a warning. See [Several styles](Docs/language/styles-and-resources.md#several-styles).
- **`Tools/Get-DreamGUIEnvironment.ps1` prints what a bug report needs.** The plugin's version and commit, the engine's
  version and whether it is a launcher install or a source build, the operating system, its UI language and the shell,
  as plain text to paste into an issue. It starts nothing, needs no git, and prints no paths; what it cannot find is
  `unknown`. The issue forms ask for its output.

### Changed

- **One kind of viewport handles at a time.** The designer's viewport shows one kind of the selection's handles -- its
  gradient's, its mask's, its shadows' -- named under the selection with the kind Tab shows next; **Tab** steps through
  them and then to none, the selection box alone. `FDreamDesignerHandles::SetActive` picks one, the *Edit in Viewport*
  button under a rect block's gradient among others; a rect block's body and border gradients are two kinds.

- **A binding that reads only FieldNotify variables no longer reads them every frame** once its Blueprint is
  recompiled: it updates when they announce a change. C++ that assigns such a variable without broadcasting it -- which
  FieldNotify never allowed -- is no longer covered up by the poll; `DreamUI.Binding.ForcePoll 1` finds it.
- **Create Dream Widget Of Class is no longer in the Blueprint menu**: the Create Dream Widget node stands for it, as
  UMG's Create Widget node stands for `UWidgetBlueprintLibrary::Create`. Graphs that already call it keep compiling.
  Being BlueprintInternalUseOnly, it is also gone from Python.
- **The Content Browser's Add menu offers DreamGUI Widget at its top**, beside the engine's Blueprint Class and
  Material, rather than only one submenu down. The submenu itself, now labelled DreamGUI, is in sections -- Basic
  (DreamGUI Widget, Widget Animation), Fonts, Graphics, Rich Text -- with short labels and a one-line tooltip each,
  instead of one alphabetical list of type names with the widget last and the classes' code comments as tooltips. The
  bitmap font, which is no longer developed, reads Legacy Bitmap Font and points to the distance-field font. Asset type
  names, on tiles and in filters, are unchanged.
- **The designer's Palette is grouped the way UMG's is.** Basic, Panels, Common, Input, Lists, Scrolling, Containers,
  Primitive, Shapes, Effects, Components, Modifiers, Advanced, User Created, and last Legacy, which starts closed --
  where there was one Controls category of thirty-odd rows of every kind beside a dozen small ones that differed by a
  word. Rows are listed by name, the panels lose their "UMG " prefix, and the Tile View and Scroll Box behaviours moved
  to Legacy beside the controls that replaced them. User Created no longer lists the plugin's own preset folder. The
  hierarchy's Create menu uses the same groups. Registry keys are unchanged, so favourites keep their stars; a project
  extension that compared `FDreamUIControlDescriptor::Category` with "Controls", "Post Process", "Extensions" or a legacy
  category name reads the names from `DreamUIPaletteCategory` instead.
- **The details panel puts what a widget is right under where it sits.** Slot, Layout, then Visual, Panel and Self
  Layout, then Appearance and Behavior, then Render Transform and -- collapsed -- Perspective, Navigation,
  Accessibility and Localization; the visual used to come after Behavior, Appearance, Panel and Self Layout. A Text's
  rows lead with Text, Font, Size, Style, Color and alignment, with its wrapping, typography and rendering settings in
  groups that start closed, and a Sprite's or Texture's colour sits under its sprite or texture. The Transform and
  anchor-data categories at the bottom, which repeated the Layout rows raw, are gone, and Is Enabled joined Behavior.
- **A list, tile or tree view in Multi mode chooses rows as SListView does.** A plain click selects the row it lands on
  and nothing else, where it used to add the row to the selection and a second click took it away; Ctrl adds a row or
  takes it away; Shift adds every row from the range anchor to the one clicked, Ctrl with Shift too. A finger's tap adds
  the row and never takes one away (`STableRow::OnTouchEnded`). With a row focused, an arrow with Shift selects from the
  anchor to the row the focus moves to, with Ctrl and Shift adds that range, with Ctrl alone adds the row, and Ctrl+A
  selects every row. The anchor is the row the last plain click, Ctrl click, tap or plain arrow landed on. A click reads
  its modifiers from the clicking player's controller. See [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A focused spin box steps its value with the arrow keys**, as USpinBox's `SSpinBox::OnKeyDown` does: Up and Right
  add a step, Down and Left take one away, each press committed, the value kept inside the range a drag sweeps and on
  the StepSize grid. With StepSize at zero the step is SSpinBox's default -- one, or a tenth across a drag range of ten
  or less -- times ten with Shift, a hundred with Shift and Alt, a tenth with Ctrl, a hundredth with Ctrl and Alt. The
  arrows used to take the focus on to the next control; they no longer do, as in UMG. The D-pad still navigates, and
  in a field being edited the arrows still move the caret.
- **A finger never brings up a tooltip**, as Slate shows tooltips at the cursor alone: not for a tap, a finger held past
  the delay, or a finger dragged onto a widget. A finger's press still takes down a bubble that is up, and the bubble
  comes back after its delay under the mouse that brought it up -- which a finger's tap used to stop, by taking the
  tooltip over and keeping it after it lifted.
- **On a split screen each player's screen is that player's part of the viewport**, as UMG's AddToPlayerScreen lays a
  player's layer out over the part the split-screen layout gives the player. The screen root the screen UI makes for a
  local player -- the one Add to Player Screen, Add to Viewport, the page stack, tooltips, drag visuals and modal dims
  land on -- is sized to that part; a pointer is measured from the part's top-left corner, and one in another player's
  part reaches nothing on it; it is drawn in that player's view alone, under the shared layer. Every player's screen
  used to be a canvas the size of the whole viewport, drawn into every player's view. `UDreamCanvas::SetViewportPlayerIndex`
  gives any ScreenSpaceOverlay root canvas a player this way, and `GetViewportRect` says which part it fills. A game
  that is not split is unchanged -- its one player's part is the whole viewport -- and so is a canvas nobody gives a
  player, such as one placed in a level, which stays the shared full-viewport layer. See
  [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A finger on a slider's track moves the value only once it travels**, as `SSlider`'s touch handlers do: a finger
  that lands only notes where; past the drag distance the slider takes it -- `OnMouseCaptureBegin`, then the value under
  it -- and follows it until it lifts. A tap that never travelled changes nothing and begins no capture; it used to jump
  the value to the tap and capture at once, as the mouse does. The mouse is unchanged. See
  [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A finger dragged along a list of rows that can be dragged scrolls the list**, as `STableRow` gives a touch drag to
  its list; it used to pick up the row it landed on. The mouse still picks rows up, and `FingerDrag = PickUpRow` gives
  the finger the old behaviour. See [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A press outside an open menu goes on to what it landed on**, as the Slate menu stack lets it: a Dream Menu Anchor's
  menu, or a menu anchor panel's, closes on the press and the button under it is pressed and clicked as well. It used to
  close the menu and go no further. A press on the menu's own trigger still closes it without opening it again:
  `UDreamMenuAnchor::ShouldOpenDueToClick` says no for the click of the press that closed the menu. With a menu open,
  what is behind it is hovered as the pointer crosses it. `bMenusConsumeOutsideClick` in the project settings puts the
  old behaviour back. See [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A press outside a dropdown's open list goes on to what it landed on too**, as SComboBox's list is a menu on the same
  stack: the list closes, choosing nothing, and the button under the press is pressed and clicked. A click on the
  dropdown's own face closes the list without opening it again. The list follows `bMenusConsumeOutsideClick` with the
  menus. `UUIDropdown::bUseInteractionBlock` now means "keep the press for this dropdown whatever the project says" and
  is off by default; it was on by default, and on was the only way a list had ever behaved. See
  [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A click, a tap or the virtual cursor's confirm on a disabled widget reaches the widget around it**, as Slate cuts
  the path under the pointer at the first disabled widget and lets what is left take the event
  (`FHittestGrid::GetBubblePath`): the nearest widget above it that takes input is hovered, pressed and clicked as if
  the pointer were on it, and the events bubble on up from there. A disabled widget used to swallow the whole press,
  so the card around a disabled button heard nothing, and a press on a disabled entry of an open menu or popup counted
  as a press outside it and closed it; now the card hears it, and the menu stays up. The disabled widget and everything
  in it still hear nothing, are not hovered, and still lie in front of whatever is drawn behind them. Screen space,
  world space, render-target surfaces, fingers and the virtual cursor follow the one rule. See
  [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A disabled widget's tooltip comes up under the mouse**, as Slate looks for a tooltip under the cursor including
  disabled widgets (`FSlateUser::UpdateTooltip`) -- which is why CommonUI's buttons carry a disabled tooltip. It never
  did. `UDreamPointerEventData::HitWidgetIgnoringEnabled` names the widget drawn on top under a pointer, disabled or
  not.
- **A link in rich text is a stop for Tab, the D-pad and the stick**, as UMG's hyperlink is an `SButton`: in a game,
  `UUITextHyperlink` puts a focusable stop over each link it draws, which the focus ring is drawn around and whose
  confirm announces the link (`UDreamText::OnHyperlinkClicked`). The keys and the pad used to pass every link by. A
  link in a disabled text is passed over. `bLinksAreFocusStops` turns it off. See
  [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **A menu or a dropdown list opened on a panel in the level stays on the panel**, as UMG keeps a widget component's
  menus in its own window (`SVirtualWindow::OnQueryPopupMethod` asks for `UseCurrentWindow`, and `FMenuStack` hosts
  them in that window's popup layer). When what opens a popup -- a dropdown, a menu anchor, anything that calls
  `UDreamUIPopupLayer::Push` -- is in a tree drawn into the level, a world-space panel or a canvas drawn into a target
  a mesh shows, the popup goes up on top of that tree's own root canvas, in its plane and sorted above its content,
  where it used to be lifted onto the player's screen at a place unrelated to where the panel is seen, and no ray
  through the panel could reach it. It is drawn and hit where the camera sees it hang from its opener; a press
  anywhere outside it -- elsewhere on the panel, on another panel, on the screen -- closes it as on a screen, with
  Consume and PassThrough as before; Back, the focus, following the opener and the fades are the screen's. A popup
  opened on a screen, a retainer on a screen included, goes up on the player's screen root as before. `Elevate` alone
  follows the widget's own tree the same way. See [Migration](Docs/guides/migration.md#from-100-to-the-next-release).
- **An action named on a live action trigger is its key at once.** `UDreamUIActionTrigger::SetAction` on a trigger
  whose widget is active and interactable binds the new action there and then, letting go of the old one, as CommonUI's
  `SetTriggeringInputAction` does; clearing it lets go. It used to rebind only an action that was already bound, so
  an action given to a trigger whose widget was already up stayed unbound until the widget was hidden and shown again.
  Set on a hidden or disabled widget, the action is bound when the widget comes back, as before.

- **A reduced screen-space render scale is not used for a layer that blends with what is behind it.** Multiply and
  a material's Modulate, AlphaHoldout or Additive need the scene they draw over, which a transparent smaller target does
  not hold, so such a layer lost its effect when scaled ([#9]). It now draws at full resolution, as one with a post
  process already did.
- **A radial gradient's ellipse turns with its `Angle`**, as a diamond does (`radial-gradient(30deg …)` in CSS). A text
  paint whose radial gradient kept an angle other than 0 or 180 -- left from a linear it was switched from -- now draws
  its ellipse turned; a circle draws as before.
- **A rect block's two-colour gradient properties** (`BodyGradientColor`, `…Center`, `…Radius`, `…Rotation`, their unit
  modes, and the border's) are no longer in the details panel, and their setters are deprecated: on a gradient that has
  stops they set its first stop's colour, its centre, its radius and its angle, and the tweens follow. A centre or radius
  written in units keeps to units only while the gradient is two-colour. A body texture that is itself partly see-through
  is tinted by the gradient as by the body colour; it used to have the gradient colour laid over its alpha a second time.
- **The gradient row's header has a type picker** beside its strip and presets, for every `FDreamGradient` (text paints,
  gradient assets, rect blocks).

- **A tween replaces the earlier tweens of the same property.** Starting `RenderOpacityTo(0)` while `RenderOpacityTo(1)` is
  still running left both running, and whichever finished last decided the result: a tooltip shown and hidden quickly by
  hover could stay lit. Now a tween that starts stops the tweens already on that object's property -- those made before it,
  and those that have started -- killing them, or pausing the ones kept with `SetAutoKill(false)`; a later one that has not
  started (a fade out queued with a delay) is left to start in its turn. It holds for the widget's property verbs, the
  visuals' `ColorTo` and `AlphaTo`, the rect block's, the ring's and polygon's angles and the line renderer's width. A tween
  in a `UDreamTweenerSequence` neither stops nor is stopped. `UDreamTweener::SetReplacesEarlierTweens(false)` opts a tween
  out, and `SetExclusive` is the C++ hook for a verb of your own. See [One tween per property](Docs/concepts/animation.md#one-tween-per-property).
- **A text is no raycast target by default**, as UMG's TextBlock is not: text is mostly a label on something else, and a
  label laid over a button took the button's hover and clicks. A text that is itself pressed sets `bRaycastTarget = true`.
  Texts saved before keep the `true` they were saved with. See [Widgets](Docs/concepts/widgets.md#visuals-what-a-widget-draws).
- **Focus asked for too early arrives on the next frame.** `SetFocus` and `SetKeyboardFocus` on a widget shown that frame,
  not laid out yet, answered no and that was the end of it; the focus is now given on the next frame, unless that player's
  focus moves elsewhere first.
- **A tilt card tilts on top of the card's own rotation and scale** instead of writing over them: a fan of cards each turned
  a little keeps its fan.
- **`RenderOpacity` can be keyed** on a `.dui` timeline and in the animation editor (DUI5016 refused it).
- **A small `.dui` widget opens on its own size.** A class whose root has a size of its own, and whose designer canvas nobody
  picked, has its canvas follow the root's size; the class's design size goes with it, which is the draw size a world
  widget takes with *Use Design Size* on.
- **An automatic `.dui` recompile reports in the designer.** Its errors go into the open designer's Compiler Results panel,
  which opens for them, and its notification shows in that window rather than in the corner of the screen.
- **Designer clicks look through silent full-screen layers.** A widget that is no raycast target and draws nothing now -- a
  full-screen pixel sort at strength 0 -- is skipped when something else is under the cursor.
- **Two warnings for things that drew wrong with no word.** A post-process visual (a background blur) inside a group drawn
  apart reads the group, not the scene, and comes out flat; an override material that does not shade through
  `MF_DreamUI_Shade` is not clipped, masked or faded by DreamGUI. Each is now logged once.
- **A ring menu's `WedgeTemplateClass` content is centred on its slice.** Each wedge's instance of the template used to fill
  the wedge's whole ring-sized square, so a template had to draw itself in the right corner of that square. It is now
  placed as the built-in icon and label box is: centred on the slice's middle line at the style's `ContentRadius` (midway
  between the inner and outer radius when that is 0), at the size its class's root is authored at when the root has fixed
  anchors, or `ContentWidth` square when the root stretches, and turned as `LabelFacing` says. The built-in box steps
  aside, inactive, while a template is set, and changing `WedgeTemplateClass` rebuilds the wedges. A template written
  against the old placement needs its root's anchors and size looking at. See
  [DreamRingMenu](Docs/Reference/DreamRingMenu.md).

### Fixed

- **A text's shadow goes under every letter of it.** The underlay was drawn with each glyph's glow and outline, so where
  letters overlap -- tight or negative letter spacing, italic, a heavy condensed face -- the next letter's shadow landed on
  this one's outline. Every glyph's underlay now draws first, then every glow and outline, then every face; a text with
  an underlay and an outline or a glow draws three quads a glyph.
- **A font whose glyphs overlap their own strokes says so.** A static instance cut from a variable font keeps its strokes
  as separate contours that run into each other, and the distance field shows a gap or a seam where they cross -- a
  silent fault until now. Every glyph's overlap is measured as it is rasterized, and the font warns once, naming the glyph
  and its file and how to take the overlaps out of the file with fontTools' `removeOverlaps`. A font the engine ships
  says so in the log alone.
- **Lines added to a node written on one line stay in its block.** Two properties written into a block that opens and
  closes on one line (`Text Title { Text = "Hi" }`) in one write had the whole write refused, and a `+` line added to
  such a node was written after its brace -- a behaviour of the parent. Both now land inside the block.
- **A built-in element in Multiply is exact when it is partly transparent.** Its transparent pixels darkened what was behind
  them to black; it now darkens by its colour as far as its alpha says, as the composite does.
- **A clip whose pivot is off its centre stays on its widget under a scaled or turned parent.** Its centre was moved by
  the pivot's offset in world units rather than through the clipper's own scale and rotation, so under a canvas scale or
  a rotated parent the drawn clip rect slid off the rect the hit test used. Both now place it through the clipper's
  transform.

- **A text field's edit menu answers the pointer.** No entry -- Copy, Paste and the rest -- could be chosen with a mouse
  or a finger. The menu and the sheet behind it were built but never brought to life, so neither took its place above
  the screen and the sheet, built last, lay in front of the entries and took every press; and a press that left the
  field ended the edit and took the menu down under it. The menu is registered as it is built, the field keeps its edit
  while its menu is up, as a Slate text field counts as focused while its context menu is open, and the focus comes
  back to the field when the menu closes -- after an entry, or after a press outside the menu, which now leaves the
  field being edited where it used to end the edit.
- **The focus ring shows on a button.** The ring the keys and the pad bring is hung under the control it marks, and a
  control whose face takes one child -- every Dream Button, whose face is a size box over its content -- refused it: the
  ring stayed where it was made, under nothing, and was drawn nowhere. It now hangs under the nearest widget that takes
  it, placed and sized over the control, and moves with the control when the two move together, as in a scroll box.
- **A drag that is not a drag and drop no longer drops on a list row.** A finger that scrolled a list of droppable rows
  and lifted over one of them reported `OnItemAcceptDrop` for that row, with no operation; only a drag that carries an
  operation drops now, as only a drag-drop event reaches Slate's `OnDrop`.
- **A menu opened again partway through its fade-in fades in from clear.** A Dream Menu Anchor closed during its fade
  and opened again went on with the old fade, so the second open came in from nearly opaque. Every open now starts its
  fade from transparent, as every push of a Slate menu is a new window that starts transparent.
- **Creating a widget of an abstract class logs an error** instead of stopping on NewObject's assert. A class picked
  from a list cannot be abstract, but a class pin or a Create Dream Widget Of Class call fed from a variable can be.
- **A DreamUI Widget Blueprint's thumbnail is the screen it authors.** The Content Browser tile was a wireframe of the
  authored anchor rects, which knows nothing of what a layout arranges, so most screens came out as a grey box with a
  line or two on it. The thumbnail now builds the widget the way the designer's preview does, lays it out on the design
  canvas and draws it, as UMG draws its widget thumbnails; the wireframe is kept for a Blueprint with no compiled class.
  It is drawn when an asset has no clean thumbnail and when it is saved, not on every frame the pointer rests on it, so
  a large screen does not stall the browser. A thumbnail already saved in an asset stays until the asset is next
  changed and saved.
- **A press is let go of when its widget moves.** A button held down and then moved under another parent, taken off
  its parent or destroyed kept the press: the release clicked it, the pointer never having left it, and a slider moved
  to another parent partway through a drag went on following the pointer. The press now ends on the next frame, as a
  Slate mouse capture ends once the path it was taken along no longer leads to its widget: the widget's release and the
  end of its drag go out then (a slider's capture end among them), the button's own release later clicks nothing, and
  the rest of the drag moves nothing. A drag and drop under way is left to run, as Slate's is once begun. A widget
  destroyed while held is no longer left behind as the pointer's press after the release.
- **On a split screen, a widget knows which player it is for in a world with no game viewport.**
  `UDreamWidget::GetLocalPlayerIndexOf` asked the local player for its game instance, which a local player only answers
  through its viewport client; one added where there is no game viewport answered nothing, so every player was player
  0, and Add to Player Screen put a second player's widget on the first player's screen. It now asks the controller's
  world, where the event system looks the players up.
- **Undo puts a widget's size back where no layout runs.** Undoing a resize restored the widget's anchors and size
  delta, but the width and height worked out from them stayed as they were: the asset's widget went on answering
  `GetWidth` and `GetHeight` with the size from before the undo.
- **F and Zoom to Fit fill the designer's view.** Both framed the box's bounding sphere with the editor's ortho zoom,
  which comes out five hundred pixels across whatever the view's size (`r.Editor.AlignedOrthoZoom`, on by default), so
  a fitted canvas took a third of a 1280-pixel view and less of a larger one. The flat view now zooms so the box, with a
  hundred pixels to spare each way, is as large as the view holds it, as UMG's Zoom to Fit does; the 3D view frames as
  before.
- **On a split screen, a widget's popups open on the screen it is on.** A dropdown's list, a tooltip's bubble and the
  rest of what the popup layer lifts went to the first player's screen for a widget on any other player's: the screen
  UI took the nearest user widget's owning player as the answer before the screen the widget was on, and a user widget
  with no owner named answers with the first local player. It now counts only an owner named with `SetOwningPlayer`,
  and otherwise goes on up to the screen.
- **A palette row dropped on the designer's viewport by hand lands under the pointer.** The drop was measured in the
  frame of the whole viewport widget, toolbar included, rather than of the scene under it, so it landed below the pointer
  by the toolbar's share of the height -- thirty pixels in a 720-pixel view. The drag preview and a hierarchy row or an
  asset dragged onto the viewport were off the same way.
- **A widget moved out of a tree while the tree is destroyed works where it lands.** A focus ring the focus took back
  from a closing dialog -- and any widget a behaviour moves out while its tree comes down -- was ended and unregistered
  with the tree and then left so under its new, live parent: drawn by that canvas with no place in its data, logging
  `WidgetPropertyDataStartPosition is invalid`. It is registered again where it went, and begins play there if the
  world has, as a UMG widget moved into a panel on screen is constructed there. One moved under a parent that is not
  registered yet stays as it was, for that parent's registration to take with it.
- **Ctrl+C copies the designer's selection.** The designer's Copy, Cut, Paste, Duplicate and Delete were mapped on the
  asset editor's own command list, and the Blueprint editor's Find Results panel maps Copy and Select All onto that list
  as it is made, after the designer has: Ctrl+C in the viewport or the hierarchy, and Copy in their right-click menu,
  copied a search result and left the designer's clipboard as it was, so the next Ctrl+V pasted nothing or something
  older. The five are now on a list of the designer's own as well, which the viewport and the hierarchy answer keys
  from first and their menus run, as UMG's designer keeps them.
- **A drag in the designer follows the pointer while a notification fades in.** The designer applies a held press, a
  drag and a marquee in its tick, and while Slate holds back expensive work -- a toast fading in at a low frame rate
  does, the editor's Undo toast among them -- the editor ticks only the viewports that asked to be redrawn. A drag begun
  just after an undo stood still, and one let go before the toast was in had never begun, as though it had been a
  click. The viewport now asks for its redraw while a gesture is live, as the engine's own viewport drags never wait.
- **Fifty-four findings of a review: callbacks that reenter or tear down a control no longer overwrite what replaced
  them.** PR [#16], each with a test that fails without its fix.
  - *Lists and tabs*: clearing a recyclable scroll view's source no longer leaves a scroll callback to crash ([#2]);
    a list callback that rebuilds the list no longer breaks the cell pool ([#3]) or brings back removed selections
    ([#15]); a tree provider cannot overwrite the refresh that replaced its own ([#19]); a dropdown stops making
    rows once it is closed or destroyed ([#25]); a tab callback destroys the page it closed and no other, and no
    obsolete tab is appended ([#29]); disabling a later tab leaves earlier ones disabled ([#36]).
  - *Menus, dialogs and tooltips*: a menu whose content provider cancels the opening does not stay open ([#24]), and
    a disabled or destroyed anchor cannot reopen or orphan its popup ([#35], [#41]); an old dialog click cannot
    close a newer question ([#43]); `CloseAllModals` closes the dialogs it found and keeps those opened meanwhile
    ([#51]); a tooltip's destruction callback cannot orphan the bubble that replaced it ([#52]).
  - *Input and focus*: the wheel leaves navigation mode and scrolls what is under the pointer ([#10]); hover on a
    shared drop target is per drag ([#11]), and a drag visual's initialization cannot lose the drag it follows
    ([#14]); focus callbacks get current focus events and keep the destination they chose ([#22], [#37]); a key
    selector destroyed or disabled in a callback stops capturing ([#30], [#34]); one text commit has one payload
    ([#42]).
  - *Screens, tweens and animations*: screen page callbacks cannot invalidate the pages being shown ([#5]); a frame
    delay tween can restart from its completion ([#13]); a sequence refuses to contain itself instead of recursing
    until the stack overflows ([#17]); superseded spring and explicit tween steps stop ([#18], [#23]); an
    animation stopped twice in one batch is retired once ([#28]); standalone animations resolve their bindings and
    are found on every component ([#47], [#48]); sprite players follow a replaced visual ([#53]).
  - *Bindings, instancing and copies*: authored event entries ([#4]), event targets after a component move or
    removal ([#12]), references inside hashed map keys and set elements ([#38]), inline tween events ([#39]) and
    bindings after initialization reorders components ([#50]) all keep their targets; a nested widget's private
    children no longer overwrite its host's variables ([#44]); an external component reference takes its type from
    the referenced class ([#49]).
  - *Designer and editor*: a DUI write-back that failed is retried before compiling ([#6]), and an imported style
    change recompiles open documents ([#20]); composite property edits, Copy/Cut/Paste and component commands write
    the authored widgets, not the preview ([#26], [#40], [#46]); source-file class aliases survive saving and
    unrelated lookups ([#31]); Undo restores resource subscriptions ([#32]) and both atlas membership lists
    ([#45]); the designer test helper compiles outside its unity group ([#33]), and the F-key framing test waits
    for the camera's last tick ([#56]).
  - *Rendering*: shared viewport UI is drawn once across a split screen instead of scaled into each view ([#7]); a
    runtime atlas no longer copies incompatible compressed texture formats ([#8]); a reloaded fallback font drops the
    primary's stale face caches ([#21]); world-space UI samples depth from its own view rect ([#27]); cylinder ray
    casts compare hits in one coordinate frame and still find hits behind a culled back face ([#54], [#55]).
- **A user widget no longer records every widget of its tree to keep its component bindings on target.** The fix for
  [#50] captured each widget and all its components whenever an instance was created -- each cell of a list too --
  though only a binding to a component reads them. Only the widgets such a binding names are captured now; a tree with
  none records nothing.
- **The test runner sees an open editor where CIM is unavailable.** It asked Win32_Process for editors with the project
  on their command line, and where `Get-CimInstance` cannot be loaded it assumed there were none and built over an open
  editor (LNK1104); the nightly and the bench launcher stopped on the error, and matched any project of the same file
  name. All three now fall back to editors holding a DLL from inside the project. See
  [Running the DreamGUI tests](Tools/Tests/README.md).
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

[#16]: https://github.com/TypeDreamMoon/DreamGUI/pull/16
[#2]: https://github.com/TypeDreamMoon/DreamGUI/issues/2
[#3]: https://github.com/TypeDreamMoon/DreamGUI/issues/3
[#4]: https://github.com/TypeDreamMoon/DreamGUI/issues/4
[#5]: https://github.com/TypeDreamMoon/DreamGUI/issues/5
[#6]: https://github.com/TypeDreamMoon/DreamGUI/issues/6
[#7]: https://github.com/TypeDreamMoon/DreamGUI/issues/7
[#8]: https://github.com/TypeDreamMoon/DreamGUI/issues/8
[#9]: https://github.com/TypeDreamMoon/DreamGUI/issues/9
[#10]: https://github.com/TypeDreamMoon/DreamGUI/issues/10
[#11]: https://github.com/TypeDreamMoon/DreamGUI/issues/11
[#12]: https://github.com/TypeDreamMoon/DreamGUI/issues/12
[#13]: https://github.com/TypeDreamMoon/DreamGUI/issues/13
[#14]: https://github.com/TypeDreamMoon/DreamGUI/issues/14
[#15]: https://github.com/TypeDreamMoon/DreamGUI/issues/15
[#17]: https://github.com/TypeDreamMoon/DreamGUI/issues/17
[#18]: https://github.com/TypeDreamMoon/DreamGUI/issues/18
[#19]: https://github.com/TypeDreamMoon/DreamGUI/issues/19
[#20]: https://github.com/TypeDreamMoon/DreamGUI/issues/20
[#21]: https://github.com/TypeDreamMoon/DreamGUI/issues/21
[#22]: https://github.com/TypeDreamMoon/DreamGUI/issues/22
[#23]: https://github.com/TypeDreamMoon/DreamGUI/issues/23
[#24]: https://github.com/TypeDreamMoon/DreamGUI/issues/24
[#25]: https://github.com/TypeDreamMoon/DreamGUI/issues/25
[#26]: https://github.com/TypeDreamMoon/DreamGUI/issues/26
[#27]: https://github.com/TypeDreamMoon/DreamGUI/issues/27
[#28]: https://github.com/TypeDreamMoon/DreamGUI/issues/28
[#29]: https://github.com/TypeDreamMoon/DreamGUI/issues/29
[#30]: https://github.com/TypeDreamMoon/DreamGUI/issues/30
[#31]: https://github.com/TypeDreamMoon/DreamGUI/issues/31
[#32]: https://github.com/TypeDreamMoon/DreamGUI/issues/32
[#33]: https://github.com/TypeDreamMoon/DreamGUI/issues/33
[#34]: https://github.com/TypeDreamMoon/DreamGUI/issues/34
[#35]: https://github.com/TypeDreamMoon/DreamGUI/issues/35
[#36]: https://github.com/TypeDreamMoon/DreamGUI/issues/36
[#37]: https://github.com/TypeDreamMoon/DreamGUI/issues/37
[#38]: https://github.com/TypeDreamMoon/DreamGUI/issues/38
[#39]: https://github.com/TypeDreamMoon/DreamGUI/issues/39
[#40]: https://github.com/TypeDreamMoon/DreamGUI/issues/40
[#41]: https://github.com/TypeDreamMoon/DreamGUI/issues/41
[#42]: https://github.com/TypeDreamMoon/DreamGUI/issues/42
[#43]: https://github.com/TypeDreamMoon/DreamGUI/issues/43
[#44]: https://github.com/TypeDreamMoon/DreamGUI/issues/44
[#45]: https://github.com/TypeDreamMoon/DreamGUI/issues/45
[#46]: https://github.com/TypeDreamMoon/DreamGUI/issues/46
[#47]: https://github.com/TypeDreamMoon/DreamGUI/issues/47
[#48]: https://github.com/TypeDreamMoon/DreamGUI/issues/48
[#49]: https://github.com/TypeDreamMoon/DreamGUI/issues/49
[#50]: https://github.com/TypeDreamMoon/DreamGUI/issues/50
[#51]: https://github.com/TypeDreamMoon/DreamGUI/issues/51
[#52]: https://github.com/TypeDreamMoon/DreamGUI/issues/52
[#53]: https://github.com/TypeDreamMoon/DreamGUI/issues/53
[#54]: https://github.com/TypeDreamMoon/DreamGUI/issues/54
[#55]: https://github.com/TypeDreamMoon/DreamGUI/issues/55
[#56]: https://github.com/TypeDreamMoon/DreamGUI/issues/56

- **A pointer light shows in the designer.** Following the pointer, it lit nothing in a world that is not playing, where
  there is no pointer; there it now stands at its `LightPosition`.
- **Rows missing from the details panels.** A user widget's *Wants Tick*, *Allow Event Bubble Up* and *Can Navigate
  Here*; a widget's tooltip widget class; an image brush's fill (amount, method, origin, direction) and its rounded box's
  corners; a rect block's nine-slice draw mode and margins and its shape blend operation; a sprite's padding and border
  before it has a texture. Each could only be written in a `.dui`.
- **A sequence player's details no longer crash the editor.** The error row of `UUISpriteSequencePlayer` and
  `UUISpriteSheetTexturePlayer` looked for the visual they need among the widget's components and read its name off
  nothing.
- **Advanced properties of a sub-category have rows.** The engine draws a sub-category (`DreamGUI|Pointer Light`)
  without its advanced rows, with "show all advanced" on as well, so a holo foil's, a pointer light's and a tilt card's
  *User Index* and *Pointer Id* were in no panel. DreamGUI's panels give such a property an ordinary row of its
  sub-category, after the others.

- **A builder error is no longer followed by DUI6002.** The code that says a file produced no hierarchy was reported with
  every error the builder gave, pinned to the root line, where nothing is wrong. It is now reported only when no other
  error says why.
- **An expandable area in a vertical box or a scroll box shrinks and grows with its travel.** The panel around it measured
  it by its header and its whole content, so a closing area showed its whole body until the close ended, and then
  vanished; an opening one was whole at once. Its `MaxHeight` reaches the panel too now.
- **The diagnostics mailbox forgets a file no class is built from.** A Blueprint taken off its `.dui`, or a `.dui` deleted
  or renamed, left its entries in `DUI/.dui-diagnostics.json` for the editor extension to show.
- **The workspace file the editor writes recommends the extension under its current id.** `DUI/DreamUI.code-workspace`
  still recommended `typedreammoon.dreamui-language-support`, the id the 0.9.2 rename left behind, so VS Code offered the
  old extension. It recommends `typedreammoon.dreamgui-language-support` and lists the old id under
  `unwantedRecommendations`; and when the editor opens the workspace in VS Code it warns, in a notification and the log,
  if `~/.vscode/extensions` holds a copy under the old id. The extension's next release reads the new syntax above.

## 1.0.0

Everything of the 2.1.0 development build, and the `.dui` language grown into what screens are written in: components
written in `.dui` alone, `use … as`, `if`, `for`, `rows`, shorter layout; the reference for all of it; and the fixes the
first two showcase screens found. Projects on 2.1.0 open unchanged ([Docs/guides/migration.md](Docs/guides/migration.md#from-21-to-100)),
except for assets that still loaded through a redirect: the plugin ships none any more.

### Removed

- **The CoreRedirects.** `Config/DefaultDreamGUI.ini` and its 763 redirects -- from LGUI and LexUI, from the prefab
  vocabulary, from the control and event renames and from the module split -- are gone, and the engine no longer
  applies any of them to a project that mounts the plugin. The plugin's own assets and the test fixtures name the
  current types. An asset saved against an old name is resaved once with 2.1.0's block borrowed into the project's
  `Config/DefaultEngine.ini` ([Docs/guides/migration.md](Docs/guides/migration.md#from-21-to-100)). The four
  `DreamGUI.Packaging` tests that held the redirect file went with it, and
  `DreamGUI.Packaging.ThePluginShipsNoCoreRedirects` keeps a `[CoreRedirects]` section from coming back unnoticed.
- **The old-asset fixtures saved before the module split.** They loaded only through the redirects; the test host's
  `DreamGUIFixtures` are saved by 1.0.0 now, the baseline later versions keep loading.
- **`Tools/ModuleSplit/generate_split_redirects.py`**, which appended a split's redirects to that file. It is
  `moved_types.py` now: it lists the types a split moved and, with `--redirects`, prints a block for a project's
  own config, and writes nothing into the plugin's. `retarget_script_paths.py` takes the names from it.

### New

- **`rows`: a table of instances in `.dui`.** `rows Row : ListRow (Label, Description) { "City Ruins", "…" … }`
  writes the type, the style and the property names once, then one line per instance -- the same component N times
  differing in a few values, which a settings page or a list of menu entries is made of. Read into the ordinary
  unnamed children the lines stand for, so nothing downstream changed; each row is named from its first value
  (`Page_0__Row_City_Ruins`), so inserting or reordering rows moves no other row's id or localization keys. A line may
  end in a block for what that row needs beyond the columns. The designer writes a column's value back into its cell
  and refuses, with the reason, what a row's line cannot spell (DUI7004). New codes DUI2020 MalformedRows and DUI3023
  DuplicateRowKey (a warning). See Docs/DuiLanguage.md, "`rows`".
- **Components by a short name in `.dui`.** `@Row Row1 { }` is a node whose type the `Asset` entry `Row` of a
  `resources` block names -- this file's, or one a `use` brought in -- so a family of components is named once in the
  library that styles it and each screen writes `@Row` instead of the asset path on every line. An entry that is
  missing, is not an `Asset`, or names no user widget is reported as such.
- **`use … as` in `.dui`.** `use "Components/Row.dui" as Row` names the class a component file compiles into -- its
  `class` line, or the Blueprint whose Source File it is -- and `use /Game/UI/WBP_Row as Row` a class with no `.dui`;
  `Row Row1 { }` is then an instance. `use "Lib.dui" as nier` on a library enters its styles, resources and component
  names under `nier.` (`: nier.Label`, `@nier.Ink`, `nier.Row`), so two libraries can be used side by side, and the
  `use … as` lines of a library travel with it. `@Row` keeps working.
- **Components written in `.dui` alone.** A file declares what its class offers its hosts: `props { Text Label }` (Blueprint
  variables, with defaults, which a host sets or binds -- `Label <- GetName()` -- though they have no setter; a C++
  parent's property of the same name is used), `events { Picked(Number Index) }` (event
  dispatchers, raised with `OnClick -> emit Picked(Index)` and routed by a host like any event), and slots that carry a
  layout and a style and are marked `default` -- the slot nested content goes to, in place of a `GetDefaultSlotName`
  override. A host fills a named slot with `slot Detail { … }` inside the instance.
- **Shorter layout in `.dui`.** A layout container is a node type (`VerticalBox Column { Spacing = 29 }`), a node nobody
  refers to needs no id (`HorizontalBox { … }`), slot lines group into `@slot { … }` and `@fill` / `@fill 2` stand for the
  two written most, and a style can carry `+ Component` blocks and slot lines, so a kind of column is one name.
- **`if` and `for` in `.dui`.** `if Cond { } else if … { } else { }` shows one branch's widgets at a time (each gets a
  `Shown` bound to its branch; a hidden branch keeps its state), `Shown <- Expr` is visibility as a yes or no on every
  widget (`UDreamWidget::Shown`), and `for Item in Items { … }` makes one copy of its template per item inside any panel,
  where `each` keeps filling a virtualized list view.
- **The language reference.** [Docs/language/index.md](Docs/language/index.md) describes the whole of `.dui` -- file structure,
  types, values, styles, `use`, components, bindings, the control statements, timelines -- with a table of every
  diagnostic, and the editor's symbol dump (`DUI/.dui-symbols.json`) lists the keywords, the container types and their
  properties for an editor's completion.
- **The designer writes the new syntax back.** An edit lands on the line that holds the value whatever the node's type is
  written as, inside a `@slot { … }` block when there is one, on a node with no id by the id made for it; a node made in
  the designer is written by the file's alias for its class, or as its container; content dropped into a component's
  named slot is written into that slot's fill. What no line spells is said rather than written: a visibility an `if`
  decides and a shorthand that cannot take the change are DUI7004, a value from a style's component DUI7005.
- **Render-transform tweens.** `UDreamWidget::RenderTranslationTo`, `RenderOffsetTo` (on the canvas plane, depth
  kept), `RenderScaleTo` and `RenderAngleTo` move, scale and turn what is drawn and never the layout: the tweens for a
  widget a panel places, whose anchored position and size the panel writes back on its next pass.
- **A control can refuse the focus ring.** `UUISelectable::bUseFocusRing` (on by default; `SetUseFocusRing` at run
  time) turns the project's ring off for a control whose own look marks its focus. The focus moving onto such a
  control takes the ring away instead of leaving it on the control it came from, and a screen whose controls all
  refuse it never makes one. An unset `NavigationSelectionClass` now means no ring anywhere, without a warning at
  every focus change.
- **A cap on how far one frame moves the tweens.** `DreamTween.MaxStepSeconds` (console variable, 0 = off, the
  default): a frame longer than it -- a screen loading its assets, the first draw of new text -- moves the tweens the
  world ticks by the cap only, so an entrance started just before such a hitch plays on from where it was instead of
  appearing at its end. Manual ticks are never capped.

### Fixed

- **The focus ring fits the control it marks.** The plugin's ring drew its frame on a child authored at a fixed
  100x100 on the centre of the ring's root, so the frame stayed 100x100 over a full-width row and a small icon alike
  while only the invisible root took the control's size. A ring class with no Blueprint handler of its own now has
  each child that covers its root (centre-anchored, at least the root's size) anchored to stretch with the root, its
  authored margin kept; smaller or corner-anchored children keep their own size.
- **A ring that has just appeared no longer stays transparent.** Moving the ring killed its running tweens, the
  fade-in of a ring shown a moment before among them, so a second key press inside a quarter second -- or the same
  focus arriving as a select and then a navigation enter -- left the ring at the opacity the fade had reached. Moving
  now finishes the fade.
- **Focus moved by code leaves the control it came from.** `SetFocus` moves the navigation cursor with the focus, but
  the control it left heard no exit until the next navigation step, so it went on drawing itself Focused -- and
  answering Focused to `GetCurrentSelectionState` -- beside the control that had the focus, through a screen opening a
  sub-list or a dialog and through being hidden and shown again. A deselect now ends the navigation's hold on it.
- **What the built-in shader draws after an image through a material is drawn by the built-in shader again.** On a
  screen or render-target canvas, a draw through a material bound the material's pipeline but left the pass's cache
  of the built-in pipeline saying it was still bound, so the next text or block drew through the material's shaders:
  text after a material-drawn image went blank or turned to boxes, and the RHI ensured on parameters set for a vertex
  shader that was not bound. World-space canvases already set the built-in pipeline again; screen ones now do too.
- **A brush material whose only parameter of the canvas's is the renderer flag is told the renderer.** The canvas
  asked a material for its texture parameters only, so `DreamUI_IsRenderByDreamUIRenderer`, a scalar, never counted:
  a procedural material that premultiplies by it was drawn as it was, the flag at its default, and premultiplied a
  second time by DreamGUI's renderer.
- **A material instance given to an image shows the parameters its owner sets on it.** The canvas draws a material
  through a proxy of its own that caches the material's uniform expressions, and made that cache again only when
  DreamGUI's own parameters changed: a `SetScalarParameterValue` on the instance after its first draw never reached the
  screen, so an animated material stood still. The proxy now makes its cache again whenever the instance's own render
  proxy has, as the draw is gathered.

## 2.1.0 (development build)

Five passes of engineering after 2.0: the frame made cheap enough for thousands of animated widgets, every control and
editor path audited, text measured against Chrome and Slate and rebuilt where it differed, gradient text, keyboard and
gamepad navigation, and what it takes to ship. `DreamGUI.uplugin` is version 3. Assets saved by 2.1 cannot be opened by
2.0.

### New

- **Gradient text.** A text's face, its outline and an overlay over its face can each be painted with a gradient:
  linear, radial, conic, diamond or four-corner, with up to 16 stops, pad, repeat or reflect, mixed in sRGB, linear light
  or Oklab, measured across the text block, the content box, each line, each glyph or each rich-text run. Gradients are
  written as CSS writes them (`linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`), in the details panel, in a
  `.dui`, or as a rich-text run (`<gradient=Name>...</gradient>`, the name a custom style, a project preset or the CSS
  itself). Presets are `UDreamGradientAsset` assets or CSS strings in the project settings (`GradientPresets`); texts
  that paint with the same gradient share its row on the GPU. A paint's phase, angle, centre and scale animate without
  laying the text out or painting it again -- keyable in Sequencer (`FacePaintPhase`, `OutlinePaintPhase`,
  `OverlayPaintPhase`, `PaintAngleOffset`) and tweened by `UDreamTextPaintLibrary` (`PaintPhaseTo`, `PaintAngleTo`,
  `PlayShimmer`). A material that does not shade through `MF_DreamUI_Shade` paints by vertex colour instead.
- **Gradients in the editor.** A gradient property shows a strip of the gradient whose stops can be dragged, a CSS box
  with Copy and Paste, and a presets menu that lists the project's presets and gradient assets and saves the gradient as
  either. A gradient asset has a thumbnail and *Copy as CSS*. A text's paint fields are one Paint group in its details.
- **Tab navigation.** Tab and Shift+Tab, with no Ctrl, Alt or Cmd held, move the focus in hierarchy order, siblings
  sorted by the new `TabIndex`; `bIsTabStop` and `TabNavigation` (Continue, Cycle, Contained, Once, None) shape the walk;
  lists, tiles and trees are one stop entered at their selected row; a stop scrolled out of view is scrolled into it. A
  modal, a dimmed dialog and a popup that cycles keep Tab inside them, and another player's popups are never stops. Tab
  commits a text field's edit and moves on, and arriving by Tab starts editing; a dropdown commits its highlighted row
  on Tab and a menu closes its chain. Slate's own navigation from the bare game viewport -- Tab, the arrows -- never
  moves the focus into UMG while the player has a DreamGUI focus or Tab stop. `TabOrder = LegacyGeometric` keeps 2.0's
  geometric order for this release.
- **Gamepad and input modes.** A navigation scope says whether its screen takes menu or game input (`InputMode`: All,
  Menu, Game; Game turns built-in navigation, confirm and back off for that player). Keys and the pad act on the focus:
  Enter, Space and the pad's accept press the focused widget, never the hovered one, and nothing when nothing is
  focused. The focus look shows only when keys or a pad moved the focus, as CSS `:focus-visible`
  (`bFocusVisibleOnlyFromKeys`), and in DreamGUI's UI-only input mode the cursor hides while a pad is in use
  (`bHideCursorOnGamepad`). The confirm, back, direction, page, extent and tab-switch keys are project settings
  (`ConfirmKeys`, `BackKeys`, `DirectionKeys`, `PageKeys`, `ExtentKeys`, `PreviousTabKeys`, `NextTabKeys`), read again
  when they change; the platform's accept and back buttons are read at run time (`bUsePlatformAcceptBack`); the triggers
  page, and the shoulder buttons switch the active tab view's tabs, with prompts in the action bar. Split screen:
  scopes, action triggers and the action bar belong to their widget's owning player unless given an index.
- **Text**, measured against Chrome and Slate and brought to them: grapheme clusters and bidi levels with ligatures and
  CSS line boxes; fallback faces chosen by Unicode range, culture and presentation, with real bold and italic faces;
  colour emoji (CBDT/CBLC, sbix, COLRv0), whose shadow, outline and long-shadow copies are silhouettes; justification,
  tab stops and a middle ellipsis; long texts laid out again only where an edit touched them; line breaking by the
  game's culture; a rich-text `<lang=xx>` tag; images in rich text sized and aligned.
- **Small text** -- up to `SmallTextMaxPixelSize`, 20 px by default -- is drawn from hinted coverage glyphs placed on the
  device pixel grid, crisp as Slate's. Text with an outline, a glow or an underlay draws its face that way and its
  effects from the field (`SmallTextEffectFace`), and text in a render layer does once the layer has held still for 3
  frames. A font keeps the atlas cells of its coverage glyphs while any text draws from them; past twice
  `MaxCoverageCells` new ones wait, drawn from the field meanwhile, with one warning per font.
- **The default font** is Slate's: Roboto in real regular, bold, italic and bold-italic faces, with DroidSansFallback for
  CJK, on the outline (multi-channel) distance field.
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
  prints it as JSON, `File=<path>` writes it.
- **A check on request**: `r.DreamUI.VerifyKeptPointers` looks up again every object DreamGUI keeps between frames -- a
  widget's canvas, a canvas's render layers, the UI manager's canvases, an animated property's object -- and says where
  the two disagree, as `r.DreamUI.VerifyPartialPrepare` does for a canvas's prepare.
- **Switches to measure with.** `DreamGUI.Text.SmallTextCoverage` and `DreamGUI.Text.SmallTextMaxPixelSize` override the
  small-text settings for every font, and a change repaints every text. `DreamGUI.Text.SmallTextOnMove`,
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
- A canvas walks only the widgets that asked to change, patches in place the sections whose geometry changed, and
  keeps its draw calls when its widgets only moved.
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
  translucent or fading text darkened, a gradient's transparent stop greyed, glows fell off too fast and light text's
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
- **Extensions**: TextAnimation's properties pass over a character that has no vertices instead of reading past them; a
  cylinder render-target surface whose canvas has no target or that has no vertices, a static mesh set after its widget
  left its canvas, a 2D line whose children changed in number, and render-target input sent to a canvas that was
  replaced no longer crash or misdirect; a cylinder's arc of 0 is 1 degree, and a negative arc's bounds are on the side
  it bends to. The retainer box retains: it sets its canvas to render-target mode, puts it back after, leaves editor
  worlds alone and warns once when it has no `DisplayVisual`. The static mesh visual refuses a mesh past the vertex
  budget, with an error, instead of truncating its indices, and its bounds are where the canvas draws it. The
  render-target surface (`UDreamUIRenderTargetGeometrySource`) in StaticMesh mode puts its material into the static
  mesh's slot 0 only in play: in the editor the transient instance it put there made saving the level write null over
  the mesh's own material. Pixelate hands its target to its proxy; pixel sort keeps its pass count within 1 to 512; the
  lyrics' TTML reader stops at 256 levels of nesting; TextAnimation properties set at run time start, and the ones they
  replace stop.
- **DreamTween**: sequences survive children the collector took; a callback that restarts or seeks its own sequence no
  longer loops, and a seek nested 16 deep is refused with an error; `Kill` fires its callbacks once; finished and killed
  tweens retire and their handles read invalid -- `Kill`, `ForceComplete` and `Goto` do nothing on a killed or retired
  tween, `Restart` on a retired one only warns, and `Restart` from a tween's own `OnComplete` keeps it running;
  `ForceComplete` on a tween that has not started starts it first; `Goto` to the end completes once and `Goto(0)`
  applies the start value; an empty curve falls back to linear with a warning instead of freezing; colour and alpha
  tweens round and clamp rather than wrap on overshooting eases; InExpo ends exactly at 1; a backward yoyo cycle ends at
  0; a spring at rest wakes on `SetTarget`; `RepeatCall` with a count of 0 calls nothing; time scale, delay and delta
  are checked; a second tick helper no longer doubles tween speed; a tween whose owner is gone is dropped without
  callbacks; layout animations and the embedded UMG widget follow the pause and time-dilation rule other widget tweens
  follow.
- **Controls**, about forty findings of an audit, each with a test: list and scroll view rows during item changes, a
  heap write before a text field's buffer, a dropdown holding a raw pointer, buttons and selections switched off while
  pressed, drags whose source is destroyed, the key selector, the text field editing by UTF-16 offset with the IME.
- **Text**: packed text-style values that made a denormal float -- a zero first value of a pair, such as face softness 0
  beside a dilation, or an effect alpha of exactly 128 -- are nudged to the nearest normal one, so a GPU that flushes
  denormals no longer turns them to 0; a rich-text tag name too long for an `FName` (1024 characters or more) is shown
  as text instead of asserting.
- **Rendering**: a mesh whose shadow copy would take it past the vertex budget is drawn without the shadow, with one
  warning, rather than not at all.
- **Fonts**: bitmap fonts reload on undo and redo; an embedded font file's bytes are read again at cook, and an empty
  one is a cook error; a font's atlas texture and its list of texts are no longer recorded in undo transactions.
- **Input**: a player index with no player no longer gets an input user made for it.
- **Widget Blueprints run their graphs in a game on uncooked content** (`-game`, Standalone): `DreamGUIEditor` is
  UncookedOnly.
- **The editor**: a designer gesture is one undo step; the designer writes its edits to the `.dui` the asset names, and
  never over a file changed on disk; a recompile, and the designer's own rebuild of its preview, no longer record into
  an open transaction.

### Platforms and servers

- Only Win64 has been built and run: the editor and the automation suite, and a packaged Development game once the
  release gate has run the test host's packaged text smoke test. Mac, Linux, iOS and Android are listed by the
  descriptor but have never been compiled (the README's Platforms section says so instead of "Builds: yes").
- The UI manager no longer starts in a dedicated server's worlds (a play-in-editor server included): no canvases, no
  renderer, no paint rows.

### Documentation and tools

- [Docs/guides/fonts-and-packaging.md](Docs/guides/fonts-and-packaging.md): colour emoji fonts, three ways to package a font file, the ICU
  packaging preset and what degrades without it.
- [Docs/guides/migration.md](Docs/guides/migration.md) has a section for moving from 2.0 to 2.1.
- msdfgen's MIT licence ships with the plugin, beside its source.
- The test host has a packaged text smoke test, part of the release gate: a probe in its game module writes what a
  packaged game draws, and `Tools/Tests/compare_text_smoke.py` holds it to the same build run on uncooked content.
- `Tools/ModuleSplit/check_game_includes.py` also finds engine macros and globals a file uses without including what
  defines them, editor headers in code a game compiles, and runtime `Build.cs` files that depend on editor modules.
- The render benchmark measures small text with coverage on and off under seven kinds of motion; the text parity run
  measures stroke edges, ink in linear light and grey against Slate, and reports gradient fills beside Chrome's
  `background-clip: text`.
- `Tools/Bench` holds the benchmarks and the scripts that read their profiles; the test runner has presets, a nightly
  script and a coverage table of which control is tested with which input.

### Changes to know about

Upgrading from 2.0 changes some defaults and behaviour -- the default font (texts reflow), small-text rendering, Tab,
keys and the pad acting on the focus rather than the hover, the focus look, player indices, scroll bars and lists as Tab
stops, the effect alpha of faded outlined text, how field text composites its layers, MSAA blending, line breaking by the game's culture, and the 64-byte
vertex with its new `UV4` channel for anyone with custom shaders or mesh modifiers. They are listed in
[Docs/Migration.md, "From 2.0 to 2.1"](Docs/guides/migration.md#from-20-to-21).

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
- **Removed**: the root Blueprints, `UDreamWidgetPresenterComponent`, the world-space raycaster source family, the
  settings and console variables that put old renderer behaviour back. See
  [Docs/guides/migration.md](Docs/guides/migration.md#4-what-no-longer-exists).
