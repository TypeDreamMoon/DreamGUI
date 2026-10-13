---
title: Styles and style sheets
description: Each control's F…Style struct, StyleSource and where a look comes from, the project style sheet UDreamUIStyleSheet with its named variants, and the face brushes, state faces and image brushes.
---

A control's tree is built by code and nobody can open it, so every appearance decision has to be a knob or
it is a fork. Those knobs live together in each control's **style struct** — the shape of Slate's
`FButtonStyle`, there for the same reason.

The split is fixed: **appearance is in the style, behaviour is on the control.** A button's colours, corner
radius and padding are fields of `FDreamButtonStyle`; when it counts as clicked (`ClickMethod`) and which
mouse buttons it answers (`AcceptedMouseButtons`) are properties of `UDreamButton` itself. That is why a few
UMG properties are one level down here: the gap between list entries is appearance, so UMG's `EntrySpacing`
is `Style.RowSpacing` (see [Coming from UMG](umg-parity.md)).

**Note:**

  This page is about the controls' style structs. A `style Name { … }` block in a `.dui` is something else —
  a bag of property lines applied to a node by name; see [Styles and resources](../language/styles-and-resources.md).

## Each control's style struct

Every control has a `Style` property of its family's struct; the project style sheet holds one default and
one table of named variants per family.

| Control | Style struct | Fields on the sheet |
| --- | --- | --- |
| `UDreamButton` | [`FDreamButtonStyle`](../Reference/DreamButtonStyle.md) | `Button`, `ButtonVariants` |
| `UDreamToggle` | [`FDreamToggleStyle`](../Reference/DreamToggleStyle.md) | `Toggle`, `ToggleVariants` |
| `UDreamRadioButton` | [`FDreamRadioButtonStyle`](../Reference/DreamRadioButtonStyle.md) | `RadioButton`, `RadioButtonVariants` |
| `UDreamInputKeySelector` | [`FDreamInputKeySelectorStyle`](../Reference/DreamInputKeySelectorStyle.md) | `InputKeySelector`, `InputKeySelectorVariants` |
| `UDreamSlider` | [`FDreamSliderStyle`](../Reference/DreamSliderStyle.md) | `Slider`, `SliderVariants` |
| `UDreamSpinBox` | [`FDreamSpinBoxStyle`](../Reference/DreamSpinBoxStyle.md) | `SpinBox`, `SpinBoxVariants` |
| `UDreamProgressBar` | [`FDreamProgressBarStyle`](../Reference/DreamProgressBarStyle.md) | `ProgressBar`, `ProgressBarVariants` |
| `UDreamThrobber` | [`FDreamThrobberStyle`](../Reference/DreamThrobberStyle.md) | `Throbber`, `ThrobberVariants` |
| `UDreamTextInput`, `UDreamEditableText`, `UDreamMultiLineEditableText` | [`FDreamTextInputStyle`](../Reference/DreamTextInputStyle.md) | `TextInput`, `TextInputVariants` |
| `UDreamRichTextBlock` | [`FDreamRichTextStyle`](../Reference/DreamRichTextStyle.md) | `RichText`, `RichTextVariants` |
| `UDreamListView` | [`FDreamListStyle`](../Reference/DreamListStyle.md) | `List`, `ListVariants` |
| `UDreamTileView` | [`FDreamTileViewStyle`](../Reference/DreamTileViewStyle.md) | `TileView`, `TileViewVariants` |
| `UDreamTreeView` | [`FDreamTreeViewStyle`](../Reference/DreamTreeViewStyle.md) | `TreeView`, `TreeViewVariants` |
| `UDreamScrollBox` | [`FDreamScrollBoxStyle`](../Reference/DreamScrollBoxStyle.md) | `ScrollBox`, `ScrollBoxVariants` |
| `UDreamScrollBar` | [`FDreamScrollBarStyle`](../Reference/DreamScrollBarStyle.md) | `ScrollBar`, `ScrollBarVariants` |
| `UDreamBorder` | [`FDreamBorderStyle`](../Reference/DreamBorderStyle.md) | `Border`, `BorderVariants` |
| `UDreamExpandableArea` | [`FDreamExpandableAreaStyle`](../Reference/DreamExpandableAreaStyle.md) | `ExpandableArea`, `ExpandableAreaVariants` |
| `UDreamTabView` | [`FDreamTabViewStyle`](../Reference/DreamTabViewStyle.md) | `TabView`, `TabViewVariants` |
| `UDreamDialog` | [`FDreamDialogStyle`](../Reference/DreamDialogStyle.md) | `Dialog`, `DialogVariants` |
| `UDreamMenuAnchor` | [`FDreamMenuAnchorStyle`](../Reference/DreamMenuAnchorStyle.md) | `MenuAnchor`, `MenuAnchorVariants` |
| `UDreamDropdown` | [`FDreamDropdownStyle`](../Reference/DreamDropdownStyle.md) | `Dropdown`, `DropdownVariants` |
| `UDreamRingMenu` | [`FDreamRingMenuStyle`](../Reference/DreamRingMenuStyle.md) | `RingMenu`, `RingMenuVariants` |

