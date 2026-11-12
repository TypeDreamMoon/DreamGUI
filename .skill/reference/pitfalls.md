# Common pitfalls

> This page stands on its own: what an author needs is here, and an agent that cannot open its links loses nothing it
> needs. The links lead to the online manual for depth. Look names up (DreamMCP's `lookup`, or
> `DUI/.dui-symbols.json`) instead of guessing them. Built from `Docs/guides/pitfalls.md` by `.skill/build-reference.ps1`.
Each entry says what you see, why, and what to do; the page it links to has the detail.

## Animation

- **A fade, then the opposite fade, and the result flickers or ends wrong.** Two tweens on one property used to both run.
  A tween now stops the earlier ones on the same property when it starts; `SetReplacesEarlierTweens(false)` opts one out
  (a looping breath that should coexist with a hover). Tweens in a sequence are never stopped.
  [One tween per property](https://gui.toolchain.64hz.cn/en/docs/concepts/animation/#one-tween-per-property).
- **A widget in a panel does not stay where `AnchoredPositionTo` puts it.** The panel writes the position back. Move what
  is drawn: `RenderOffsetTo`, `RenderScaleTo`, `RenderRotationTo`, `RenderPoseTo`.
  [Widgets inside panels](https://gui.toolchain.64hz.cn/en/docs/concepts/animation/#widgets-inside-panels-animate-the-render-transform).
- **A positive roll turns clockwise.** `RenderRotation.Roll`, `SetRenderTransformAngle` and `RenderAngleTo` turn a widget
  clockwise on the screen (its right edge down), as UMG's angle does.

## Angles

- **The angle properties do not share a zero.** `Ring` measures from three o'clock, counter-clockwise; `Native.RingMenu`,
  `Native.ProgressBar` and text on a path measure from twelve o'clock, clockwise. A progress ring on `Ring` is
  `StartAngle = 90`, `EndAngle = 90 - 360 * progress`. [Angle conventions](https://gui.toolchain.64hz.cn/en/docs/concepts/widgets/#visuals-what-a-widget-draws).

## Input

- **A label over a button takes no hover or clicks.** Right: a `UDreamText` is not a raycast target by default. A text that
  is the thing to press sets `bRaycastTarget = true`. [Visuals](https://gui.toolchain.64hz.cn/en/docs/concepts/widgets/#visuals-what-a-widget-draws).
- **`bRaycastTarget` on a `Widget` node is an error.** It belongs to a visual. Write `Raycastable = Disabled` on the widget.
  [Properties and values](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#properties-and-values).
- **A button the pad has selected shows nothing.** A selectable's transition wears its Hovered look while it has focus,
  unless *Use Focused Visuals* gives focus a look of its own. A look drawn by your own code from `OnSelectionStateChanged`
  hears `Focused`, not `Hovered`: draw the two alike there.
- **Navigation driven from code takes no step.** A press and its release given on one frame never reach a frame with the
  direction down. Call `TapNavigation`, which takes one step on the next frame; for a recording made frame by frame, turn
  on the event system's *Navigation Repeat In Game Time*, or a held direction repeats on real time and runs ahead.
- **Focus asked for in a widget's construct.** A widget shown that frame takes it on the next frame; `SetKeyboardFocus`
  answers false meanwhile, so do not treat that answer as final.

## The `.dui` language

- **`Style.X = v` changes nothing.** The instance's `Style` is used only when `StyleSource` is `Inline` (or there is no
  style sheet), or `ProjectStyleSheetOverride` with the field's `bOverride_X` ticked. A `.dui` never touches the bits, and
  they start ticked. [In .dui and at run time](https://gui.toolchain.64hz.cn/en/docs/controls/styles/#in-dui-and-at-run-time).
- **`: A : B` applies `B` over `A`, and a name twice is a warning.** Styles apply left to right, bases first, each once, at
  its first appearance; the node's own lines come last. Naming a style that an earlier one already applied changes
  nothing and is DUI3025. [Several styles](https://gui.toolchain.64hz.cn/en/docs/dui/styles-and-resources/#several-styles).
- **`@slot Size <- F()` is an error.** `Size` is a shorthand for `MinDesiredSize` and `MaxDesiredSize` with one value, so it
  takes `=` only; bind the two properties separately (DUI4001).
  [`@slot Size`](https://gui.toolchain.64hz.cn/en/docs/dui/slots-and-components/#slot-size).
- **`@tag Trim` is a tag, `@tag Trim { }` is a node.** A name after `@tag` is a tag line unless a block, a `:` or a
  `(was: …)` follows it, which makes it the old `@Name` form of a node typed by a resource called `tag`. A tag line in a
  `style` or directly in a `for` body is DUI2024. [Tags](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#tags-tag).
- **A ring menu's items are written in the `.dui`.** `Items = [{ "Lamp", Tag: Lamp }, …]`: a map per item, the label
  first and without its key. A map written without a key anywhere else, or on a struct that has no primary field, is
  DUI4009. [Structs as field maps](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#structs-as-field-maps).
- **A ring menu's template content is centred on its slice, not stretched over it.** It takes the size its class's root
  is authored at, or the style's `ContentWidth` square when the root stretches, at `ContentRadius`. Give the root fixed
  anchors for a size of its own. [Ring menu](https://gui.toolchain.64hz.cn/en/docs/reference/dreamringmenu/).
- **`#8C8C94` is darker than it looks.** Colour literals are sRGB-encoded, and a tint is multiplied in linear space.
  [Values](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#values).
- **A positive `AnchoredPosition` Y moves a node up, and `(0, 0)` anchors are the bottom-left.** Y goes up, as in Unity.
  In `RenderTranslation` X is depth: `(-40, 0, 0)` brings a node nearer and draws it larger; `(0, -40, 0)` moves it
  left. `RenderRotation` is `(Pitch, Yaw, Roll)`. [Positions, sizes and axes](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#positions-sizes-and-axes).
- **`VAlign = Center` on a text is an error, and `@slot VerticalAlignment = Middle` is too.** A text's own alignment
  says `Middle` (`Top`, `Middle`, `Bottom`); a slot's says `Center` (`Top`, `Center`, `Bottom`, `Fill`). The error
  names the right one.
- **`CornerRadius = (24, 24, 24, 24)` draws a capsule, not 24 px corners.** The radii are fractions by default
  (`CornerRadiusUnitMode = Percentage`, 1 rounding a corner fully); write `CornerRadiusUnitMode = Value` for pixels.

## Text and fonts

- **Boxes, or nothing, where Chinese, Japanese or Korean text should be.** The font has no glyph for the character, and
  DreamGUI logged a warning once naming it. The default font covers these through `/DreamGUI/DefaultFont_DistanceField_CJK`;
  a font of your own needs a fallback. [Fonts](https://gui.toolchain.64hz.cn/en/docs/concepts/text/#fonts).
- **Gaps or seams where a glyph's strokes cross.** The font file keeps its strokes as overlapping contours — a static
  instance cut from a variable font does unless its overlaps were removed — and DreamGUI logged a warning once naming the
  glyph. Run the file through fontTools' `removeOverlaps` and use the result. [Fonts](https://gui.toolchain.64hz.cn/en/docs/concepts/text/#fonts).
- **A control's text has the wrong typeface.** Styles with a `Font` field: text input, spin box, rich text, expandable
  area, ring menu. [Styles](https://gui.toolchain.64hz.cn/en/docs/controls/styles/).

## Layout and world space

- **The text of a row in a vertical box stands one letter to a line.** A vertical box gives a child the width the child
  measures, and a node whose `SizeDelta` width is 0 measures 0, so its text wraps after every character. Write
  `@slot HorizontalAlignment = Fill` on the row (or in its style), or give it a width.
  [Positions, sizes and axes](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#positions-sizes-and-axes).
- **A list that opens or grows draws past its panel.** A panel does not clip. Use a `ScrollBox`, or `Clipping =
  ClipToBounds`. [The panels](https://gui.toolchain.64hz.cn/en/docs/concepts/layout/#the-panels).
- **A world-space panel sits off its actor.** The component's `Pivot` and size go on the user widget; give the class's root
  node anchors from `(0, 0)` to `(1, 1)` and no size of its own. [World-space UI](https://gui.toolchain.64hz.cn/en/docs/concepts/world-space/).

## Rendering

- **A blur inside a faded or masked container is flat grey.** A group (`GroupOpacity`, `GroupMask`, ...) draws apart and a
  background effect inside it sees only the group. Put the blur outside and shape it with `StrengthMap`.
  [Inside a layer group](https://gui.toolchain.64hz.cn/en/docs/concepts/backdrop-effects/#inside-a-layer-group).
- **A material of mine ignores the clip or the mask transition.** Shade through `MF_DreamUI_Shade`.
  [Clip masks](https://gui.toolchain.64hz.cn/en/docs/concepts/clip-masks/).
- **A pixelate fade runs fast and then crawls.** The strength is not a block size.
  [Pixelate strength](https://gui.toolchain.64hz.cn/en/docs/concepts/backdrop-effects/#pixelate-strength).

## Code

- **`NativeOnDestruct`, not `NativeDestruct`.** The C++ virtuals of a user widget have their own names, and
  `GetWorld()` may be null in `NativeOnInitialized`; use `NativeOnConstruct`.
  [An instance's life cycle](https://gui.toolchain.64hz.cn/en/docs/concepts/widget-blueprints/#an-instances-life-cycle).

## Tools

- **A recording at the wrong scale, or two recordings spoiling each other.** Pass `-ForceRes` with the resolution, and run
  one capture at a time. [Recording the UI off-screen](https://gui.toolchain.64hz.cn/en/docs/tools/debugging/#recording-the-ui-off-screen).
- **Compiling `.dui` classes from a script.** The `DreamUICompileWidgets` commandlet.
  [Compiling from the command line](https://gui.toolchain.64hz.cn/en/docs/tools/scripting/#compiling-from-the-command-line).
