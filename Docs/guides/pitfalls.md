---
title: Common pitfalls
description: Things that look like bugs and are not, or that cost an afternoon -- tweens on one property, angle conventions, raycast targets, colour literals, style overrides, fonts, world-space widgets, effects inside groups, pixelate strength, recording off-screen, and where UMG's names differ -- each with the page that explains it.
---

Each entry says what you see, why, and what to do; the page it links to has the detail.

## Animation

- **A fade, then the opposite fade, and the result flickers or ends wrong.** Two tweens on one property used to both run.
  A tween now stops the earlier ones on the same property when it starts; `SetReplacesEarlierTweens(false)` opts one out
  (a looping breath that should coexist with a hover). Tweens in a sequence are never stopped.
  [One tween per property](../concepts/animation.md#one-tween-per-property).
- **A widget in a panel does not stay where `AnchoredPositionTo` puts it.** The panel writes the position back. Move what
  is drawn: `RenderOffsetTo`, `RenderScaleTo`, `RenderRotationTo`, `RenderPoseTo`.
  [Widgets inside panels](../concepts/animation.md#widgets-inside-panels-animate-the-render-transform).
- **A positive roll turns clockwise.** `RenderRotation.Roll`, `SetRenderTransformAngle` and `RenderAngleTo` turn a widget
  clockwise on the screen (its right edge down), as UMG's angle does.

## Angles

- **The angle properties do not share a zero.** `Ring` measures from three o'clock, counter-clockwise; `Native.RingMenu`,
  `Native.ProgressBar` and text on a path measure from twelve o'clock, clockwise. A progress ring on `Ring` is
  `StartAngle = 90`, `EndAngle = 90 - 360 * progress`. [Angle conventions](../concepts/widgets.md#visuals-what-a-widget-draws).

## Input

- **A label over a button takes no hover or clicks.** Right: a `UDreamText` is not a raycast target by default. A text that
  is the thing to press sets `bRaycastTarget = true`. [Visuals](../concepts/widgets.md#visuals-what-a-widget-draws).
- **`bRaycastTarget` on a `Widget` node is an error.** It belongs to a visual. Write `Raycastable = Disabled` on the widget.
  [Properties and values](../language/nodes-and-values.md#properties-and-values).
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
  they start ticked. [In .dui and at run time](../controls/styles.md#in-dui-and-at-run-time).
- **`Text Note : A : B` is a syntax error.** A node wears one style, a style inherits one.
  [Styles](../language/styles-and-resources.md#styles).
- **`#8C8C94` is darker than it looks.** Colour literals are sRGB-encoded, and a tint is multiplied in linear space.
  [Values](../language/nodes-and-values.md#values).

## Text and fonts

- **Boxes, or nothing, where Chinese, Japanese or Korean text should be.** The font has no glyph for the character, and
  DreamGUI logged a warning once naming it. The default font covers these through `/DreamGUI/DefaultFont_DistanceField_CJK`;
  a font of your own needs a fallback. [Fonts](../concepts/text.md#fonts).
- **A control's text has the wrong typeface.** Styles with a `Font` field: text input, spin box, rich text, expandable
  area, ring menu. [Styles](../controls/styles.md).

## Layout and world space

- **A list that opens or grows draws past its panel.** A panel does not clip. Use a `ScrollBox`, or `Clipping =
  ClipToBounds`. [The panels](../concepts/layout.md#the-panels).
- **A world-space panel sits off its actor.** The component's `Pivot` and size go on the user widget; give the class's root
  node anchors from `(0, 0)` to `(1, 1)` and no size of its own. [World-space UI](../concepts/world-space.md).

## Rendering

- **A blur inside a faded or masked container is flat grey.** A group (`GroupOpacity`, `GroupMask`, ...) draws apart and a
  background effect inside it sees only the group. Put the blur outside and shape it with `StrengthMap`.
  [Inside a layer group](../concepts/backdrop-effects.md#inside-a-layer-group).
- **A material of mine ignores the clip or the mask transition.** Shade through `MF_DreamUI_Shade`.
  [Clip masks](../concepts/clip-masks.md).
- **A pixelate fade runs fast and then crawls.** The strength is not a block size.
  [Pixelate strength](../concepts/backdrop-effects.md#pixelate-strength).

## Code

- **`NativeOnDestruct`, not `NativeDestruct`.** The C++ virtuals of a user widget have their own names, and
  `GetWorld()` may be null in `NativeOnInitialized`; use `NativeOnConstruct`.
  [An instance's life cycle](../concepts/widget-blueprints.md#an-instances-life-cycle).

## Tools

- **A recording at the wrong scale, or two recordings spoiling each other.** Pass `-ForceRes` with the resolution, and run
  one capture at a time. [Recording the UI off-screen](../tools/debugging.md#recording-the-ui-off-screen).
- **Compiling `.dui` classes from a script.** The `DreamUICompileWidgets` commandlet.
  [Compiling from the command line](../tools/scripting.md#compiling-from-the-command-line).
