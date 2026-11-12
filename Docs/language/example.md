---
title: "Worked example"
description: "A settings screen built from three files — one row component, a library and the screen — that compiles and lays out as written, walked through to show which rule of the language each part relies on."
---

## A worked example

A settings screen built from one row component. Three files: the component, a library that names it and styles the
family, and the screen. Copied as they are into a project's `DUI/` folder, they compile and lay out with no error and no
warning: the screen needs nothing from C++ or a Blueprint graph.

The row -- a label on the left, a value with arrows on the right -- declares what a host sets and hears:

```
// DUI/Settings/SettingsRow.dui
class /Game/UI/Settings/WBP_SettingsRow
use "Settings/SettingsLibrary.dui"

props {
    Text   Label
    Text   Value
    Number Index = 0
}
events {
    Changed(Number Index, Number Step)
}

Widget Root : RowBox {
    + HorizontalBox { Padding = (16, 0, 16, 0)  Spacing = 12 }

    Text LabelText : Caption {
        Text <- Label
        HAlign = Left
        @fill
    }
    Native.Button Previous : Arrow {
        OnClicked -> emit Changed(Index, -1)
        Text { Text = "<" }
    }
    Text ValueText : Caption {
        Text <- Value
        @slot MinDesiredSize = (220, 0)
    }
    Native.Button Next : Arrow {
        OnClicked -> emit Changed(Index, 1)
        Text { Text = ">" }
    }
}
```

The library holds the family's look and its names. Because it has no root it can be used plainly or under a namespace,
and its `use … as` line travels with it:

```
// DUI/Settings/SettingsLibrary.dui
use "Settings/SettingsRow.dui" as Row

resources {
    Color Ink   = #E6E9F0
    Color Muted = #8C93A6
    Color Panel = #1B1E26
}

style Caption {
    FontSize = 18
    Color    = @Ink
}
style RowBox {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
}
style RowSlot {
    @slot HorizontalAlignment = Fill
    @slot MinDesiredSize      = (0, 48)
}
style Arrow {
    AnchorData.SizeDelta = (40, 40)
    @slot MinDesiredSize = (40, 40)
}
style Column {
    + VerticalBox { Spacing = 8 }
    @fill
}
```

The screen places three rows and turns each one's `Changed` into an event of its own that says which row it was, shows
a hint while there are unsaved changes, and raises `Applied` and `Closed` from its buttons for the code that opened it:

```
// DUI/Settings/SettingsScreen.dui
class /Game/UI/Settings/WBP_SettingsScreen
use "Settings/SettingsLibrary.dui" as ui

props {
    Bool Unsaved = true
}
events {
    SettingChanged(Number Index)
    Applied()
    Closed()
}

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay { }

    Image Backdrop {
        Brush.TintColor = @ui.Panel
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
    }

    VerticalBox Page {
        Spacing = 24
        Padding = (64, 48, 64, 48)
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }

        Text Heading : ui.Caption {
            Text = "Settings"
            FontSize = 32
            HAlign = Left
        }

        Widget Rows : ui.Column {
            ui.Row Audio : ui.RowSlot {
                Label = "Master volume"
                Value = "80 %"
                Changed -> emit SettingChanged(0)
            }
            ui.Row Display : ui.RowSlot {
                Label = "Display mode"
                Value = "Fullscreen"
                Index = 1
                Changed -> emit SettingChanged(1)
            }
            ui.Row Language : ui.RowSlot {
                Label = "Language"
                Value = "English"
                Index = 2
                Changed -> emit SettingChanged(2)
            }
        }

        if Unsaved {
            Text UnsavedHint : ui.Caption {
                Text = "Unsaved changes"
                Color = @ui.Muted
                HAlign = Left
            }
        }

        HorizontalBox Buttons {
            Spacing = 16
            Native.Button Apply {
                OnClicked -> emit Applied()
                Text { Text = "Apply" }
            }
            Native.Button Back {
                OnClicked -> emit Closed()
                Text { Text = "Back" }
            }
        }
    }
}
```

What each part relies on:

- `use … as ui` keeps the library's names apart: `: ui.Caption`, `@ui.Panel`, and the row as `ui.Row` -- the
  library's own `use "SettingsRow.dui" as Row` came along under the namespace.
- `ui.Row Audio { … }` is an instance of the row: `Label`, `Value` and `Index` set its props -- the first values; code
  changes `Value` later through the row's variable, `Audio` -- and `Changed ->` routes the event the row raises with
  `emit` from its arrow buttons. Here the screen raises one of its own in turn: the arguments of an `emit` are
  expressions over the screen's class (the event's own arguments are not among them), so each row passes its number. A
  screen whose class has a function for it writes `Changed -> HandleVolumeChanged` instead (the class must have that
  function, or the compile says DUI6004).
- `: ui.RowSlot` on each row, and `RowBox` on the row's root: a vertical box gives a child the width the child
  measures, so the instance asks to fill the column and to be 48 high, and the row's root stretches over the instance.
  Without them the row is as wide as its root, which is 0, and every label stands one letter to a line.
- `VerticalBox Page`, `HorizontalBox Buttons` and the row's `+ HorizontalBox` are boxes; their `Spacing` and `Padding`
  are the boxes'. `Widget Rows : ui.Column` gets its vertical box and its fill from the style.
- `if Unsaved` shows the hint only while the prop is true; nothing is created or destroyed when it flips. The condition
  can be any binding over the class -- `if HasUnsavedChanges()` when the class has that function.
- A button draws no label of its own: the `Text` inside each one is its label.
- A `for` over an array of the class makes one node per item; see [`if` and `for`](control-flow.md).
