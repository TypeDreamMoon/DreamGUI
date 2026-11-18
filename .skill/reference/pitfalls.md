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
- **A glow meant to spill out of its card is still DUI8008 until it says so.** `layout` reports what draws past its
  panel and what is drawn over a text. A decoration that does either on purpose takes `@tag Bleed`; one that does not
  goes inside, or before the text in the file. [Going past the frame](https://gui.toolchain.64hz.cn/en/docs/tools/dreammcp/#going-past-the-frame-on-purpose-tag-bleed).
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
- **A rect block's corners come out in the wrong places.** Its `CornerRadius` runs bottom-right, top-right, top-left,
  bottom-left (as `ClippingCornerRadius` does), not clockwise from the top left: a shield with round bottom corners is
  `(22, 6, 6, 22)`.
- **`Text = "Hello"  @slot HorizontalAlignment = Fill` on one line is DUI2001.** An `@` right after a value is read as
  that value's annotation (`@key`, `@note`, `@max`, `@noloc`). End the value with `;` or start a new line:
  `Text = "Hello";  @slot HorizontalAlignment = Fill`.
- **`Padding = (@Gap, 0, @Gap, 0)` is DUI4003.** A resource stands for a whole value, never for one part of a tuple.
  Write the numbers, or give the whole value a name.
- **A `Color` prop does not drive `BodyColor` (DUI5004).** A `props` colour is a linear colour, and a visual's
  `Color` and a rect block's `BodyColor` are not; neither can be bound to it. Show the right part instead: one node per
  colour, each with `Shown <- Rarity == 2` on an `Integer` prop, or `Shown <- Kind == "Rare"` on a `String` one.

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

- **The whole screen is drawn several times too large, or off the screen, at 1280 x 720.** A `ScaleBox` around a
  1920 x 1080 design canvas measures the canvas by what is inside it, not by its `SizeDelta`, and scales that up. Give the
  canvas `@slot Size = (1920, 1080)` as well. [The root of a screen](https://gui.toolchain.64hz.cn/en/docs/dui/building-blocks/#the-root-of-a-screen).
- **A short label, a chip or a key cap is 100 px wide.** Every node starts 100 x 100 (`AnchorData.SizeDelta`), and a box
  or an overlay that sizes itself to its children counts that size -- a backdrop filling an overlay included. Give the
  nodes it should measure by their content `AnchorData.SizeDelta = (0, 0)`; the presets' `ui.FillSlot` does.
- **What a host puts in a component's slot spills out of it.** A slot with no layout box (`slot Badge { }`) places what
  is put in it by its anchors, at its own size. Give that widget `ui.FillParent` (anchors at the four corners).
- **The text of a row in a vertical box stands one letter to a line.** A vertical box gives a child the width the child
  measures, and a node whose `SizeDelta` width is 0 measures 0, so its text wraps after every character. Write
  `@slot HorizontalAlignment = Fill` on the row (or in its style), or give it a width.
  [Positions, sizes and axes](https://gui.toolchain.64hz.cn/en/docs/dui/nodes-and-values/#positions-sizes-and-axes).
- **A list that opens or grows draws past its panel.** A panel does not clip. Use a `ScrollBox`, or `Clipping =
  ClipToBounds`. [The panels](https://gui.toolchain.64hz.cn/en/docs/concepts/layout/#the-panels).
- **A world-space panel sits off its actor.** The component's `Pivot` and size go on the user widget; give the class's root
  node anchors from `(0, 0)` to `(1, 1)` and no size of its own. [World-space UI](https://gui.toolchain.64hz.cn/en/docs/concepts/world-space/).
- **A panel meant to face the middle of the screen faces away from it.** A positive Yaw brings a node's right edge
  nearer and a positive Pitch its top edge, so the signs run the other way from what a first guess gives: a panel on
  the left faces the middle with a negative Yaw, one on the right with a positive Yaw; a plane laid back like a table
  takes a negative Pitch; a row of cards that curves toward the player goes from a negative Yaw on the left to a positive
  one on the right, largest at the ends. `layout` and `preview` say which way each turned face looks as drawn
  (`Panel RosterLeft, on the left: turned 14 degrees: faces toward the screen centre (right edge farther)`); read that
  line after every turn. [Turned in 3D](https://gui.toolchain.64hz.cn/en/docs/dui/building-blocks/#turned-in-3d).

```
// DUI/Pitfalls/Facing.dui
class /Game/UI/Pitfalls/WBP_Facing
use "Theme.dui" as ui

// The three turns, with their signs: a positive Yaw brings a node's right edge nearer, a positive Pitch its top edge.
style Side {
    AnchorData.AnchorMin = (0.5, 0.5)
    AnchorData.AnchorMax = (0.5, 0.5)
    AnchorData.Pivot = (0.5, 0.5)
    AnchorData.SizeDelta = (360, 280)
}
style ArcCard : ui.Card {
    @slot Size = (100, 150)
}

Widget Root : ui.FillParent {
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillParent

    // Two panels that face the middle: on the left a negative Yaw, on the right a positive one.
    RectBlock FacingLeft : ui.Panel : Side {
        AnchorData.AnchoredPosition = (-620, 160)
        RenderRotation = (0, -16, 0)
    }
    RectBlock FacingRight : ui.Panel : Side {
        AnchorData.AnchoredPosition = (620, 160)
        RenderRotation = (0, 16, 0)
    }

    // A plane laid back, its far (top) edge narrower: a negative Pitch.
    RectBlock Table : ui.Panel : Side {
        AnchorData.AnchoredPosition = (0, -250)
        AnchorData.SizeDelta = (720, 300)
        RenderRotation = (-35, 0, 0)
    }

    // Cards that curve toward the player: negative left of the middle, positive right of it, the ends turned most.
    HorizontalBox Arc : Side {
        Spacing = 20
        AnchorData.AnchoredPosition = (0, 160)
        AnchorData.SizeDelta = (580, 150)
        RectBlock CardA : ArcCard { RenderRotation = (0, -24, 0) }
        RectBlock CardB : ArcCard { RenderRotation = (0, -12, 0) }
        RectBlock CardC : ArcCard { RenderTranslation = (-20, 0, 0) }
        RectBlock CardD : ArcCard { RenderRotation = (0, 12, 0) }
        RectBlock CardE : ArcCard { RenderRotation = (0, 24, 0) }
    }
}
```

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
