---
title: The control library
description: What DreamGUIControls holds — the Dream* controls by kind, the UI* behaviours they are built from, the action bar, the UMG interop, and how to place a control from .dui.
---

The control library lives in the `DreamGUIControls` module, above the input system (L3). It offers two
layers:

- **The `Dream*` controls**: button, toggle, slider, lists, dialog… Each is a subclass of
  `UDreamUserWidget` whose **hierarchy is built by code**, not instanced from an asset.
- **The `UI*` behaviours**: components such as `UUIButton`, `UUIToggle` and `UUISlider` that you put on
  an ordinary widget to make it respond like a button. The controls are assembled from them.

C++ that uses these types adds `DreamGUIControls` to its `Build.cs`. Assets need nothing.

## Why a control's hierarchy is code

The plugin's `Content/Controls/` still holds a set of preset Blueprints (`BP_Button`, `BP_Toggle`,
`BP_Dropdown`, …) — the older way, where a control is an asset tree you can open and edit. What is
wrong with that is said plainly in `UDreamButton`'s header: `BP_Button` shipped for months with no
`UIButton` on it, so clicking it did nothing, and nothing anywhere said so.

A control built by code has no half-built state. `UDreamUIControl` runs the tree, the parts, the
behaviours and the look in one fixed order in `NativeOnInitialized`, and the behaviour is always one the
control added itself. The price is a tree nobody can open, so **everything an author would otherwise
reach into the hierarchy to change has to be a property** — which is what each control's `F…Style`
struct is; see [Styles and style sheets](styles.md).

## The controls

The `.dui` tag column is the node type you write in a `.dui`; the class links to its generated
reference page.

### Buttons and choices

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamButton`](../Reference/DreamButton.md) | `Native.Button` | A face and a hole in it. **It draws no text of its own**: what is on a button is whatever the host nests into the hole |
| [`UDreamToggle`](../Reference/DreamToggle.md) | `Native.Toggle` | A check box: a box and a tick. `CheckedState` has UMG's third value; `bIsOn` is its bool spelling |
| [`UDreamRadioButton`](../Reference/DreamRadioButton.md) | `Native.RadioButton` | The toggle's anatomy with radio behaviour |
| [`UDreamInputKeySelector`](../Reference/DreamInputKeySelector.md) | `Native.InputKeySelector` | A button whose label is a bound key, and which listens for the next key when clicked |

### Values and progress

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamSlider`](../Reference/DreamSlider.md) | `Native.Slider` | A track, a fill and a handle; horizontal or vertical is one property, `Direction`, not two assets |
| [`UDreamSpinBox`](../Reference/DreamSpinBox.md) | `Native.SpinBox` | `[-] value [+]`, a row of three parts: a number, a range, a step |
| [`UDreamProgressBar`](../Reference/DreamProgressBar.md) | `Native.ProgressBar` | A track and a fill, with no behaviour |
| [`UDreamThrobber`](../Reference/DreamThrobber.md) | `Native.Throbber` | N pieces that pulse, saying only "something is happening". UMG's `Throbber` and `CircularThrobber` are one class here and two palette rows |

### Text

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamTextInput`](../Reference/DreamTextInput.md) | `Native.TextInput` | A boxed text field; `bMultiLine = true` is the multi-line one. UMG's `EditableTextBox` / `MultiLineEditableTextBox` |
| [`UDreamEditableText`](../Reference/DreamEditableText.md) | `Native.EditableText` | A field with no box, UMG's `EditableText` |
| [`UDreamMultiLineEditableText`](../Reference/DreamMultiLineEditableText.md) | `Native.MultiLineEditableText` | A field with no box, over several lines |
| [`UDreamRichTextBlock`](../Reference/DreamRichTextBlock.md) | `Native.RichText` | One paragraph that reads markup, built on `UDreamText::bRichText` |

### Lists and scrolling

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamListView`](../Reference/DreamListView.md) | `Native.List` | Rows built from a source in a scrolling viewport — smaller than UMG's `ListView`: there is no entry-widget protocol to implement |
| [`UDreamTileView`](../Reference/DreamTileView.md) | `Native.TileView` | A list whose rows are laid out across as well as down |
| [`UDreamTreeView`](../Reference/DreamTreeView.md) | `Native.TreeView` | A list whose rows carry an indent and a twisty |
| [`UDreamScrollBox`](../Reference/DreamScrollBox.md) | `Native.ScrollBox` | A face, a clipped viewport, the content stack sliding inside it, and a bar |
| [`UDreamScrollBar`](../Reference/DreamScrollBar.md) | `Native.ScrollBar` | A track and a handle; one control for both orientations |

