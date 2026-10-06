# Moving a project to DreamGUI 1.0

1.0.0 is the first public release. The fork's development builds before it were numbered 1.x, 2.0 and 2.1 -- see the
[CHANGELOG](../CHANGELOG.md) -- and this guide names them that way: a 2.1 here is the development build, older than 1.0.0.

For five kinds of project:

- one on **1.0.0**, moving to what comes after it -- [From 1.0.0 to the next release](#from-100-to-the-next-release),
  right below;
- one on a **development build, 2.1 or 2.0** -- [From 2.1 to 1.0.0](#from-21-to-100), and from 2.0
  [From 2.0 to 2.1](#from-20-to-21) before it;
- one with **assets saved against LGUI or LexUI** — the upstream this fork started from;
- one on an **early build of this fork** (the single-module 1.x builds, not 1.0.0) — before the module split, the
  input rework and the renderer rework;
- one with **C++ of its own against the plugin** — subclasses, custom visuals, a viewport client of its own.

The numbered sections are the move to the 2.0 line from LGUI, LexUI or the 1.x builds; a project making it reads
[From 2.0 to 2.1](#from-20-to-21) and [From 2.1 to 1.0.0](#from-21-to-100) afterwards. Everything here is about keeping what you have. What the plugin is and how to start from
nothing is in the [README](../README.md); what each version added is in the [CHANGELOG](../CHANGELOG.md).

## From 1.0.0 to the next release

For a project on 1.0.0. Nothing was renamed, and nothing has to be done before opening the project. A control takes
input the way UMG's does now, and a screen built around the old way will feel different:

- **A list, tile or tree view in Multi mode chooses rows as SListView does.** A plain click selected the row and kept
  every row already chosen, and a second click took it away; now a plain click chooses that row alone. Ctrl+click (Cmd on
  a Mac) is what adds a row or takes one away, Shift+click adds the rows from the anchor to the one clicked, Shift or Ctrl
  with an arrow extends the selection from a focused row, and Ctrl+A chooses every row. A finger's tap adds its row as
  before, but a second tap on a chosen row no longer takes it away. A pad's confirm on a row is a plain click, so a
  pad-driven screen that let players pick several rows by confirming each one picks one now; such a screen keeps its own
  set -- toggled in `OnItemClicked`, put back with `SetSelectedIndices` -- or offers Ctrl's meaning on a pad button of its
  own. `SetItemSelection` and the other selection calls are unchanged, so code that builds a selection is not affected.

A C++ subclass of the list family that overrides `NativeOnKeyDown` calls `Super` to keep these keys, as for any key
the base answers.

## From 2.1 to 1.0.0

For a project on the 2.1.0 development build. Nothing was renamed between the two, so an asset 2.1 saved naming the
current types loads as it is, and nothing has to be done before opening the project -- with one exception, below.
What 1.0.0 adds to `.dui` -- components written in `.dui`, `use … as`, `if`, `for`, `rows`,
`@slot { … }` and `@fill` -- is new syntax beside the old, and a 2.1 file compiles as it did. Three things can look
different afterwards, each the end of a bug a project may have worked around:

- **A brush material whose only parameter of the canvas's is `DreamUI_IsRenderByDreamUIRenderer` is told the renderer.**
  Such a material was premultiplied twice by DreamGUI's renderer, and so drew darker at its edges. A material that
  compensated for that now draws too light: take the compensation out.
- **A material instance given to an image shows the parameters set on it after its first draw.** An instance that was
  re-created every frame to get round that can be kept and set instead.
- **Text and blocks after a material-drawn image draw again** on screen and render-target canvases; a layout that kept
  text out of the batch after such an image no longer needs to.

The focus ring now fits the control it marks and can be refused per control (`UUISelectable::bUseFocusRing`); a ring
class of your own with a Blueprint handler keeps its own layout. `DreamTween.MaxStepSeconds` is new and off by default.

**The plugin no longer carries CoreRedirects.** 2.1 shipped 763 of them in `Config/DefaultDreamGUI.ini`; 1.0.0 ships
none, and that file is gone. An asset that loaded only through one -- saved against LGUI or LexUI, by an early 1.x
build, before the module split, or before a control or event rename -- has to be resaved before it is opened with
1.0.0, or it loses whatever named an old type. The redirects work with 1.0.0 unchanged, since nothing was renamed
since 2.1, so the project borrows them for as long as the resave takes:

1. Copy the `[CoreRedirects]` section of [2.1.0's `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini) (commit `3049561a`) into the project's
   `Config/DefaultEngine.ini`.
2. Open the project with 1.0.0 and resave every asset that uses DreamGUI -- *File > Save All* after opening them, or
   the `ResavePackages` commandlet over the content.
3. Delete the section from `Config/DefaultEngine.ini` again.

A project that never had such an asset -- one started on 2.1, or resaved since -- skips all three. Sections 1 to 3
below say what the redirects cover.

## From 2.0 to 2.1

For a project already on 2.0. Nothing was renamed, so no redirect is needed and nothing has to be done before opening
the project; what follows is what can look or behave differently afterwards, and what C++ of your own has to change. The
[CHANGELOG](../CHANGELOG.md) lists what is new.

### Before you upgrade

- **Assets resaved by 2.1 do not open in 2.0.** 2.0 refuses a package saved with a newer `FDreamGUIObjectVersion`. Keep
  the project under source control, as for any upgrade.
- **No redirects were added**; there are still 763, and every name 2.0 saved still loads.
- **`UDreamDialog::DimmerBehaviour` is gone**, with no redirect. It was read-only to Blueprints, and a Blueprint that
  read it fails to compile: the dimmer is no longer a button, and `bShowDimmer` and `bCloseOnDimmerClick` configure it.

### Assets that change as they load

- A font's fallback list (`FallbackFontArray`) becomes `Fallbacks` entries with the default settings: every code point,
  any language, scale 1, not preferred over the font's own face.
- Emoji data keys gain their whole sequence.
- A distance-field font saved before `SdfSource` existed loads on the outline multi-channel field instead of being
  forced to the single-channel one: sharper corners, a BGRA atlas, and about four times the atlas memory. Set
  `SdfSource` back to keep the old look.
- A Border visual whose colour equals its `BrushColor` is reset to white when it registers.
- `UDreamMeshModifierPositionAsUV`'s UV channel has defaulted to 0 since 2.0.0, and channel 1, the canvas's own, is
  refused with a warning. An asset made before 2.0 that relied on the old default of 1 has to set the channel.

### Defaults that changed

An asset that left a property at its default never saved it, so it takes the new default, and a property new in 2.1
starts at its default:

- **The default font** is Roboto with real bold and italic faces and DroidSansFallback for CJK, on the multi-channel
  field: every text that uses it lays out again, with other widths.
- **Small text** (`UDreamGUISettings`). 2.0 drew all text from the distance field. Small screen text now draws from
  hinted coverage glyphs on the device pixel grid (`bSmallTextCoverage` on, up to `SmallTextMaxPixelSize` 20,
  `SmallTextContrast` 1), and `bSmallTextCorrection`, on, darkens what still draws from the field at those sizes. With an
  outline, a glow or an underlay, its face draws from coverage glyphs -- hinted, or unhinted under an outline thinner
  than 2 device pixels (`SmallTextEffectFace` Auto) -- and those effects from the field, moved to line up with it. In a
  render layer it draws from coverage once the layer has held still for 3 frames, and from the field while the layer
  moves. Face softness and dilation keep a text on the field. `DreamGUI.Text.SmallTextCoverage 0` turns coverage off
  everywhere, fonts set to On included.
- **Text**: ligatures on (`bLigatures`), `TabSize` 8, `bPreferColorEmoji` on.
- **Rendering**: `RenderLayer` Auto; `bShowEffectWhenDisabled` on, so a disabled Border's background draws at 45 %.
- **Scrolling**: `UUIScrollView::CanScrollInSmallSize` off, so a short list no longer rubber-bands;
  `bAllowRightClickDragScrolling` and `bEnableTouchScrolling` on. A scroll bar is no longer a focus target
  (`UUIScrollbar`'s `bCanNavigateHere` off; turn it on to put a bar back in the navigation), its arrows never are, and
  the bar a list or a scroll box makes for itself is never a Tab stop.
- **Lists, tiles and trees** are one Tab stop (`TabNavigation` Once): Tab enters at the selected row when it can,
  scrolling it into view, else at the first row, or the last for Shift+Tab; the next Tab leaves the list.
- **Player indices**: `UDreamUINavigationScope::UserIndex`, `UDreamUIActionTrigger::UserIndex` and
  `UDreamUIActionBar::UserIndex` default to -1, the widget's owning player; they were 0. An index set by hand still
  wins, and `GetUserIndex()` answers the index in force. On one player nothing changes; on a split screen the second
  player's screens answer to that player.
- **Tab and focus** (Project Settings > Plugins > Dream GUI): `bTabNavigation` on, `TabOrder` Hierarchy,
  `bTabWrapsAtScreenEnd` on, `bTabStartsTextEdit` on; `bFocusVisibleOnlyFromKeys` on, `bHideCursorOnGamepad` on,
  `bUsePlatformAcceptBack` on, `InputModeWithoutScope` All. On dropdowns (`UUIDropdown`, `UDreamDropdown`),
  `bTabCommitsHighlightedRow` on.
- **Keys** (the same page): `ConfirmKeys`, `BackKeys`, `DirectionKeys`, `PageKeys` and `ExtentKeys` hold 2.0's tables,
  with the triggers added to `PageKeys` (LT and RT page); `PreviousTabKeys` and `NextTabKeys` are the shoulder buttons
  (LB and RB switch tabs).

### Switches that put 2.0's behaviour back, or measure the new one

- `TabOrder = LegacyGeometric` (project setting): Tab and Shift+Tab move geometrically, as in 2.0, for this release.
- `bSmallTextCoverage` off (project setting), or `DreamGUI.Text.SmallTextCoverage 0`: all text draws from the field, as
  in 2.0. `SmallTextEffectFace = Field` keeps only small text with an outline, a glow or an underlay wholly on the field.
- `DreamGUI.Text.SmallTextCoverage` (-1 follows the project setting; 0 off for every font, those set On included; 1 on
  for every font that leaves it to the project) and `DreamGUI.Text.SmallTextMaxPixelSize` (above 0 replaces the setting
  for every font with no limit of its own) reach every font, and a change repaints every text;
  `SmallTextRepaintBudgetPerFrame` (512) caps how many texts a world repaints onto coverage glyphs in one frame.
- `DreamGUI.Text.IncrementalParse`, `DreamGUI.Text.IncrementalMeasure` and `DreamGUI.Text.InPlaceDisplayList` each turn
  one part of the incremental layout off, back to laying the whole text out; `DreamGUI.Text.VerifyIncremental` (not in
  Shipping builds) checks incremental layouts against fresh ones and says where they differ.

- An incremental layout and a fresh one of the same text are the same, element for element; the switches only trade
  time. `ElementsParsed` counts the elements an edit actually spliced in, not reads a refused window threw away.

- New in 2.1 and off by default, to be decided after measurement: `bFieldTextCorrection` (field text above
  `SmallTextMaxPixelSize` gets small text's contrast and linear-light blend, so its weight does not jump at the
  cut-off), `DreamGUI.Text.SmallTextOnMove` (what a small text does while it moves off its pixel grid: 0 is repainted
  from coverage at every move, 1 keeps its coverage quads, 2 draws from the field; 1 and 2 repaint from coverage once it
  has held still for 3 frames) and `DreamGUI.Scroll.SnapToDevicePixels` (a scroll view on a 2D canvas moves its content
  in whole device pixels).

### Text

- **Line breaking follows the game's culture**: the line and word iterators are made for the current culture and made
  again when it changes. 2.0 followed the operating system's language. A game with CJK text has to be packaged with the
  ICU data for it (the EFIGSCJK preset, or All for Thai and its neighbours): see
  [FontsAndPackaging.md](FontsAndPackaging.md#4-icu-data-package-efigscjk-or-all).
- **Effects fade with the text, not with its colour.** An outline, glow or underlay fades with the render opacity, the
  content tint's alpha and a TextAnimation's alpha, and no longer with the alpha of the text's own colour. Text faded
  out by its tint or a TextAnimation now fades its outline too; hollow text -- a transparent colour with an outline --
  keeps its outline.
- **Paints.** `FDreamTextStyle` has `FacePaint`, `OutlinePaint`, `OverlayPaint`, `OverlayBlend`, `PaintBoxHorizontal`
  and `PaintBoxVertical`, all off, so nothing draws differently until one is set. A face paint takes the place of the
  text's own colour on the face, as CSS's `background-clip: text` does; a `<color>` run inside a painted text is solid,
  and a custom style's Multiply tints the gradient. Rich text has a `<gradient=Name>` tag behind a new filter flag,
  `Gradient`: a text whose `RichTextTagFilterFlags` was saved as a mask without it shows the tag as text. Rich-text
  custom style entries have a `paintType` (KeepOrigin, Set, None) and a `paint`. A gradient's CSS is read as Chrome reads
  it but for two things: `to <corner>` takes a square's angle (45deg, 135deg, ...) whatever the box's shape, and colour
  names are CSS's (`green` is #008000, where `<color=green>` is #00FF00). Editing a paint never lays the text out --
  another gradient only writes its rows, turning a paint on or off repaints -- except an undo, which lays it out and
  paints it again.
- **Tab stops** measure their space as a space in the text is measured -- at the device size under a scaled canvas -- so
  `TabSize` spaces end exactly on a stop.

- **Gradient boxes**: the text block spans the content box across and runs from the first line's top to the last
  *visible* line's bottom (lines a clamp hid are left out); a rich-text run whose letters are all solid gets no piece of
  a `Run` box. A `<color>` inside a gradient run is solid; a custom style's Multiply inside it tints the gradient.
- **Line breaking by culture in the editor**: ICU is a static library on Windows, so the editor gives DreamGUI a copy of
  its own; 2.1 points that copy at the engine's ICU data (`Engine/Content/Internationalization`) so the editor breaks
  lines by the game's culture like a packaged game does. If the data cannot be read, the log says so once and the
  engine's default-culture iterators stand in.

- **A material of your own** that does not shade through `MF_DreamUI_Shade` gets no coverage glyphs, draws its emoji from
  `EmojiData` or a monochrome face rather than sampling a colour bitmap as a field, and paints gradients by vertex
  colour. DreamGUI's own materials carry a `DreamUI_ShadeMarker` parameter that says they shade through it.
- **From 2.0's changes**, for a project that skipped their notes: CSS half-leading for line-height percentages; "Left"
  means the start in right-to-left paragraphs; bidi reorders lines (L2); letter spacing is not applied inside cursive
  scripts and counts at the line's end as in Chrome; bold advances once per cluster, and only for synthetic bold;
  underlines and strikethroughs use the font's metrics and are drawn per run; outlines and hard shadows are mitered;
  effect colours are decoded as sRGB and draw darker; emoji precedence changed; `<lang>` tags are parsed (with the
  Language filter flag off they are text); a scaled fallback grows its line box; tabs advance to stops.

### Fonts

- Embedded font files (`CustomFontFile`, not external) are read again when the game is cooked, and a cook that finds
  nothing to embed is an error rather than a font that ships with no face.
- A font keeps the atlas cells of its coverage glyphs while any text still draws from them; a hidden, collapsed or
  inactive text lets go of them, and paints again before it is drawn if they were taken back meanwhile. Past twice the
  cell budget (`MaxCoverageCells`, 4, in Project Settings > Plugins > DreamUI) new small-text glyphs wait and draw from
  the field meanwhile, with one warning per font.
- A font's atlas texture and its list of texts are no longer recorded in undo transactions; bitmap fonts reload on undo
  and redo.

### Layout

Since 2.0: a text inside a panel is measured at the width the panel will give it; collapsed children measure 0;
`OnDimensionChanged` fires once rather than twice; grid spans, centred asymmetric padding, the wrap box's fill, the scale
box's modes and stated scales follow UMG; the scroll box has fill slots and front and back padding, does not overscroll
when nothing scrolls, collapses its built-in bar and hides an idle track.

### Controls, focus and navigation

- **Tab** is Next and Shift+Tab Prev, with no Ctrl, Alt or Cmd held; with one of those held Tab goes to key bindings
  only, and bindings get every key first. The order is the hierarchy's, siblings by `TabIndex`; an explicit Next or Prev
  link on a selectable or a widget still wins, unless its target cannot take the focus or lies outside where Tab is
  held. `TabOrder = LegacyGeometric` puts 2.0's geometric order back for this release. Tab stays inside the top popup,
  then inside a modal or a dialog whose dimmer is up, else on the player's screen -- and with the focus on a layer drawn
  in front of an open popup, on that layer. Another player's popups on a shared screen are never stops. Lists, tiles and
  trees are one stop; a stop out of view is scrolled into it; tooltips, popup sheets and scrollbars are never stops; a
  widget made unfocusable is no landing place for Tab, the arrows or a per-widget rule. A Tab counts as handled whenever
  the player has a Tab stop to go to, even at a screen end that does not wrap.
- **Text fields**: Tab commits the edit -- as leaving the field does, by `bSubmitWhenDeactivate` -- and moves on, and
  arriving by Tab starts editing; a multi-line field types a tab only with `bTabTypesTabCharacter` (then Ctrl+Tab leaves
  it); during IME composition Tab stays with the IME; Alt+Tab and Cmd+Tab are never a field's, nor Ctrl+Tab unless it
  types tabs. A field takes Tab only while it uses it, so `IgnoreKeys` no longer needs Tab in it to keep Tab from being
  swallowed. A pad's accept on a field being edited submits it; the press that started the edit does not count.
- **Dropdowns and menus**: Tab in an open dropdown commits the highlighted row (`bTabCommitsHighlightedRow` on
  `UUIDropdown` and `UDreamDropdown`, on by default), closes it and moves to the next control, Shift+Tab to the previous;
  Tab in a menu closes the whole menu chain and moves on (popup `TabBehavior` CloseAndContinue). Other popups keep Tab
  inside them (Cycle).
- **The focus look** -- the Focused state and the focus ring -- shows only while the focus came from keys or a pad, or
  from script after a key or pad was the last input. A mouse click no longer shows it, and a mouse hover no longer takes
  the ring away. The ring shows on every screen page while the focus is drawn, a page with no presenter included, and for
  the focus a scope gives as its screen opens. A scroll box's `OnFocusReceived` and `OnFocusLost` follow the real focus,
  not its Focused look.
- **Navigation scopes have an input mode** (`InputMode`: All, Menu, Game). Game turns DreamGUI's built-in navigation,
  confirm, Back, paging and tab switching off for that player while the scope is the active one; bindings, pointers and
  typing still work. With no scope and no focus, a navigation press looks only at the player's own screen-space
  canvases, never at world-space UI or another player's screen.
- **Popups and dialogs**: showing a modal closes that player's open popups first, and the focus goes back to their
  opener when the modal closes. A press on a layer drawn in front of an open popup closes the popup and still reaches
  that layer -- it is no longer swallowed -- and Back goes to such a layer before the popup. A standalone dialog
  confines navigation exactly while its dimmer is up, whatever Close on Back says: an undimmed dialog with Close on Back
  on no longer confines it. A menu anchor's `OnMenuOpenChanged(false)` fires only after an open it announced. A lifted
  panel menu keeps its slot's padding, nudge and size bounds, and its slot comes back intact when it goes home; the
  canvas a popup was given when it opened is taken off again then, or given back its sort order, override and trace
  channel when it had one already. A standalone dialog's scope belongs to its owning player.
- **Scrolling**: paging, Home, End and the right stick start from the focused widget itself, so a focused scroll box
  scrolls itself; the triggers page.
- **The shoulder buttons** switch the active tab view's tabs, passing over disabled ones and going round the ends, and
  the action bar shows "Previous tab" and "Next tab" prompts for them.
- **Per-widget navigation rules** (`UDreamWidgetNavigation`) stay inside a confining dialog and the canvas, a Wrap rule
  on Next or Prev is honoured, and a direction the rules leave unset is answered by the selectable on the same widget.
- **From 2.0's changes**: the popup layer replaces the click blockers; a click outside a popup is consumed; Back closes
  only the top popup; the focus returns to the opener; a dropdown opens on its selected row; dialogs push a Back scope;
  confirm no longer pulls the focus back; disabled toggles, tabs and dropdowns ignore clicks; sliders and dropdowns take
  the left button only; the text field edits by UTF-16 offset.

### Input

- **Keys and the pad act on the focus.** Enter, Space and a pad's accept press the focused widget, not the hovered one;
  with nothing focused a confirm presses nothing -- it used to press the default control -- and leaves the mouse's hover
  alone. Hover moves the navigation highlight only after the mouse really moved, and a new pointer starts off the
  viewport.
- `UDreamEventSystem::ActivateNavigationInput(PointerId, DefaultWidget)` and `SetHighlightedComponentForNavigation` no
  longer say where keys start: keys start from the player's focus. Code that used them to place the start sets the focus
  instead (`UDreamWidget::SetFocus`).
- The key tables are read again whenever they change, so a remap at run time applies from the next key. The platform's
  accept and back buttons are read at run time (a Switch swaps them), and a face button the platform uses the other way
  is left out of the other table.
- In DreamGUI's UI-only input mode (`UDreamUIInputModeLibrary::SetInputModeUIOnly`) the cursor hides while the player
  uses a pad and comes back on the mouse or the keyboard, and the mode starts with it hidden when the player is on a pad.
  Elsewhere DreamGUI leaves the cursor alone.
- While Slate's focus is on the bare game viewport and the player has a DreamGUI focus or a Tab stop, Slate's own
  navigation -- Tab, the arrows -- is DreamGUI's, so none of it moves the focus off the viewport into UMG.
- The pad model is detected from the device used last, on every device change; the action bar rebuilds when it changes.
- A player index with no player creates no input user.
- **From 2.0's changes**: a click needs its release on the pressed widget; a release goes to whoever took the press; the
  pointer is no longer parked at (0, 0); the Slate input source checks UMG's cover and the keyboard focus.

### Rendering

- **Field text composites its layers premultiplied**: what lies under nothing keeps its colour. Text drawn from the
  field with an alpha below 1 -- a translucent colour, render opacity, a fade, a gradient stop -- no longer darkens
  towards black; glows are brighter towards their outer edge; in the viewport the edges of light text on a dark ground
  are as full as Slate draws them (they were thinner), and in a render target they blend in linear light like dark text
  always did. Black text and opaque text over an opaque outline look as they did. Screenshots of coloured, faded or
  glowing text taken with 2.0 will differ at the edges.
- **Multisampled canvases blend in sRGB when their target is sRGB**: an MSAA canvas's edges and translucent overlaps
  come out slightly different, closer to the same canvas without MSAA.
- **The DreamUI vertex is 64 bytes** (it was 56) with a fifth texture coordinate, `UV4`, vertex attribute 8, after
  `TangentZ`; every other field kept its offset and attribute number, and `LEXUI_VERTEX_TEXCOORDINATE_COUNT` is still 4
  (`LEXUI_VERTEX_UV_CHANNEL_COUNT`, 5, counts UV4 in). A material reads it as `TexCoord(4)`. Text writes it for gradient
  paints; everything else writes (0, 0). A vertex made with a constructor, `SetNum` or a whole-struct copy is right; code
  that grows a vertex array uninitialized and fills it field by field has to write `UV4`. A world-space canvas drawn by
  the UE renderer carries five texture coordinates where it carried four.
- **Text quads carry a paint slot in `UV2.x`**: `UV2.x += 128 * slot` (`DreamTextQuadCode::SlotStride`), slot 0 for an
  unpainted text, whose vertices are what 2.0 wrote. A shader or material that decodes `UV2.x` strips the slot first
  (`DreamUIText_UnpackPaintSlot` in `DreamUIText.ush`); a mesh modifier that rewrites `UV2.x` of text quads keeps it
  (`DreamTextQuadCode::AddSlot(NewCode, DreamTextQuadCode::GetSlot(OldCode))`). `DreamUI_ShadePixel` keeps its old
  signature as an overload that draws no paints.
- **The shadow, outline and long shadow modifiers' copies of a colour emoji** draw its silhouette in the copy's colour
  (they drew a second full-colour emoji), and copies are never painted. A mesh whose shadow copy would take it past the
  vertex budget is drawn without the shadow, with one warning; it used to be dropped whole.
- `MF_DreamUI_Shade` takes the paint rows as `DreamUI_PaintDataTexture`.
- The render stats' uploaded bytes grow by 8 a vertex.
- **From 2.0's changes**: transform changes are announced once a frame (`r.DreamUI.DeferTransformNotifications`); Auto
  render layers do not pixel-snap; new console variables (`DreamUI.Animation.LitePlayer`, `DirectEvaluation`,
  `DreamGUI.Text.ShapeCache`, `IncrementalLayout`, `r.DreamUI.*`).

### Animation and tweens

- **Finished and killed tweens retire.** A retired tween is marked garbage, so its handle reads invalid and a UPROPERTY
  holding it is nulled at the next garbage collection. `Kill`, `ForceComplete` and `Goto` do nothing on a tween that was
  killed or has retired, and `Restart` does nothing on a retired one but log a warning. To restart a tween after it
  finishes, `SetAutoKill(false)` before it starts, or `Restart` it from its own `OnComplete`, which keeps it running.
- `Kill` fires `OnKill` and `OnComplete` once. `ForceComplete` on a tween that has not started starts it first: it reads
  its start value and fires `OnStart` and `OnCycleStart`. `Goto` to the end completes once and `Goto(0)` applies the
  start value; a backward yoyo cycle ends at progress 0; `RepeatCall` with a count of 0 calls nothing; an empty runtime
  curve eases linearly, with a warning; time scale, delay and delta are checked.
- A callback that seeks its own sequence into itself is refused once 16 such seeks are nested, with an error in the log.
- Colour tweens and RectBlock's `*AlphaTo` tweens compute in float, round and clamp: an overshooting ease no longer
  wraps, and a value mid-tween can differ from 2.0's by 1. So can a RectBlock alpha's end value, which 2.0 truncated:
  `BodyAlphaTo(0.5)` ends at 128 where it ended at 127. A plain colour tween still ends exactly on its end value.
- Springs are held at rest by default: `SetTarget` wakes one; kill it, or `SetAutoKill(true)` before it starts, to have
  it retire at rest.
- A second tween tick helper no longer doubles tween speed. A tween whose owner is gone is dropped without callbacks.
  Layout animations and the embedded UMG widget follow the same pause and time-dilation rule as every other widget
  tween.

### Extensions

- The retainer box switches its canvas to render-target mode while it applies its settings, puts the old mode back
  after, does nothing in editor worlds, and warns once when it has no `DisplayVisual` to show what it retains. On a root
  canvas that is the whole canvas: it is then seen only through a `DisplayVisual` somewhere else.
- The static mesh visual refuses a mesh with more vertices than a section takes, with an error (it truncated its
  indices), and its bounds are where the canvas draws it.
- The render-target surface (`UDreamUIRenderTargetGeometrySource`) in StaticMesh geometry mode, with
  `bOverrideStaticMeshMaterial` on, puts its material instance into the static mesh component's slot 0 only in play. In
  the editor it no longer does: the instance was transient, so saving the level wrote null over the slot's own material,
  and nothing put it back. The editor therefore no longer shows the target on that mesh.
- A cylinder render-target surface with an arc of 0 bends by 1 degree, and a negative arc's bounds are on the side it
  bends to.
- Background pixelate hands its render target to its proxy, as blur and pixel sort do; pixel sort's `SetMaxSortPasses`
  keeps the count within 1 to 512.
- The lyrics' TTML reader stops at 256 levels of nesting, with an error.
- TextAnimation properties set at run time start, and those they replace stop; a property passes over a character that
  has no vertices.

### Servers

The UI manager does not start in a dedicated server's worlds, a play-in-editor server included
(`UDreamUIManagerWorldSubsystem::ShouldRunForNetMode`): a server builds the widget trees its level holds without a
manager, as a commandlet does, draws nothing and makes no renderer or paint rows. Code of your own that asks for the
manager there gets null, as every DreamGUI caller already expects.

### C++ of your own

- **Vertices and shaders**: the 64-byte vertex and `UV4`, the paint slot in `UV2.x` (see Rendering).
- **`FRichTextParser` and custom styles**: `FDreamUIRichTextCustomStyleItemData::ApplyToRichTextParseResult` takes a tag
  order and a style name, both defaulted, so callers compile unchanged; `FRichTextParseResult` has `PaintName`,
  `PaintOrder` and `bPaintRemoved`.
- **Player indices**: `GetUserIndex()` on a navigation scope, an action trigger and the action bar is no longer inline
  and answers the resolved index; read it instead of the `UserIndex` property, which may be -1.
- **Popups**: `UDreamUIPopupLayer::Push` returns false for a popup that closed during its own focus-on-open; check it,
  or the popup's state, after pushing. `FDreamPopupParams` has a `TabBehavior` (Cycle, CloseAndContinue) and an
  `OnClosing` callback, told as the popup starts to close while the focus is still in it. `EDreamPopupDismissReason`
  gains `Tab`, appended, for code that switches on the reason. The layer has `DismissAll(UserIndex, Reason)`, which a
  modal calls for its player, and `GetHomeSlot(Widget)`, the copy of the panel slot a lifted widget had at home.
- **Navigation**: a container of your own with `TabNavigation` Once says where Tab enters it by overriding
  `UDreamWidget::ResolveTabEntry`; a control of your own answers the shoulder buttons by implementing
  `IDreamUITabSwitchTarget` (`DreamGUIInput`, `Interaction/DreamUITabSwitchTarget.h`); `UDreamUIActionBar::GetPrompts`
  lists what the bar shows.
- **TextAnimation**: `UDreamMeshModifierTextAnimation_Property::ApplyProperty` is no longer pure virtual. It forwards to
  the new `ApplyPropertyToCharacters`, which the built-in properties override; an override of `ApplyProperty` of your
  own still works.
- **The UI manager**: `ShouldRunForNetMode(ENetMode)`; the paint rows (`GetPaintRowsTexture`, `AcquirePaintTextRow`,
  ...); `GetPaintRowsMemoryInfo` for the memory report.
- **From 2.0's changes**: `IDreamUIRendererPrimitive::DreamUI_CollectRenderData` takes an `FDreamUIPrimitiveDataArray&`;
  `UDreamWidget::MarkTransformChanged` and `UpdateObjectToWorldTransform` are gone; `UUIDropdown::CreateBlocker` (a
  virtual) is gone; `RestoreFocusForTopScope` is gone; `SetBrush_Material` is deprecated for `SetBrushFromMaterial`; a
  custom mesh modifier that moves vertices or UVs has to say so through `ModifierWillChangeVertexData`, or coverage text
  is drawn misplaced.

### Packaging

A game with colour emoji ships an emoji font of its own, and a game with CJK text packages the EFIGSCJK preset (All for
Thai, Lao, Khmer or Burmese): [FontsAndPackaging.md](FontsAndPackaging.md). The test host's packaged text smoke test
holds a packaged game's text to the same build run on uncooked content (`Tools/TestHost/README.md`).

## 1. Before you open the project

1. **Engine 5.8.** The plugin builds against 5.8 only; there is no 5.7 branch of it.
2. **Put the project under source control first,** or copy it. Opening it with the new plugin is harmless —
   redirects rewrite names in memory, not on disk — but the first save of an asset writes the new names into
   it, and there is no way back from that save to the old plugin.
3. **Take the old plugin out.** An LGUI or LexUI folder under `Plugins/` has to go before DreamGUI goes in: its
   classes would be live under the names the redirects map *from*, and a redirect never applies to a name that
   still resolves.
4. **Put the redirect block into your `Config/DefaultEngine.ini`.** 1.0.0 ships no redirects (see
   [From 2.1 to 1.0.0](#from-21-to-100)): copy the `[CoreRedirects]` section of [2.1.0's `Config/DefaultDreamGUI.ini`](https://github.com/TypeDreamMoon/DreamGUI/blob/3049561a1742ab59d83a404ee267d2f580e53589/Config/DefaultDreamGUI.ini)
   (commit `3049561a`) there, once. A copy of an older block that is already there goes first — two redirects for one
   old name with different new names are an error, and the first one registered wins.

## 2. Install

Clone into the project's `Plugins/` directory, regenerate project files, build:

```bash
git clone https://github.com/TypeDreamMoon/DreamGUI.git Plugins/DreamGUI
```

The plugin needs `EnhancedInput`, which it enables itself. Nothing else is required of the project: no engine
source build, no private engine headers, no settings to copy.

## 3. What the redirects carry

The block from 2.1.0, once it is in the project's `Config/DefaultEngine.ini`, is applied by the engine before the
first package loads. It holds 763 redirects:

| Kind | Entries | What they cover |
| --- | --- | --- |
| Package | 4 | The `/LGUI/` content mount to `/DreamGUI/`, and the `/Script/LGUI`, `/Script/LGUIEditor` and `/Script/LTween` modules to their DreamGUI names |
| Class | 395 | Every LGUI/LexUI class; the prefab vocabulary the class model replaced; the control renames (`UIButtonComponent` → `UIButton` and its thirteen siblings); every class the module split moved out of the core, from its old `/Script/DreamGUI` name to its module |
| Struct, enum | 96, 124 | The same, for data |
| Function | 74 | Functions a Blueprint calls or overrides that were renamed or moved — `UDreamUIBehaviour`'s `Update` → `Tick`, `UDreamUINavigationScope`'s events that lost their `On` |
| Object | 70 | The delegate signatures the module split moved with their classes |

**Then resave, and take the block out.** A redirect is applied every time an asset that needs it loads. Once the
project opens and the UI looks right, resave the assets that use DreamGUI (*File > Save All* after opening them, or
the `ResavePackages` commandlet over your content), so they name the current types themselves, and delete the block
from `Config/DefaultEngine.ini`: nothing needs it after that, and 1.0.0 does not keep a copy of its own.

## 4. What no longer exists

These have no redirect, because there is nothing left for one to point at. An asset that still names one of them
loses that part on load, and the engine logs a warning naming it.

- **The root Blueprints** `WorldSpaceRoot_DreamRenderer`, `WorldSpaceRoot_UERenderer`, `ScreenSpaceRoot` and
  `DreamWorldSpaceRaycasterSource_Mouse`. A level that places one drops that actor: drag the widget Blueprint
  into the level again, as a `UDreamWorldWidgetComponent` on an actor of your own or through the level
  editor's drop.
- **`UDreamWidgetPresenterComponent`.** The abstract base `UDreamWidgetPresenterComponentBase` stays;
  `UDreamWorldWidgetComponent` is what you place.
- **The `UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` family** — `UDreamWorldSpaceRaycaster`
  absorbed all of it.
- **The plugin settings** `ScreenSpaceRootClass`, `WorldSpaceRootClass`, `WorldSpaceUERendererRootClass`,
  `WorldSpaceRaycasterSourceClass`, and `bLegacyTouchPointerIds` (see 5).
- **The Lex layout family** — `ULexLayoutContainerFlexBox`, `ULexLayoutContainerGrid`, `ULexLayoutSelfFlexBox`,
  `ULexLayoutSelfGrid` and the `ELexUILayoutMode` switch. The UMG-shaped panels replace them; a tree laid out
  by one of these is laid out again by hand.
- **The prefab machinery** and its types, which the widget class model replaced before any of the above.
- **Two console variables that put earlier renderer behaviour back** — `r.DreamUI.MaterialWrappers` and
  `r.DreamUI.RTDrawer` — together with the behaviour they put back (see 5).

## 5. Behaviour to check

What an asset does can differ from what it did, with nothing renamed. Worth a look in any project that has run
an earlier version:

### Input

- **Pointer ids have ranges.** The mouse is pointer 0, a finger is 100 plus its index, and ids a script makes up
  start at 1000. A finger used to be its own index, so the first finger and the mouse were the same pointer and
  fought over hover and selection. Code that reads touch pointers by finger index asks
  `DreamUIPointerIds::ForTouch` instead; the setting that restored the old ids is gone.
- **The Enhanced Input preset binds the actions it is given, as they are.** It used to play with runtime copies of
  them, whose `bTriggerWhenPaused` it rewrote every frame. Now a paused game is decided per event: while the game
  is paused and `UDreamUISettings::bScreenSpaceUIAffectByGamePause` is set, the preset drops what arrives. For a
  paused frame's click to arrive at all, its action must trigger while paused — the shipped `IA_*` actions do; set
  `bTriggerWhenPaused` on actions of your own that the preset binds.
- **Focus and text input belong to a player.** A split screen's second player focuses and types on its own.
  `UUITextInput::GetActiveTextInputForPlayer` answers per player; `GetActiveTextInput` answers across worlds.
- **Characters reach a text field through the game viewport client.** Use `UDreamGameViewportClient`, or call
  `DreamUITextInputRouter::RouteViewportCharacter` from your own viewport client's `InputChar`, after the console
  and before the base class ([Installation](https://gui.toolchain.64hz.cn/en/docs/start/installation) on the docs site has both ways).
- **The Slate input source is optional and off.** *Project Settings > Plugins > Dream GUI > Input >
  Use Slate Input Source* hears every pointer, key and stick before the viewport, so the UI keeps working in the
  engine's own UI-only input mode; the preset actors stand down while it is on, and
  `SlateInputConsumePolicy` decides what it keeps from the game.

### Rendering

- **A canvas makes no material instance of its own.** It answers the parameters it gives a material — the main
  and font textures, its data textures — through a render-thread proxy of that material. A material instance you
  give a widget is answered for the same way and never written to, so parameters you set on it stay yours.
  Nothing a canvas draws with is an object in the level, and nothing of it is copied into a play session or a
  paste.
- **A render-target canvas draws whether or not anything renders its world.** It is drawn by a render command
  and a graph of its own after its sections change, not inside one of its world's views.
- **Background blur, pixelate and pixel sort** look as they did, with two fixes: a full-size blur on a
  multisampled canvas shows (it was lost to the canvas's resolve), and a full-size blur into an output target
  blurs the screen (it blurred an empty texture).

### Seeing it

`DreamUI.Capture` writes a PNG of the viewport and of every render-target canvas, and `DreamUI.Stats` prints
what the UI cost stage by stage — enough to compare a migrated screen with how it looked before. If something is
drawn wrong or not drawn, run with `r.DreamUI.VerifyPartialPrepare 1` and include the ensure it raises in the
report.

## 6. C++ of your own

**Modules.** The runtime is several modules now. Add to your `Build.cs` the ones whose types you use:
`DreamGUIRenderer` (drawing, effects' render proxies), `DreamGUI` (widgets, visuals, canvases), `DreamGUIInput`
(event systems, raycasters, navigation, the viewport client), `DreamGUIControls` (the `Dream*` controls and the
`UI*` behaviours), `DreamGUIExtensions` (lines, rings, effects, render-target helpers). The README's
[module table](../README.md#modules) says what is where.

**Includes.** Every header kept its path except those in the README's
[include table](https://gui.toolchain.64hz.cn/en/docs/guides/migration).

**Calls.** The README's call table lists what moved with the module split. Since then:

| Was | Is now |
| --- | --- |
| `UDreamVisual::OnMaterialInstanceDynamicCreated(UMaterialInstanceDynamic*)` | `UDreamVisual::AddMaterialParameters(FDreamUIMaterialParameters&)`: give the parameters your visual's material needs, and the canvas answers them for its proxy |
| `DreamUITextInputRouter::RouteCharacter(TCHAR)` | `RouteViewportCharacter` from a viewport client, or `RouteCharacter(WorldContext, UserIndex, Character)` |
| `UDreamGUISettings::bLegacyTouchPointerIds` | Gone; `DreamUIPointerIds::ForTouch` gives a finger's pointer id |
| `ADreamEnhancedInputEventSystemActor::GetOriginalAction` | Gone: the actions the actor holds are the ones it was given |
| `FDreamUIRenderer::IsRenderTargetDrawerEnabled` | Gone: a render-target canvas is always drawn by `DrawRenderTarget_GameThread` |
| `UDreamUIMeshComponent::OnSceneProxyCreated` | `OnRenderRootCreated`, broadcast with the mesh's render root. The root holds the canvas's sections and outlives the scene proxies made for it |
| `FDreamVisualPostProcessRenderProxy::RenderMeshOnScreen_RenderThread` taking RHI textures | Takes the graph's textures (`FRDGTextureRef`). An effect of your own reads the screen with `ReadScreen_RenderThread` and `GrabRegion_RenderThread`, works in textures from `CreateWorkTexture`, and writes back with `WriteBack_RenderThread` |

**Settings in code.** `UDreamGUISettings` is *Project Settings > Plugins > Dream GUI*; `UDreamUISettings` is
*Project Settings > Plugins > DreamUI*. Both are read where they are used, so a value changed at runtime applies
from the next frame.

## 7. When something does not come across

The automation suite that ships with the plugin loads assets saved by the plugin and holds them to what was saved
(`DreamGUI.Compatibility.*`, `DreamGUI.Assets.*`); the redirect block itself was checked by 2.1.0's suite — that every
entry reached the engine, that no old name was still a live type, that none hopped into another redirect, and that
every new name existed. An asset of yours that fails to load with the block in place is worth a report with the
warning the engine logged for it: a name DreamGUI renamed and did not redirect is a bug.
