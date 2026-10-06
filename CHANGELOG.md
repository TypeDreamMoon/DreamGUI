# Changelog

What changed in each version of DreamGUI, grouped by what it changes for a project. Moving a project from one version to
the next -- what loads differently, which defaults moved, what C++ has to change -- is in
[Docs/Migration.md](Docs/Migration.md).

**1.0.0 is the first public release.** The builds before it were the fork's development line: the single-module 1.x
builds, then 2.0.0 and 2.1.0 (commits `2416e3f8` and `3049561a` on `main`). Public numbering starts again at 1.0.0, so a 2.0 or a 2.1
below is a development build and older than 1.0.0. The descriptor's integer `Version` keeps rising -- 100 for 1.0.0,
after 3 for 2.1.0 -- so the engine never takes 1.0.0 for the older of the two.

## Unreleased

### New

- **View models.** A `.dui` file declares the view models its class holds in a `viewmodels { … }` block -- by class,
  with where each comes from: the host (a creation pin, `SetViewModel`, a host's `.dui` line), `= new`, `= global` /
  `= global "Name"` from the new `UDreamViewModelSubsystem`, or `= parent` from the nearest enclosing widget. Bindings,
  routes and loops reach into them through member paths: `Text <- Player.Name`, `Value <-> Settings.MasterVolume`,
  `OnClicked += Settings.Apply()`, `OnValueChanged += Settings.SetVolume(Value)`, `for Item in Inventory.Items`. Any
  UObject class can be one; `UDreamViewModel` is the convenient base (with `DREAM_VM_SET` for C++ setters), and anything
  implementing `INotifyFieldValueChanged` -- UE's `UMVVMViewModelBase` included -- is subscribed to, with no dependency
  on the ModelViewViewModel plugin. A path whose objects are not all set is not evaluated, and the property keeps its
  value. See [View models](Docs/DuiLanguage.md#view-models).
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
- **`UDreamListViewBase::FingerDrag`** (List, Tile and Tree views): what a finger dragged along rows that can be picked up
  does. `ScrollList`, the default, scrolls the list, as UMG's does; `PickUpRow` picks the row up, as the mouse does, for a
  touch-first screen whose rows are dragged by finger. The mouse is the same either way.
- **`bMenusConsumeOutsideClick`** in the project settings (Dream GUI, Input): on, a press outside an open menu or
  dropdown list only closes it, as DreamGUI's menus and lists did before; off, the default, it closes it and goes on to
  what it landed on (see Changed).
- **`UUIButton::GetClickEventData`**: while a button's click listeners run, the pointer event the click came from --
  whose it is, which pointer, a finger or the mouse, navigation or not. The click delegates carry no arguments, and a
  C++ listener that has to tell a tap from a click asks this; null for a click raised by broadcasting the delegate.

### Changed

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
  its modifiers from the clicking player's controller. See [Migration](Docs/Migration.md#from-100-to-the-next-release).
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
  [Migration](Docs/Migration.md#from-100-to-the-next-release).
- **A finger on a slider's track moves the value only once it travels**, as `SSlider`'s touch handlers do: a finger
  that lands only notes where; past the drag distance the slider takes it -- `OnMouseCaptureBegin`, then the value under
  it -- and follows it until it lifts. A tap that never travelled changes nothing and begins no capture; it used to jump
  the value to the tap and capture at once, as the mouse does. The mouse is unchanged. See
  [Migration](Docs/Migration.md#from-100-to-the-next-release).
- **A finger dragged along a list of rows that can be dragged scrolls the list**, as `STableRow` gives a touch drag to
  its list; it used to pick up the row it landed on. The mouse still picks rows up, and `FingerDrag = PickUpRow` gives
  the finger the old behaviour. See [Migration](Docs/Migration.md#from-100-to-the-next-release).
- **A press outside an open menu goes on to what it landed on**, as the Slate menu stack lets it: a Dream Menu Anchor's
  menu, or a menu anchor panel's, closes on the press and the button under it is pressed and clicked as well. It used to
  close the menu and go no further. A press on the menu's own trigger still closes it without opening it again:
  `UDreamMenuAnchor::ShouldOpenDueToClick` says no for the click of the press that closed the menu. With a menu open,
  what is behind it is hovered as the pointer crosses it. `bMenusConsumeOutsideClick` in the project settings puts the
  old behaviour back. See [Migration](Docs/Migration.md#from-100-to-the-next-release).
- **A press outside a dropdown's open list goes on to what it landed on too**, as SComboBox's list is a menu on the same
  stack: the list closes, choosing nothing, and the button under the press is pressed and clicked. A click on the
  dropdown's own face closes the list without opening it again. The list follows `bMenusConsumeOutsideClick` with the
  menus. `UUIDropdown::bUseInteractionBlock` now means "keep the press for this dropdown whatever the project says" and
  is off by default; it was on by default, and on was the only way a list had ever behaved. See
  [Migration](Docs/Migration.md#from-100-to-the-next-release).

### Fixed

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

## 1.0.0

Everything of the 2.1.0 development build, and the `.dui` language grown into what screens are written in: components
written in `.dui` alone, `use … as`, `if`, `for`, `rows`, shorter layout; the reference for all of it; and the fixes the
first two showcase screens found. Projects on 2.1.0 open unchanged ([Docs/Migration.md](Docs/Migration.md#from-21-to-100)),
except for assets that still loaded through a redirect: the plugin ships none any more.

### Removed

- **The CoreRedirects.** `Config/DefaultDreamGUI.ini` and its 763 redirects -- from LGUI and LexUI, from the prefab
  vocabulary, from the control and event renames and from the module split -- are gone, and the engine no longer
  applies any of them to a project that mounts the plugin. The plugin's own assets and the test fixtures name the
  current types. An asset saved against an old name is resaved once with 2.1.0's block borrowed into the project's
  `Config/DefaultEngine.ini` ([Docs/Migration.md](Docs/Migration.md#from-21-to-100)). The four
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
- **The language reference.** [Docs/DuiLanguage.md](Docs/DuiLanguage.md) describes the whole of `.dui` -- file structure,
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
  [Docs/Migration.md](Docs/Migration.md#4-what-no-longer-exists).