All three lists derive from [`UDreamListViewBase`](../Reference/DreamListViewBase.md), which owns the
viewport, the bar, the row template, the row pool and the selection — "a tree is a list that indents"
is literally true of the class structure.

### Containers, popups and navigation

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamBorder`](../Reference/DreamBorder.md) | `Native.Border` | A face, a padding, and a hole to put things in |
| [`UDreamExpandableArea`](../Reference/DreamExpandableArea.md) | `Native.ExpandableArea` | A clickable header and the content it hides |
| [`UDreamTabView`](../Reference/DreamTabView.md) | `Native.TabView` | A strip of tabs over a switcher of pages |
| [`UDreamDialog`](../Reference/DreamDialog.md) | `Native.Dialog` | A dimmer, a centred panel, a title, a message and a row of buttons. Opened through `UDreamUIModalSubsystem::ShowModal`, the dimming, input blocking and focus are the subsystem's |
| [`UDreamMenuAnchor`](../Reference/DreamMenuAnchor.md) | `Native.MenuAnchor` | Where a menu opens from, and what puts it away again. The menu is a popup on the per-player popup layer |
| [`UDreamDropdown`](../Reference/DreamDropdown.md) | `Native.Dropdown` | A face with a caption and a list that pops open |
| [`UDreamRingMenu`](../Reference/DreamRingMenu.md) | `Native.RingMenu` | Wedges around a hub, picked by direction |

### Interop

| Control | `.dui` tag | What it is |
| --- | --- | --- |
| [`UDreamNativeWidgetHost`](../Reference/DreamNativeWidgetHost.md) | `Native.NativeWidgetHost` | A hole in a DreamGUI hierarchy that a UMG widget fills. See [UMG interop](#umg-interop) below |

The base of every control is [`UDreamUIControl`](../Reference/DreamUIControl.md), which is abstract and
has no tag.

## Placing a control from .dui

In a `.dui` a control is a node typed by its **registry tag**: `Scope.Name`, with the plugin's controls
under `Native`.

```dui
class /Game/UI/WBP_AudioSettings

VerticalBox Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    Spacing = 12
    Padding = (48, 32, 48, 32)

    HorizontalBox {
        Spacing = 8
        Native.Toggle Mute { OnToggleChanged -> HandleMuteChanged }
        Text { Text = "Mute" }
    }
    Native.Slider Volume {
        MinValue = 0
        MaxValue = 1
        Value = 0.8
        OnValueChanged -> HandleVolumeChanged
    }
    HorizontalBox {
        Spacing = 16
        Native.Button Apply {
            Text { Text = "Apply" }
            OnClicked -> HandleApply
        }
        Native.Button Back {
            Text { Text = "Back" }
            OnClicked -> HandleBack
        }
    }
}
```

What to notice:

- **A node's lines set the control's properties**: `MinValue` and `Value` are `UDreamSlider`'s, named
  as UMG names them (see [Coming from UMG](umg-parity.md)). The style struct takes a dotted
  path: `Style.TickChecked = #FF3355`.
- **Nested content goes into the control's default hole.** The `Text { }` inside `Native.Button` is the
  button's label — the button draws none of its own. A toggle has no hole; the text beside the box is your
  own layout, which is why it sits next to a `Text` in one `HorizontalBox` above.
- **Events are routes**: `OnClicked -> HandleApply` sends the control's event to the user widget's
  function of that name, as any other event is routed.
- A class with no tag can be named by its class path: `/Script/DreamGUIControls.DreamDialog Confirm { … }`.
  The write-back (the designer editing the file) always writes the tag, though: a class path names the
  module the class lives in, and a file written with one stops reading back the day the class moves.

