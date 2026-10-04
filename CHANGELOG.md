# Changelog

What changed in each version of DreamGUI, grouped by what it changes for a project. Moving a project from one version to
the next -- what loads differently, which defaults moved, what C++ has to change -- is in
[Docs/Migration.md](Docs/Migration.md).

## Unreleased

### New

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

## 2.1.0

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

- [Docs/FontsAndPackaging.md](Docs/FontsAndPackaging.md): colour emoji fonts, three ways to package a font file, the ICU
  packaging preset and what degrades without it.
- [Docs/Migration.md](Docs/Migration.md) has a section for moving from 2.0 to 2.1.
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
[Docs/Migration.md, "From 2.0 to 2.1"](Docs/Migration.md#from-20-to-21).

## 2.0.0

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
  [Docs/Migration.md](Docs/Migration.md#4-what-no-longer-exists).
