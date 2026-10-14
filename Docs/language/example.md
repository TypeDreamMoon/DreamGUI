---
title: "Worked example"
description: "A settings screen built from three files — one row component, a library and the screen — walked through to show which rule of the language each part relies on."
---

## A worked example

A settings screen built from one row component. Three files: the component, a library that names it and styles the
family, and the screen.

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
    + UIButton { TransitionType = None }

    Text LabelText : Caption {
        Text <- Label
        @fill
    }
    Native.Button Previous : Arrow {
        OnClicked -> emit Changed(Index, -1)
        Text { Text = "◀" }
    }
    Text ValueText : Caption {
        Text <- Value
        HAlign = Center
        @slot MinDesiredSize = (220, 0)
    }
    Native.Button Next : Arrow {
        OnClicked -> emit Changed(Index, 1)
        Text { Text = "▶" }
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
    AnchorData.SizeDelta = (0, 48)
}
style Arrow {
    AnchorData.SizeDelta = (40, 40)
}
style Column {
    + VerticalBox { Spacing = 8 }
    @fill
}
```

The screen places three rows, routes each one's `Changed` to a handler of its own, shows a hint only while there are
unsaved changes, and lists the key hints from the class's data:

```
// DUI/Settings/SettingsScreen.dui
class /Game/UI/Settings/WBP_SettingsScreen
use "Settings/SettingsLibrary.dui" as ui

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
        }

        Widget Rows : ui.Column {
            ui.Row Audio {
                Label = "Master volume"
                Value = "80 %"
                Changed -> HandleVolumeChanged
            }
            ui.Row Display {
                Label = "Display mode"
                Value = "Fullscreen"
                Index = 1
                Changed -> HandleDisplayModeChanged
            }
            ui.Row Language {
                Label = "Language"
                Value = "English"
                Index = 2
                Changed -> HandleLanguageChanged
            }
        }

        if HasUnsavedChanges() {
            Text UnsavedHint : ui.Caption {
                Text = "Unsaved changes"
                Color = @ui.Muted
            }
        }

        HorizontalBox {
            Spacing = 16
            Native.Button Apply { OnClicked -> HandleApply }
            Native.Button Back  { OnClicked -> HandleBack }
            for Hint in KeyHints {
                Text : ui.Caption { Text <- Hint.Label }
            }
        }
    }
}
```

What each part relies on:

- `use … as ui` keeps the library's names apart: `: ui.Caption`, `@ui.Panel`, and the row as `ui.Row` -- the
  library's own `use "SettingsRow.dui" as Row` came along under the namespace.
- `ui.Row Audio { … }` is an instance of the row: `Label`, `Value` and `Index` set its props -- the first values; a
  handler changes `Value` later through the row's variable, `Audio` -- and `Changed ->` routes the event the row raises
  with `emit` from its arrow buttons.
- `VerticalBox Page` and the unnamed `HorizontalBox` are containers written as node types; their `Spacing` and
  `Padding` are the containers'.
- `Widget Rows : ui.Column` gets its vertical box and its fill from the style.
- The `if` shows the hint only while `HasUnsavedChanges()` is true; nothing is created or destroyed when it flips.
- The `for` makes one text per item of the class's `KeyHints` array (a FieldNotify variable of objects with a `Label`
  member), after the two buttons, and `Text <- Hint.Label` binds each copy to its item.
- The screen's class supplies `HasUnsavedChanges`, `KeyHints`, the three `Handle…Changed(Index, Step)` handlers,
  `HandleApply` and `HandleBack`, in C++ or in the Blueprint's graph.