The tags are not built into the language. A class registers itself with one line in its `.cpp`:

```cpp
DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
```

A project plugin declares a scope of its own with the same macro (`Game.HealthBar`, say); the compiler,
the symbol export, the "unknown tag" diagnostic and the write-back all read the one table. The full order
in which a node's type is resolved is in [Nodes and values](../language/nodes-and-values.md).

## How a control assembles itself

`UDreamUIControl` runs four steps in `NativeOnInitialized`, in this order; a subclass only says what it is
through the hooks:

### The tree
Instance `Template`'s tree when there is one (`RealizeTemplate`), otherwise build the code tree
(`RealizeBuiltIn`).

### The parts
Find the parts in the tree **by name** (`BindParts`), from the one list `CollectParts` declares.

### The behaviours
Hook behaviours onto the parts found (`WireParts`). It adds what is missing rather than assuming it is
there, so a `UUIButton` the template's author never put on the face is put there anyway.

### The look
Resolve the style and push it into the parts (`ApplyStyle`), and push again whenever a knob changes.

Splitting step 1 from step 2 is what makes `Template` possible. It is WPF's ControlTemplate: the control
keeps the behaviour, the state machine and the style contract; the template decides what it looks like,
down to which nodes exist. Parts are matched by name, so a template is "a widget Blueprint with a `Face`
and a `Label` in it", not a subclass of anything and not an interface to implement. A required part the
template omits is reported once, by name; `GetUnboundRequiredParts` returns every one not found, and an
empty array is the healthy answer.

A Blueprint subclass that wants to touch the control's parts waits for **On Control Ready**
(`OnControlReady`), not On Initialized: the latter fires before the four steps, when there are no parts
yet. `FindPart` looks a part up by display name among the control's own contents.

## The UI* behaviours

A behaviour is a `UDreamUIBehaviour` component on a widget, written as a `+ ClassName { }` block in a
`.dui`. The controls are assembled from them, and you can use them directly to give a widget you drew
yourself an interaction — `+ UIButton { TransitionType = None }` is common in component files.

| Behaviour | Module | What it does |
| --- | --- | --- |
| `UUISelectable` | `DreamGUIInput` | The base of every interactive behaviour: the normal, hovered, pressed, disabled and focused states and the transitions between them, navigation, sounds |
| `UUIButton` | `DreamGUIControls` | Clicks |
| `UUIToggle`, `UUIToggleGroup` | `DreamGUIControls` | A toggle; a group of toggles where one is on |
| `UUISlider`, `UUIScrollbar` | `DreamGUIControls` | A value set by dragging |
| `UUIScrollView`, `UUIScrollViewWithScrollbar`, `UUIRecyclableScrollView` | `DreamGUIControls` | A scroll view; one with a bar; one that recycles its cells |
| `UUIListView`, `UUITileView`, `UUITreeView`, `UUIListEntry` | `DreamGUIControls` | Recycling lists, and the entry behaviour on each row |
| `UUIDropdown` | `DreamGUIControls` | A dropdown |
| `UUITextInput` | `DreamGUIControls` | Text editing: caret, selection, IME, the virtual keyboard |
| `UUITextHyperlink` | `DreamGUIControls` | A hyperlink in text |
| `UUIProgressBar` | `DreamGUIControls` | A progress bar |

`UUISelectable` sits in the input layer rather than the controls layer because navigation and focus need
it; the controls layer's behaviours derive from it or from `UDreamUIBehaviour`. The designer's palette
files these behaviours under a legacy category; normally the corresponding control is the one to use.

## The action bar

`UDreamUIActionBar` is the row of "A: Confirm  B: Back" prompts along the bottom of a screen. It **reads
the action router** rather than being told what to show, so it cannot drift out of step with what the keys
actually do — the failure of every hand-authored prompt bar, where a screen changes a binding and the hint
underneath keeps advertising the old one.

| Property | Meaning |
| --- | --- |
| `EntryClass` | The widget class loaded once per prompt, as a child of the bar. Nothing is shown without one |
| `UserIndex` | Whose prompts. `-1` (the default) is the player who owns this widget |
| `MaxEntries` | At most this many (8 by default), against a runaway table filling the screen |

