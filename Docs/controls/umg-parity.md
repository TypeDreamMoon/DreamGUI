---
title: "Coming from UMG: the names are the same"
description: The structure differs and the vocabulary deliberately does not — one table per UMG class in Resources/UMGParity, adopt / map / reject, the automation suite holding them against UMG's reflection both ways, and Docs/Reference printed from reflection.
---

DreamGUI's **structure** differs from UMG's (box-first, anchors and a pivot, widgets that are `UObject`s; see
[Widgets and the tree](../concepts/widgets.md)), but its **vocabulary** deliberately does not. A control
answers to the name its UMG counterpart uses, with the meaning it has there:

- `ScrollWidgetIntoView` takes a destination and a padding;
- a scroll box has `bFrontPadScrolling` and `WheelScrollMultiplier`;
- a spin box has `MinSliderValue` and `ClearMaxValue`;
- a panel slot has `Nudge` and `bForceNewLine`;
- every widget has `SetIsEnabled`, `SetRenderShear` and a `FlowDirectionPreference`.

And **every knob the details panel can turn is one a Blueprint can turn at run time**, through a setter that
pushes the change — not by writing a field nothing reads again.

## The tables

Where a name could not be kept, that is written down rather than left to be discovered.
`Resources/UMGParity/` holds one JSON table per UMG class — **58** of them, covering **960** Blueprint-facing
members — and each row says one of three things:

| Status | Meaning | Rows |
| --- | --- | --- |
| `adopt` | Same name, same meaning | 605 |
| `map` | Here under another name, or on another type, and which | 301 |
| `reject` | Deliberately absent, with the reason | 54 |

The reasons for a `reject` are structural: there is no immediate-mode paint context to draw into; a procedural
rect has no material instance of its own. One row of `Border.json` (its `note` shortened):

```json
{"umg": "SetBrushFromMaterial", "kind": "function", "status": "reject", "dream": "",
 "note": "A procedural rect has no material of its own to set. ..."}
```

The shape of a table — `ListView.json`'s header and one of its rows:

```json
{
	"umgClass": "UListView",
	"umgHeader": "Components/ListView.h",
	"dreamClass": "UDreamListView",
	"members": [
		{"umg": "EntrySpacing", "kind": "property", "status": "map", "dream": "FDreamListStyle::RowSpacing", "note": "The gap between entries is appearance, so it lives in the list's style rather than on the control: FDreamListStyle::RowSpacing, along the list's main axis (between rows of a vertical list, between columns of a horizontal one)."}
	]
}
```

| Field | Meaning |
| --- | --- |
| `umgClass`, `umgHeader` | The UMG class and its header |
| `dreamClass` | The type it corresponds to here |
| `members[].umg` | The UMG member's name |
| `members[].kind` | `property` or `function` |
| `members[].status` | `adopt` / `map` / `reject` |
| `members[].dream` | Its name here; empty for an `adopt` under the same name, required for a `map` |
| `members[].note` | The explanation; for a `reject`, the reason |

The correspondence is not always one to one. A few worth knowing:

| UMG | Here |
| --- | --- |
| `UCheckBox` | `UDreamToggle` |
| `UComboBoxString` | `UDreamDropdown` |
| `UEditableTextBox`, `UMultiLineEditableTextBox` | `UDreamTextInput` |
| `UCircularThrobber`, `UThrobber` | `UDreamThrobber` |
| `UListView`, `UListViewBase` | `UDreamListView` |
| every `…Slot` | `UDreamPanelSlot` |
| `UPanelWidget` | `UDreamPanelLayoutBase` |
| `UWidget` | `UDreamWidget` |
| `UWidgetBlueprintLibrary` | `UDreamUIWidgetLibrary` |
| `UWidgetLayoutLibrary` | `UDreamUILayoutLibrary` |
| `USlateBlueprintLibrary` | `UDreamUIWidgetGeometryLibrary` |
| `UCanvasPanel`, `UVerticalBox` and the other panels | the `UDreamLayoutContainer…` of the same name |

A file whose name starts with an underscore is **data for the tests**, not a table: `_BlueprintWritableExceptions.json`
lists the knobs that are deliberately construction-time only, each with its reason (for instance
`UDreamUIControl::Template`: the tree is instanced once, at initialization, and a control already holding its parts
cannot be handed another without throwing away every behaviour and binding wired onto the old one).

## The suite holds them both ways

The automation suite (`DreamGUI.UMGParity.*`) holds the tables against **UMG's own reflection**:

- **From the table to UMG**: the target of an `adopt` or a `map` must resolve on `dreamClass`; a `map` must name
  its target; a `reject` must give a reason somebody could argue with; a row with no verdict is an error. This half
  catches **a rename here** — a row pointing at something this plugin no longer has.
- **From UMG to the table**: reflection lists the UMG class's own Blueprint-facing members, and every one the table
  does not mention is an error. This half catches **an engine upgrade** — a member UMG gained turns up as a row that
  does not exist.
- **`TheTablesShipWithThePlugin`**: the table directory exists and is not empty. The per-table test enumerates that
  directory, so an empty one would let it pass by running nothing — the one way a guard like this can fail unnoticed.
- **`AKnobThePanelCanTurnIsOneABlueprintCanTurn`**: a property the details panel can edit must be changeable from
  Blueprint through a setter, with exceptions only from `_BlueprintWritableExceptions.json`. A control's fields are read
  once, when it assembles itself, so a raw write to one changes a number nothing will look at again.

The suite itself is described in [The automation suite](../tools/tests.md).

## Two differences worth knowing in advance

**A new knob defaults to what the control already did, not to UMG's default**, so existing content does not move.
`ScrollWhenFocusChanges` on a scroll box is `AnimatedScroll` here and `NoScroll` in UMG: navigation here has always
revealed its target, and `NoScroll` would stop every gamepad-driven list from following the focus. The tables record
each such case:

```json
{"umg": "ScrollWhenFocusChanges", "kind": "property", "status": "adopt", "dream": "", "note": "Default AnimatedScroll, not UMG's NoScroll: navigation here has always revealed its target, and NoScroll would stop every existing gamepad-driven list from following focus."}
```

**Appearance lives in a control's style struct and behaviour on the control**, so a few UMG properties are one level
down: `EntrySpacing` is `Style.RowSpacing`, and a button's `WidgetStyle` is `Style` (resolved through `StyleSource`).
See [Styles and style sheets](styles.md).

## Docs/Reference is printed from reflection

The plugin's `Docs/Reference/` is the property and function reference, one page per class, each ending with that
class's UMG comparison (read from the tables above). This site's [Reference](../Reference/index.md) is those pages. They
are **not written by hand**; a commandlet prints them from reflection, so they cannot fall behind the headers:

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs -Out=<directory>
```

Without `-Out` it writes into the plugin's `Docs/Reference/`.

- A property's description is the comment above it in the header — one more reason to write that comment for the
  person using the control rather than the person maintaining it.
- Each property's row says whether the details panel shows it and how a Blueprint reads and writes it (the getter and
  setter names).
- The output is deterministic — members in declaration order, everything else sorted — so the diff of a regenerated
  page is the diff of the API.