`UDreamNativeWidgetHost` has no style: its one knob, `ResolutionScale`, is a fidelity-versus-memory decision
about one hosted widget, and a project sheet has nothing useful to say about it.

A few conventions hold across all of them:

- **Colours are `FColor`**, throughout the style family.
- **A button has no text style.** A button draws no text; the label is whatever the host puts in the hole,
  so `FDreamButtonStyle` has no `LabelColor` or `FontSize` — a knob with nowhere to write is worse than no
  knob. Where a control does supply a label, its own style says so (`FDreamDialogStyle::ButtonLabelColor`).
- **Disabled and focused are style states too.** Pushing a focus colour is what switches the selectable's
  focus visuals on, so a pad user's navigation always lands on something that shows it.

## Where the look comes from: StyleSource

Two properties on every control decide which style is in effect:

| Property | Meaning |
| --- | --- |
| `StyleSource` | `ProjectStyleSheet` (the default): resolve from the project sheet; `ProjectStyleSheetOverride`: the sheet's values with the fields this instance **ticked** written over them; `Inline`: this instance's own `Style`, whole |
| `StyleVariant` | A named entry in the sheet ("Danger", "Compact"); none means the family default. Only meaningful while resolving from the sheet |

The sheet is the default because that is the point of having one: a code-built control has no tree anyone
could open and recolour, so without a project-wide answer every instance is a hand-tuned fork.

**With no sheet in the project**, the instance's own `Style` is the look in effect, whatever `StyleSource`
says. That is also why `Style` stays editable rather than being greyed by `StyleSource`.

### Per-field override bits

Every field of every style struct has a `bOverride_<Field>` bit beside it — UE's own idiom, the one
`FPostProcessSettings` uses: a checkbox beside the value in the details panel, the value greyed when it is
not in effect (but still shown — it is what this instance would push if ticked). The merges read only those
bits:

- **A variant** is the family default with the variant's ticked fields written over it. An unticked field
  means "whatever the family default says" — inheritance with no Parent pointer and no cycle to check.
- **`ProjectStyleSheetOverride`** is the style the sheet resolves to, with this instance's ticked fields
  written over it.

**Note:**

  Every bit **starts ticked**, which is what keeps existing content byte for byte: a variant with every bit
  ticked is the full fork it always was. The other side of that: switching to `ProjectStyleSheetOverride`
  without unticking anything gives the same result as `Inline`. Untick each field that should follow the sheet.

### In .dui and at run time

```dui
class /Game/UI/WBP_DeleteSave

HorizontalBox Root {
    Spacing = 16

    Native.Button Delete {
        StyleVariant = "Danger"
        Text { Text = "Delete" }
        OnClicked -> HandleDelete
    }
    Native.Toggle AutoSave {
        StyleSource = Inline
        Style.TickChecked = #FF3355
    }
}
```

At run time, `SetStyle`, `SetStyleSource` and `SetStyleVariant` **push again at once**: a style that changed
source without re-resolving would have no visible effect until something else happened to push. After
editing a style struct in place, call `ApplyStyle` yourself.

Note that `SetStyle` writes the instance's `Style`: while the sheet is driving, writing an inline style changes
nothing you can see. The push still runs, but `StyleSource` still decides which one wins.

## The project style sheet

`UDreamUIStyleSheet` is a data asset: one place where the project's controls agree on how to look. Per family
it holds the default plus named variants, which is the whole of what a control needs to say about itself —
which family, and optionally which name.

### Make a sheet
Create a data asset in the Content Browser and pick `DreamUIStyleSheet` as its class.

### Point the project at it
In *Project Settings ▸ Plugins ▸ Dream GUI*, category Assets, set **Default Style Sheet** to it. The setting
is `UDreamGUISettings::DefaultStyleSheet`.

### Pick variants on controls
Controls resolve from the sheet by default; an instance that should look special sets `StyleVariant`.

Worth knowing:

- **The plugin ships no sheet.** A project without one is a supported state, not a missing piece: the structs'
  own C++ defaults are the built-in theme.
- **`DefaultStyleSheet` is a `TSoftObjectPtr<UDataAsset>`**, because the core module does not name the control
  library's types; the picker lists style sheets only, and `UDreamUIStyleSheet::GetProjectSheet()` does the cast.
  It loads the sheet the first time it is asked, and returns null when none is configured, which callers read
  as "use the inline defaults", not as an error.
- **Resolution happens at construction**, by name, as a Slate style set's does. There is no run-time re-theme,
  and none intended: what this buys is that changing the project's accent colour is one edit to one asset rather
  than a visit to every screen that ever placed a toggle.
- **A misspelled variant falls back to the family default** rather than failing: something visible is on
  screen, and it is the right thing nine times out of ten.

## Face brushes, state faces and image brushes