It rebuilds only when the answer changes: bindings come or go, the player switches device, or picks up
another model of pad (the glyphs are per model). After the screen's own actions come two prompts, "previous
tab" and "next tab", while the player has a tab view the shoulder buttons would switch — keys taken from
the project's `PreviousTabKeys` and `NextTabKeys`. `GetPrompts` returns what the last rebuild put on the
bar, readable with no entry widget at all.

Each prompt's root carries a `UDreamUIActionBarEntry` pointing at the two or three widgets that draw it:
`LabelText` (what the action does), `IconImage` (the key glyph, hidden when the device in use has none) and
`KeyText` (the key's name, shown when there is no glyph). A more elaborate prompt overrides the Blueprint
event `OnBindingChanged`; a hold action's progress is pushed through `OnHoldProgressChanged`, which is
where a filling ring is drawn.

## UMG interop

### UMG inside DreamGUI

`Native.NativeWidgetHost` (`UDreamNativeWidgetHost`) is a hole in a DreamGUI hierarchy that a UMG widget
fills. It is built on two components that already existed: `UDreamUMGWidget` (the `UMGWidget` visual tag in
`.dui`) renders a `UUserWidget` to a render target and draws it as DreamGUI's own geometry, and
`UDreamUMGWidgetInteraction` routes pointer events into it. The control turns adding, wiring and sizing
those two into one tag.

| Property | Meaning |
| --- | --- |
| `WidgetClass` | The UMG widget class to instance and draw; null is an empty hole |
| `ResolutionScale` | How many render-target pixels one local unit is worth; 1 is pixel for pixel. A fidelity-versus-memory decision, so it is not in a style |
| `BackgroundColor` | What shows through where the hosted widget draws nothing; transparent by default |

It is **not** a way to mix UMG layout with DreamGUI layout: the hosted widget is drawn into a texture at
this control's size and knows nothing about what is around it — it cannot push the DreamGUI layout around,
and DreamGUI cannot reach inside it. That is the same bargain UMG's own world-space `WidgetComponent`
makes. It also **needs a world**: with none (a headless test, the parts of the designer that build no
world) the node exists, sized and placed, and draws nothing.

For interaction, every DreamGUI pointer over the surface is forwarded as a pointer of its own: the mouse as
Slate's cursor, each finger as a touch (which is what a UMG `ScrollBox` pans on). Several
`UDreamUMGWidgetInteraction` components may share one `VirtualUserIndex`; only one holds that simulated
cursor at a time, arbitrated by a per-world `UDreamUMGWidgetInteractionManager`.

### UMG over DreamGUI

The other direction is two UIs sharing one viewport. With the Slate input source on (*Use Slate Input
Source*), a pointer over a UMG widget drawn on top of the viewport, and keys while a UMG widget holds the
keyboard focus, are not DreamGUI's. While the player has a DreamGUI focus or Tab stop,
`UDreamGameViewportClient` takes Slate's own navigation from the bare viewport (Tab, the arrows), so Slate
does not move the focus into the UMG layers. Input as a whole is in [Input](../concepts/input.md).

## Samples

The plugin ships one small screen that runs as it is: `Content/Samples/HelloDreamGUI.dui`; how to use it
is in [Your first screen](../getting-started/first-screen.md).

The `DreamGUISamples` module (L4, above the controls) holds the **native bases** of two showcase screens:

| Class | What it is for |
| --- | --- |
| `UDreamUIShowcasePanel` | The media-console showcase's base: every variable and function its `.dui` binds against — `bMuted` for a `<->` binding, `IsMuted()` / `GetFade()` for expressions, `Tracks` and `GetHistory()` for `each` |
| `UDreamUIControlsGalleryPanel` | The controls gallery's base: one handler per input-control family, each interaction appending a line to an event log a binding shows live — coverage for click, toggle, drag, scroll, text and selection |
| `UDreamUIShowcaseDialog` | The `Native.Dialog` subclass the showcase's dialog button opens |
| `UDreamUIShowcaseTrack` | The row of data `each` iterates |

The `.dui` files and widget Blueprints of those two screens belong to the project using the plugin and do
not ship with it; `DreamGUISamples` provides only the C++ behind them.