### FDreamUIFaceBrush: the skin of a control's face

Every control's face is a procedural rounded rect (`UDreamRectBlock`); that is where most of the UMG feel
lives. [`FDreamUIFaceBrush`](../Reference/DreamUIFaceBrush.md) is an optional skin for it: empty, the face
stays the plain rect; given an image, the face draws it **inside the same silhouette** — the corner radius, the
border and the selectable's tint all keep working, because the skin is the rect's body texture, not a
different visual. A control's face never stops being a rect.

| Field | Meaning |
| --- | --- |
| `Image` | A texture or an atlas sprite, whichever is dropped in |
| `Tint` | Multiplied over the image, Slate's tint. A separate channel from the style's state colours; the two multiply |
| `ScaleMode` | `Stretch` / `FitIn` / `Envelop`, UMG's Stretch / ScaleToFit / ScaleToFill |
| `DrawMode` | `Image` / `Box` / `Border`, Slate's DrawAs. The last two are nine-slice, for a plain texture only; a sprite always draws as `Image` |
| `Margin` | The nine-slice edges in **texture pixels**: a border eight pixels wide in the file stays eight pixels wide at any rect size |
| `ImageSize` | The image's own drawn size. Zero means no opinion; non-zero wins over the style's size on the parts that have one (the toggle's box and mark, the radio's box and dot, the slider's handle) |

### FDreamUIStateFaces: one face drawn five ways

A style could tell the five states apart by colour alone, so a project whose normal and pressed buttons are
**different drawings** had to fork the control. [`FDreamUIStateFaces`](../Reference/DreamUIStateFaces.md)
closes that gap without a second state machine: the selectable already decides which state the control is
in, and this is only what that decision paints.

| Field | Meaning |
| --- | --- |
| `Normal`, `Hovered`, `Pressed`, `Disabled`, `Focused` | One face brush per state. A state with no image falls back to `Normal`, and a `Normal` with none to the control's own single brush |
| `bTintForeground` + the five `…Foreground` colours | What the control's **own** foreground part — its own label or glyph, never a host's content — is tinted per state. Off by default |
| `bUsePressedPadding` + `PressedPadding` | The padding around the content while pressed, the classic pushed-in look. The resting padding is the control's own `ContentPadding` |
| `HoveredSound`, `PressedSound`, `ClickedSound` | The three moments a control makes a noise, as a style decision: a click should sound like the other clicks in this UI |

**Every field is optional**, which is what makes it change nothing: an empty group draws exactly what was drawn
before, down to the pixel. It is a **field** of the button, toggle, dropdown-face and list-row styles rather
than a base struct, because a USTRUCT hierarchy would not survive the override-bit merge.

Focus and hover are different questions — focus survives the pointer leaving, and on a pad there is no pointer
at all — so `Focused` has a cell of its own.

### FDreamUIImageBrush: the Image visual's brush

[`FDreamUIImageBrush`](../Reference/DreamUIImageBrush.md) is the brush of the core module's `Image` visual
(`UDreamImage::Brush`), also used by `UUIToggle`, `UUIDropdown` and `UDreamSelectableStyle`. It is not a
control face, and draws more ways:

| Field | Meaning |
| --- | --- |
| `ResourceObject` | A texture, a sprite or a material |
| `TintColor` | The colour |
| `DrawAs` | `None`, `Box`, `Border`, `Image`, `Tiled` (repeat at its own size), `Filled` (show part of it, as a progress bar does), `RoundedBox` (a rounded outline) |
| `ImageSize`, `Margin`, `UVRegion`, `PixelsPerUnitMultiplier` | Size, a 0-to-1 margin, the UV region, pixel density |
| `FillAmount`, `FillMethod`, `FillOrigin`, `FillDirectionFlip` | `Filled` only: how much is shown, the shape of the cut, which end it starts from, the other direction |
| `CornerRadius`, `CornerSegments` | `RoundedBox` only: each corner's radius in pixels, clockwise from the top left, and points per arc |

`RoundedBox` is an outline cut out of geometry: its corners are as smooth as the segment count and do not
antialias themselves. For an exact, resolution-independent rounded rect with a border or gradients, use a
`RectBlock`.

## Two things beside them

- **`UDreamSelectableStyle`**: a data asset of the input layer, set on a behaviour (`UUISelectable::Style`) — a
  reusable set of state colours, state brushes, an animation duration and sounds. When a selectable carries one,
  it wins over the colours and sounds a control pushes.
- **[`FDreamTextStyle`](../Reference/DreamTextStyle.md)**: a text's outline, drop shadow and glow, one struct
  for the whole library; `FDreamRichTextStyle` carries it rather than defining a second copy.

When what should change is not a colour but **the whole tree**, use the control's `Template` property (in the
Style category): a widget Blueprint with the right part names in it, whose tree the control then uses while
keeping its behaviour and style contract. See
[How a control assembles itself](index.md#how-a-control-assembles-itself).
