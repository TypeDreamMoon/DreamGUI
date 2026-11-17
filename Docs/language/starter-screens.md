---
title: "Starter screens"
description: "Five complete .dui screens to start from -- a title menu, an inventory, a field HUD, a quest journal and a results screen -- built from the presets and the building blocks, laid out on a 1920 x 1080 design canvas that fits every viewport."
---

## Starter screens

Five whole screens of different kinds. Each is one file built from the [building blocks](building-blocks.md) and worn
in a [preset](presets.md) through `DUI/Theme.dui`: copy those files with it. Each lays out with no problem at
1920 x 1080, 1280 x 720 and 2560 x 1440 under all four presets. The text is placeholder text written to read like a
game's; change it, and the values, to your own.

Start from the screen nearest to what you need and change it; do not start from an empty file. What they have in common
is what makes them look finished:

- **One design canvas.** Each screen is laid out once in 1920 x 1080 design pixels on the canvas of
  [the root of a screen](building-blocks.md#the-root-of-a-screen); the backdrop alone is outside it.
- **A margin, a grid and a balance.** Groups keep 112 px from the canvas's left and right edges and line up with one
  another; a panel's empty space goes to the end of a list or above its buttons, not between unrelated things.
- **Three steps of text.** A title or figure in `Ink`, running text in `InkSoft`, labels and small print in
  `InkFaint` or as an `Overline`. The accent marks one thing at a time: the selected line, the primary button.
- **Depth without noise.** Panels sit a step above the backdrop with a hairline edge and a soft shadow, cards a step
  above their panel, wells a step below. A glow goes behind a figure or an icon, never over text.
- **3D where it helps.** The title menu's save card and the results screen's rank card turn toward the middle of the
  screen (`RenderRotation`, Yaw); everything that is read for long stays flat.

### Title menu

The game's name, a menu with the selected line marked, and the save that *Continue* resumes on a card turned toward the
menu.

```
// DUI/Starter/TitleMenu.dui
class /Game/UI/Starter/WBP_TitleMenu
use "Theme.dui" as ui
use "Blocks/MenuItem.dui" as MenuItem
use "Blocks/KeyHint.dui" as KeyHint

style Line {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 60) }
}
style Fact {
    + VerticalBox { Spacing = 2 }
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // A wide soft light behind the save card. It sits under the card and reaches no text.
            RectBlock Light : ui.Glow {
                AnchorData.AnchorMin = (1, 0.5)
                AnchorData.AnchorMax = (1, 0.5)
                AnchorData.AnchoredPosition = (-500, 20)
                AnchorData.SizeDelta = (900, 900)
                RenderOpacity = 0.7
            }

            // ---------------------------------------------------------------- the title and the menu, on the left
            VerticalBox Column {
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (160, 10)
                AnchorData.SizeDelta = (560, 720)
                Spacing = 12

                Text Tagline : ui.Overline { Text = "A tale of the Saltmarsh";  @slot HorizontalAlignment = Fill }
                Text Name : ui.Display { Text = "Hollow Tide";  FontSize = 96;  @slot HorizontalAlignment = Fill }
                Text Pitch : ui.Lead {
                    Text = "The sea took the village. The bell still rings."
                    Color = @ui.InkSoft
                    @slot { HorizontalAlignment = Fill  Padding = (0, 0, 0, 40) }
                }
                MenuItem Continue : Line { Label = "Continue";  Note = "Chapter III";  Selected = true }
                MenuItem NewJourney : Line { Label = "New journey" }
                MenuItem Load : Line { Label = "Load";  Note = "6 saves" }
                MenuItem Settings : Line { Label = "Settings" }
                MenuItem Quit : Line { Label = "Quit" }
            }

            // ---------------------------------------------------------------- the save the selected line resumes,
            // turned toward the menu
            Widget Save {
                AnchorData.AnchorMin = (1, 0.5)
                AnchorData.AnchorMax = (1, 0.5)
                AnchorData.Pivot = (1, 0.5)
                AnchorData.AnchoredPosition = (-200, 20)
                AnchorData.SizeDelta = (600, 360)
                RenderRotation = (0, -14, 0)
                + Overlay { }
                RectBlock SaveFace : ui.Panel : ui.FillSlot
                RectBlock SaveEdge : ui.AccentFade {
                    @slot { HorizontalAlignment = Fill  VerticalAlignment = Top  Padding = (16, 0, 16, 0)  Size = (0, 2) }
                }
                VerticalBox SaveColumn : ui.FillSlot {
                    Padding = (36, 32, 36, 32)
                    Spacing = 10
                    Text SaveKicker : ui.Overline { Text = "Continue";  Color = @ui.Accent;  @slot HorizontalAlignment = Fill }
                    Text Chapter : ui.Title { Text = "III. The Drowned Bell";  @slot HorizontalAlignment = Fill }
                    Text Place : ui.Body { Text = "Saltmarsh, east of the ferry";  @slot HorizontalAlignment = Fill }
                    RectBlock SaveRule : ui.Divider { @slot { HorizontalAlignment = Fill  Padding = (0, 12, 0, 12)  Size = (0, 2) } }
                    HorizontalBox Facts {
                        Spacing = 48
                        Widget Level : Fact {
                            Text LevelLabel : ui.Overline { Text = "Level" }
                            Text LevelValue : ui.Figure { Text = "14" }
                        }
                        Widget Played : Fact {
                            Text PlayedLabel : ui.Overline { Text = "Played" }
                            Text PlayedValue : ui.Figure { Text = "12 h 40 m" }
                        }
                        Widget Saved : Fact {
                            Text SavedLabel : ui.Overline { Text = "Saved" }
                            Text SavedValue : ui.Figure { Text = "2 h ago" }
                        }
                    }
                    Widget Spacer { AnchorData.SizeDelta = (0, 0);  @fill }
                    HorizontalBox ProgressLine {
                        Spacing = 12
                        @slot HorizontalAlignment = Fill
                        Text ProgressLabel : ui.Caption { Text = "Chapter progress";  @fill }
                        Text ProgressValue : ui.Caption { Text = "64 %";  Color = @ui.Ink;  HAlign = Right }
                    }
                    Native.ProgressBar Progress : ui.Meter {
                        Percent = 0.64
                        @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 6) }
                    }
                }
            }

            // ---------------------------------------------------------------- the corners
            Text Version : ui.Caption {
                Text = "Version 0.9.2" @noloc
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 0)
                AnchorData.Pivot = (0, 0)
                AnchorData.AnchoredPosition = (160, 52)
                AnchorData.SizeDelta = (300, 24)
            }
            HorizontalBox Hints {
                Spacing = 40
                AnchorData.AnchorMin = (1, 0)
                AnchorData.AnchorMax = (1, 0)
                AnchorData.Pivot = (1, 0)
                AnchorData.AnchoredPosition = (-160, 46)
                AnchorData.SizeDelta = (320, 36)
                KeyHint Pick { Key = "Enter";  Action = "Select";  @slot Size = (150, 36) }
                KeyHint Move { Key = "W S";  Action = "Move";  @slot Size = (130, 36) }
            }
        }
    }
}
```

### Inventory

Tabs and a purse along the top, a six-by-five grid of slots (the filled ones written as `rows` tables), and the picked
item's panel: its picture, its numbers, its stats against what is equipped, its traits and its actions.

```
// DUI/Starter/Inventory.dui
class /Game/UI/Starter/WBP_Inventory
use "Theme.dui" as ui
use "Blocks/Panel.dui" as Panel
use "Blocks/Tab.dui" as Tab
use "Blocks/StatBar.dui" as StatBar
use "Blocks/ItemSlot.dui" as ItemSlot
use "Blocks/Icon.dui" as Icon
use "Blocks/KeyHint.dui" as KeyHint

style Cell {
    @slot Size = (104, 104)
}
style EmptyCell : Cell {
    Empty = true
}
style GridRow {
    + HorizontalBox { Spacing = 12 }
}
style StatRow {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 52) }
}
style ChipCell {
    AnchorData.SizeDelta = (0, 0)
    @slot MinDesiredSize = (0, 32)
}
style TabCell {
    @slot Size = (132, 44)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // ------------------------------------------------------------------ the header: title, tabs, purse
            Widget Header {
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (1, 1)
                AnchorData.Pivot = (0.5, 1)
                AnchorData.AnchoredPosition = (0, -56)
                AnchorData.SizeDelta = (-224, 96)
                + HorizontalBox { Spacing = 48 }
                VerticalBox Titles {
                    Spacing = 4
                    @slot VerticalAlignment = Center
                    Text Section : ui.Overline { Text = "Inventory" }
                    Text Heading : ui.Heading { Text = "Pack" }
                }
                HorizontalBox Tabs {
                    Spacing = 4
                    @fill
                    @slot VerticalAlignment = Center
                    Tab All : TabCell { Label = "All";  Selected = true }
                    Tab Weapons : TabCell { Label = "Weapons" }
                    Tab Armour : TabCell { Label = "Armour" }
                    Tab Supplies : TabCell { Label = "Supplies" }
                    Tab Materials : TabCell { Label = "Materials" }
                }
                HorizontalBox Purse {
                    Spacing = 10
                    @slot VerticalAlignment = Center
                    Icon PurseIcon { Kind = 5;  @slot { VerticalAlignment = Center  Size = (32, 32) } }
                    Text PurseText : ui.Figure { Text = "1,240";  FontSize = @ui.SizeLead;  @slot VerticalAlignment = Center }
                    RectBlock PurseRule : ui.Divider { @slot { VerticalAlignment = Center  Padding = (14, 0, 14, 0)  Size = (2, 28) } }
                    Text Load : ui.Body { Text = "38 / 60 kg";  @slot VerticalAlignment = Center }
                }
            }

            // ------------------------------------------------------------------ the grid: six by four, in a well
            Widget Grid {
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (112, -24)
                AnchorData.SizeDelta = (748, 720)
                + Overlay { }
                RectBlock GridFace : ui.Panel : ui.FillSlot
                VerticalBox Rows : ui.FillSlot {
                    Padding = (32, 28, 32, 32)
                    Spacing = 12
                    HorizontalBox GridHeader {
                        @slot { HorizontalAlignment = Fill  Padding = (0, 0, 0, 8) }
                        Text Slots : ui.Overline { Text = "14 of 30 slots";  @fill }
                        Text Sorting : ui.Caption { Text = "Sorted by kind";  HAlign = Right }
                    }
                    // One row of the grid per box; `rows` writes a row's filled slots as a table.
                    Widget Row1 : GridRow {
                        rows ItemSlot : Cell (Glyph, Rarity, Count) {
                            0, 2, "" { Selected = true };  1, 1, "";  2, 0, "x3";  3, 3, "";  4, 2, "";  5, 0, "1,240"
                        }
                    }
                    Widget Row2 : GridRow {
                        rows ItemSlot : Cell (Glyph, Rarity, Count) {
                            6, 1, "";  7, 4, "";  2, 1, "x12";  0, 0, "";  1, 0, "";  4, 1, ""
                        }
                    }
                    Widget Row3 : GridRow {
                        rows ItemSlot : Cell (Glyph, Rarity, Count) {
                            3, 1, "x4";  1, 2, ""
                        }
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                    }
                    Widget Row4 : GridRow {
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                    }
                    Widget Row5 : GridRow {
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                        ItemSlot : EmptyCell
                    }
                }
            }

            // ------------------------------------------------------------------ the detail of the picked item
            Panel Detail {
                Kicker = "Rare weapon"
                Heading = "Ashwood Longbow"
                AnchorData.AnchorMin = (1, 0.5)
                AnchorData.AnchorMax = (1, 0.5)
                AnchorData.Pivot = (1, 0.5)
                AnchorData.AnchoredPosition = (-112, -24)
                AnchorData.SizeDelta = (880, 720)

                HorizontalBox Showcase {
                    Spacing = 28
                    @slot HorizontalAlignment = Fill
                    Widget Art {
                        + Overlay { }
                        @slot Size = (200, 200)
                        RectBlock ArtWell : ui.Well : ui.FillSlot
                        RectBlock Halo : ui.Glow { @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Size = (190, 190) } }
                        Icon ArtIcon { Kind = 0;  @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Size = (120, 120) } }
                    }
                    VerticalBox About {
                        Spacing = 14
                        @fill
                        Text Blurb : ui.Body {
                            Text = "Drawn from a single stave of seasoned ash. Light enough to carry all day, stiff enough to drop a boar at sixty paces."
                            bAutoWrapText = true
                            OverflowType = VerticalOverflow
                            @slot HorizontalAlignment = Fill
                        }
                        HorizontalBox Facts {
                            Spacing = 32
                            VerticalBox Hit {
                                Spacing = 2
                                Text DamageLabel : ui.Overline { Text = "Damage" }
                                Text DamageValue : ui.Figure { Text = "42-58" }
                            }
                            VerticalBox Mass {
                                Spacing = 2
                                Text WeightLabel : ui.Overline { Text = "Weight" }
                                Text WeightValue : ui.Figure { Text = "2.4 kg" }
                            }
                            VerticalBox Price {
                                Spacing = 2
                                Text ValueLabel : ui.Overline { Text = "Value" }
                                Text ValueValue : ui.Figure { Text = "310" }
                            }
                        }
                    }
                }
                StatBar Draw : StatRow { Label = "Draw weight";  Value = "48";  Gain = "+6";  Percent = 0.64 }
                StatBar Reach : StatRow { Label = "Range";  Value = "62 m";  Percent = 0.78 }
                StatBar Handling : StatRow { Label = "Handling";  Value = "31";  Loss = "-4";  Percent = 0.31 }
                VerticalBox Traits {
                    Spacing = 10
                    @slot { HorizontalAlignment = Fill  Padding = (0, 8, 0, 0) }
                    Text TraitsLabel : ui.Overline { Text = "Traits" }
                    HorizontalBox Chips {
                        AnchorData.SizeDelta = (0, 0)
                        Spacing = 8
                        Widget QuietDraw : ChipCell {
                            + Overlay { }
                            RectBlock : ui.Chip : ui.FillSlot
                            Text : ui.Caption { Text = "Quiet draw";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot { VerticalAlignment = Center } }
                        }
                        Widget AshCured : ChipCell {
                            + Overlay { }
                            RectBlock : ui.Chip : ui.FillSlot
                            Text : ui.Caption { Text = "Ash-cured";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot { VerticalAlignment = Center } }
                        }
                        Widget TwoHanded : ChipCell {
                            + Overlay { }
                            RectBlock : ui.Chip : ui.FillSlot
                            Text : ui.Caption { Text = "Two-handed";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot { VerticalAlignment = Center } }
                        }
                    }
                }
                slot Footer {
                    HorizontalBox Actions {
                        Spacing = 12
                        Native.Button Drop : ui.ButtonSecondary { Text : ui.Label { Text = "Drop" } }
                        Native.Button Compare : ui.ButtonSecondary { Text : ui.Label { Text = "Compare" } }
                        Native.Button Equip : ui.ButtonPrimary { Text : ui.LabelOnAccent { Text = "Equip" } }
                    }
                }
            }

            // ------------------------------------------------------------------ the prompts along the bottom
            HorizontalBox Hints {
                Spacing = 24
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 0)
                AnchorData.Pivot = (0, 0)
                AnchorData.AnchoredPosition = (112, 48)
                AnchorData.SizeDelta = (800, 36)
                KeyHint Pick { Key = "Enter";  Action = "Equip";  @slot Size = (150, 36) }
                KeyHint Sort { Key = "S";  Action = "Sort by kind";  @slot Size = (160, 36) }
                KeyHint Back { Key = "Esc";  Action = "Close";  @slot Size = (130, 36) }
            }
        }
    }
}
```

### Field HUD

What a player sees while exploring: the objective, a compass, the place and the time, what can be done here, the
vitals and the quick slots. Every group stands on a plate. The `Scene` node only stands in for the game in a preview;
take it out in a real HUD.

```
// DUI/Starter/FieldHud.dui
class /Game/UI/Starter/WBP_FieldHud
use "Theme.dui" as ui
use "Blocks/HudPlate.dui" as HudPlate
use "Blocks/ItemSlot.dui" as ItemSlot
use "Blocks/KeyHint.dui" as KeyHint

// A HUD is drawn over the game, so nothing here has a backdrop of its own: every group sits on a ui.Plate, which
// keeps it readable over a bright sky or a dark cave alike. The Scene node only stands in for the game in a preview;
// delete it in a real HUD.

style Corner {
    + Overlay { }
}
style StepBox {
    AnchorData.SizeDelta = (0, 0)
    CornerRadiusUnitMode = Value
    CornerRadius = (3, 3, 3, 3)
    bEnableBorder = true
    BorderWidthUnitMode = Value
    BorderWidth = 2
    @slot { VerticalAlignment = Center  Size = (16, 16) }
}
style Bearing {
    HAlign = Center
    Color = @ui.InkSoft
    @slot Size = (72, 24)
}
style Quick {
    @slot Size = (76, 76)
}

Widget Root : ui.FillParent {
    + Overlay { }

    RectBlock Scene : ui.FillSlot {
        CornerRadius = (0, 0, 0, 0)
        bEnableBodyGradient = true
        BodyGradient = "linear-gradient(180deg, #56636E, #8E9690 52%, #4A4C42 53%, #2E2F28)"
    }

    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // ------------------------------------------------------------------ objective, top left
            Widget Objective : Corner {
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (48, -48)
                AnchorData.SizeDelta = (440, 168)
                RectBlock ObjectiveBack : ui.Plate : ui.FillSlot
                VerticalBox Steps : ui.FillSlot {
                    Padding = (22, 18, 22, 18)
                    Spacing = 10
                    Text ObjectiveKicker : ui.Overline { Text = "Objective";  Color = @ui.Accent }
                    Text ObjectiveName : ui.Lead { Text = "Ring the drowned bell";  @slot HorizontalAlignment = Fill }
                    HorizontalBox StepDone {
                        Spacing = 12
                        @slot HorizontalAlignment = Fill
                        RectBlock DoneBox : StepBox { BodyColor = @ui.Good;  BorderColor = @ui.Good }
                        Text DoneText : ui.Body { Text = "Find the tower key";  Color = @ui.InkSoft;  bStrikethrough = true;  @fill }
                    }
                    HorizontalBox StepNext {
                        Spacing = 12
                        @slot HorizontalAlignment = Fill
                        RectBlock NextBox : StepBox { BodyColor = #00000000;  BorderColor = @ui.InkSoft }
                        Text NextText : ui.Body { Text = "Climb the bell tower";  Color = @ui.Ink;  @fill }
                        Text NextDistance : ui.Caption { Text = "120 m";  Color = @ui.Ink;  HAlign = Right }
                    }
                }
            }

            // ------------------------------------------------------------------ compass, top centre
            Widget Compass : Corner {
                AnchorData.AnchorMin = (0.5, 1)
                AnchorData.AnchorMax = (0.5, 1)
                AnchorData.Pivot = (0.5, 1)
                AnchorData.AnchoredPosition = (0, -48)
                AnchorData.SizeDelta = (720, 44)
                RectBlock CompassBack : ui.Plate : ui.FillSlot
                HorizontalBox Bearings : ui.FillSlot {
                    Padding = (36, 0, 36, 0)
                    Text W : ui.Caption : Bearing { Text = "W";  @slot VerticalAlignment = Center }
                    Text WNW : ui.Caption : Bearing { Text = "300";  @slot VerticalAlignment = Center }
                    Text NW : ui.Caption : Bearing { Text = "NW";  @slot VerticalAlignment = Center }
                    Text NNW : ui.Caption : Bearing { Text = "330";  @slot VerticalAlignment = Center }
                    Text N : ui.Label : Bearing { Text = "N";  Color = @ui.Accent;  @slot VerticalAlignment = Center }
                    Text NNE : ui.Caption : Bearing { Text = "30";  @slot VerticalAlignment = Center }
                    Text NE : ui.Caption : Bearing { Text = "NE";  @slot VerticalAlignment = Center }
                    Text ENE : ui.Caption : Bearing { Text = "60";  @slot VerticalAlignment = Center }
                    Text E : ui.Caption : Bearing { Text = "E";  @slot VerticalAlignment = Center }
                }
                // The heading: a short accent tick on the plate's lower edge, under the N.
                RectBlock Heading : ui.AccentBar {
                    @slot { HorizontalAlignment = Center  VerticalAlignment = Bottom  Size = (24, 3) }
                }
            }

            // ------------------------------------------------------------------ place and time, top right
            Widget Place : Corner {
                AnchorData.AnchorMin = (1, 1)
                AnchorData.AnchorMax = (1, 1)
                AnchorData.Pivot = (1, 1)
                AnchorData.AnchoredPosition = (-48, -48)
                AnchorData.SizeDelta = (260, 132)
                RectBlock PlaceBack : ui.Plate : ui.FillSlot
                VerticalBox PlaceLines : ui.FillSlot {
                    Padding = (20, 14, 20, 14)
                    Spacing = 4
                    Text Region : ui.Overline { Text = "Saltmarsh";  Color = @ui.InkSoft;  HAlign = Right;  @slot HorizontalAlignment = Fill }
                    Text Clock : ui.Lead { Text = "Dusk, 21:40";  FontSize = @ui.SizeBody;  HAlign = Right;  @slot HorizontalAlignment = Fill }
                    Text Tide : ui.WarnText { Text = "Tide turns in 4 min";  FontSize = @ui.SizeCaption;  HAlign = Right;  @slot { HorizontalAlignment = Fill  Padding = (0, 10, 0, 0) } }
                    Native.ProgressBar TideBar : ui.MeterWarn { Percent = 0.3;  @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 4) } }
                }
            }

            // ------------------------------------------------------------------ what can be done here, right of centre
            Widget Prompt : Corner {
                AnchorData.AnchoredPosition = (300, -40)
                AnchorData.SizeDelta = (240, 56)
                RectBlock PromptBack : ui.Plate : ui.FillSlot
                KeyHint Ring {
                    Key = "E"
                    Action = "Ring the bell"
                    @slot { HorizontalAlignment = Fill  VerticalAlignment = Center  Padding = (14, 0, 14, 0)  Size = (0, 36) }
                }
            }

            // ------------------------------------------------------------------ vitals, bottom left
            HudPlate Vitals {
                Name = "Wren Halloway"
                Level = "Lv 14"
                Health = 0.72
                Stamina = 0.45
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 0)
                AnchorData.Pivot = (0, 0)
                AnchorData.AnchoredPosition = (48, 48)
                AnchorData.SizeDelta = (380, 104)
            }

            // ------------------------------------------------------------------ quick slots, bottom centre
            Widget Belt : Corner {
                AnchorData.AnchorMin = (0.5, 0)
                AnchorData.AnchorMax = (0.5, 0)
                AnchorData.Pivot = (0.5, 0)
                AnchorData.AnchoredPosition = (0, 48)
                AnchorData.SizeDelta = (368, 128)
                RectBlock BeltBack : ui.Plate : ui.FillSlot
                VerticalBox BeltColumn : ui.FillSlot {
                    Padding = (14, 12, 14, 10)
                    Spacing = 6
                    HorizontalBox Slots {
                        Spacing = 12
                        rows ItemSlot : Quick (Glyph, Rarity, Count) {
                            2, 1, "x3";  0, 2, "";  6, 0, "";  7, 4, ""
                        }
                    }
                    HorizontalBox Keys {
                        Spacing = 12
                        Text K1 : ui.Caption : KeyLabel { Text = "1" }
                        Text K2 : ui.Caption : KeyLabel { Text = "2" }
                        Text K3 : ui.Caption : KeyLabel { Text = "3" }
                        Text K4 : ui.Caption : KeyLabel { Text = "4" }
                    }
                }
            }
        }
    }
}

style KeyLabel {
    HAlign = Center
    Color = @ui.InkSoft
    @slot Size = (76, 20)
}
```

### Quest journal

A list of quests on the left, and the picked one on the right: its tags, its story, its objectives with their state,
the last journal entry, its rewards and its actions.

```
// DUI/Starter/Journal.dui
class /Game/UI/Starter/WBP_Journal
use "Theme.dui" as ui
use "Blocks/Tab.dui" as Tab
use "Blocks/ListRow.dui" as ListRow
use "Blocks/Icon.dui" as Icon
use "Blocks/KeyHint.dui" as KeyHint

style TabCell {
    @slot Size = (124, 44)
}
style QuestRow {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 72) }
}
// A slot with no layout box places what is put in it by its anchors: the dot fills the row's badge hole.
style Dot : ui.FillParent {
    CornerRadius = (1, 1, 1, 1)
}
style Step {
    + HorizontalBox { Spacing = 14 }
    @slot HorizontalAlignment = Fill
}
style Tick {
    AnchorData.SizeDelta = (0, 0)
    CornerRadiusUnitMode = Value
    CornerRadius = (3, 3, 3, 3)
    bEnableBorder = true
    BorderWidthUnitMode = Value
    BorderWidth = 2
    @slot { VerticalAlignment = Center  Size = (18, 18) }
}
style Reward {
    + Overlay { }
    @slot Size = (88, 88)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            Text Section : ui.Overline {
                Text = "Journal"
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (112, -64)
                AnchorData.SizeDelta = (400, 20)
            }
            Text Heading : ui.Heading {
                Text = "Quests"
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (112, -88)
                AnchorData.SizeDelta = (400, 56)
            }

            // ------------------------------------------------------------------ the list, on the left
            Widget List {
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (112, -24)
                AnchorData.SizeDelta = (620, -304)
                + Overlay { }
                RectBlock ListFace : ui.Panel : ui.FillSlot
                VerticalBox ListColumn : ui.FillSlot {
                    Padding = (24, 20, 24, 24)
                    Spacing = 8
                    HorizontalBox Tabs {
                        Spacing = 4
                        @slot { Padding = (0, 0, 0, 12) }
                        Tab Active : TabCell { Label = "Active";  Selected = true }
                        Tab Done : TabCell { Label = "Done" }
                        Tab Lore : TabCell { Label = "Lore" }
                    }
                    ListRow Bell : QuestRow {
                        Label = "The Drowned Bell";  Detail = "Saltmarsh, east of the ferry";  Value = "Lv 12";  Selected = true
                        slot Badge { RectBlock : Dot { BodyColor = @ui.Accent } }
                    }
                    ListRow Copper : QuestRow {
                        Label = "A Debt in Copper";  Detail = "Ironmonger's Row";  Value = "Lv 9"
                        slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                    }
                    ListRow Lanterns : QuestRow {
                        Label = "Lanterns for the Pass";  Detail = "Greyhook Pass";  Value = "Lv 15"
                        slot Badge { RectBlock : Dot { BodyColor = @ui.Warn } }
                    }
                    ListRow Ferryman : QuestRow {
                        Label = "The Ferryman's Coin";  Detail = "Old ferry landing";  Value = "Lv 11"
                        slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                    }
                    ListRow Heron : QuestRow {
                        Label = "Where the Herons Nest";  Detail = "Reedbank";  Value = "Lv 13"
                        slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                    }
                    Widget ListGap { AnchorData.SizeDelta = (0, 0);  @fill }
                    Text Tally : ui.Caption { Text = "5 active, 12 done";  @slot { HorizontalAlignment = Fill  Padding = (20, 0, 0, 0) } }
                }
            }

            // ------------------------------------------------------------------ the picked quest, on the right
            Widget Page {
                AnchorData.AnchorMin = (1, 0)
                AnchorData.AnchorMax = (1, 1)
                AnchorData.Pivot = (1, 0.5)
                AnchorData.AnchoredPosition = (-112, 32)
                AnchorData.SizeDelta = (1020, -192)
                + Overlay { }
                RectBlock PageFace : ui.Panel : ui.FillSlot
                RectBlock PageEdge : ui.AccentFade {
                    @slot { HorizontalAlignment = Fill  VerticalAlignment = Top  Padding = (16, 0, 16, 0)  Size = (0, 2) }
                }
                VerticalBox PageColumn : ui.FillSlot {
                    Padding = (48, 40, 48, 36)
                    Spacing = 18

                    HorizontalBox Tags {
                        Spacing = 10
                        Widget Region : TagCell {
                            RectBlock RegionFace : ui.Chip : ui.FillSlot
                            Text RegionText : ui.Caption { Text = "Saltmarsh";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot VerticalAlignment = Center }
                        }
                        Widget Level : TagCell {
                            RectBlock LevelFace : ui.Chip : ui.FillSlot
                            Text LevelText : ui.Caption { Text = "Level 12";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot VerticalAlignment = Center }
                        }
                        Widget Main : TagCell {
                            RectBlock MainFace : ui.Chip : ui.FillSlot
                            Text MainText : ui.Caption { Text = "Main story";  Color = @ui.Ink;  Margin = (12, 0, 12, 0);  @slot VerticalAlignment = Center }
                        }
                    }
                    Text Title : ui.Heading { Text = "The Drowned Bell";  @slot HorizontalAlignment = Fill }
                    Text Story : ui.Body {
                        Text = "Since the flood the old bell has rung every night at the turn of the tide, though no one has climbed the tower in nine years. The ferryman swears the rope moves on its own. Someone should find out who, or what, is pulling it."
                        bAutoWrapText = true
                        OverflowType = VerticalOverflow
                        FontSize = @ui.SizeLead
                        @slot HorizontalAlignment = Fill
                    }
                    RectBlock Rule : ui.Divider { @slot { HorizontalAlignment = Fill  Padding = (0, 6, 0, 6)  Size = (0, 2) } }

                    Text StepsLabel : ui.Overline { Text = "Objectives" }
                    Widget StepKey : Step {
                        RectBlock KeyTick : Tick { BodyColor = @ui.Good;  BorderColor = @ui.Good }
                        Text KeyText : ui.Body { Text = "Find the tower key in the drowned chapel";  bStrikethrough = true;  @fill }
                    }
                    Widget StepClimb : Step {
                        RectBlock ClimbTick : Tick { BodyColor = #00000000;  BorderColor = @ui.Accent }
                        Text ClimbText : ui.Body { Text = "Climb the bell tower before the tide turns";  Color = @ui.Ink;  @fill }
                        Text ClimbNote : ui.Caption { Text = "120 m north";  Color = @ui.InkSoft;  HAlign = Right }
                    }
                    Widget StepRing : Step {
                        RectBlock RingTick : Tick { BodyColor = #00000000;  BorderColor = @ui.EdgeStrong }
                        Text RingText : ui.Body { Text = "Ring the bell once, and listen";  @fill }
                    }

                    Text EntryLabel : ui.Overline { Text = "Last entry";  @slot Padding = (0, 14, 0, 0) }
                    Text Entry : ui.Body {
                        Text = "The rope was wet to the top. The tower has had no roof since the flood, and it has not rained in a week."
                        Font = /DreamGUI/DefaultFont_DistanceField_Italic
                        bAutoWrapText = true
                        OverflowType = VerticalOverflow
                        @slot HorizontalAlignment = Fill
                    }
                    Widget Gap { AnchorData.SizeDelta = (0, 0);  @fill }

                    HorizontalBox Bottom {
                        Spacing = 16
                        @slot HorizontalAlignment = Fill
                        VerticalBox Rewards {
                            Spacing = 10
                            @fill
                            Text RewardsLabel : ui.Overline { Text = "Rewards" }
                            HorizontalBox RewardItems {
                                Spacing = 12
                                Widget Coins : Reward {
                                    RectBlock CoinsWell : ui.Well : ui.FillSlot
                                    Icon CoinsIcon { Kind = 5;  @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Padding = (0, 0, 0, 10)  Size = (48, 48) } }
                                    Text CoinsCount : ui.Caption { Text = "350";  Color = @ui.Ink;  @slot { HorizontalAlignment = Center  VerticalAlignment = Bottom  Padding = (0, 0, 0, 6) } }
                                }
                                Widget Bow : Reward {
                                    RectBlock BowWell : ui.Well : ui.FillSlot
                                    Icon BowIcon { Kind = 0;  @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Padding = (0, 0, 0, 10)  Size = (48, 48) } }
                                    Text BowCount : ui.Caption { Text = "Rare";  Color = @ui.Rare;  @slot { HorizontalAlignment = Center  VerticalAlignment = Bottom  Padding = (0, 0, 0, 6) } }
                                }
                                Widget Map : Reward {
                                    RectBlock MapWell : ui.Well : ui.FillSlot
                                    Icon MapIcon { Kind = 6;  @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Padding = (0, 0, 0, 10)  Size = (48, 48) } }
                                    Text MapCount : ui.Caption { Text = "Map";  Color = @ui.Ink;  @slot { HorizontalAlignment = Center  VerticalAlignment = Bottom  Padding = (0, 0, 0, 6) } }
                                }
                            }
                        }
                        HorizontalBox Actions {
                            Spacing = 12
                            @slot VerticalAlignment = Bottom
                            Native.Button Abandon : ui.ButtonSecondary { Text : ui.Label { Text = "Abandon" } }
                            Native.Button Track : ui.ButtonPrimary { Text : ui.LabelOnAccent { Text = "Track" } }
                        }
                    }
                }
            }

            HorizontalBox Hints {
                Spacing = 40
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 0)
                AnchorData.Pivot = (0, 0)
                AnchorData.AnchoredPosition = (112, 48)
                AnchorData.SizeDelta = (600, 36)
                KeyHint Pick { Key = "Enter";  Action = "Track";  @slot Size = (150, 36) }
                KeyHint Switch { Key = "Q E";  Action = "Tabs";  @slot Size = (130, 36) }
                KeyHint Back { Key = "Esc";  Action = "Close";  @slot Size = (130, 36) }
            }
        }
    }
}

style TagCell {
    AnchorData.SizeDelta = (0, 0)
    + Overlay { }
    @slot MinDesiredSize = (0, 30)
}
```

### Results

The end of a run: its name, the rank on a card turned toward the middle, the run in numbers with the experience it
earned, what was found, and what to do next.

```
// DUI/Starter/Results.dui
class /Game/UI/Starter/WBP_Results
use "Theme.dui" as ui
use "Blocks/StatBar.dui" as StatBar
use "Blocks/ItemCard.dui" as ItemCard

style Tally {
    + HorizontalBox { Spacing = 12 }
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 40) }
}
style TallyLabel {
    @fill
    @slot VerticalAlignment = Center
}
style TallyValue {
    HAlign = Right
    @slot VerticalAlignment = Center
}
style CardCell {
    @slot Size = (168, 220)
}
style Block {
    + Overlay { }
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // ------------------------------------------------------------------ the heading, top centre
            VerticalBox Top {
                AnchorData.AnchorMin = (0.5, 1)
                AnchorData.AnchorMax = (0.5, 1)
                AnchorData.Pivot = (0.5, 1)
                AnchorData.AnchoredPosition = (0, -72)
                AnchorData.SizeDelta = (900, 110)
                Spacing = 6
                Text Kicker : ui.Overline { Text = "Expedition complete";  Color = @ui.Accent;  HAlign = Center;  @slot HorizontalAlignment = Fill }
                Text Name : ui.Heading { Text = "The Flooded Chapel";  HAlign = Center;  @slot HorizontalAlignment = Fill }
            }

            // ------------------------------------------------------------------ the rank, turned toward the centre
            RectBlock RankLight : ui.Glow {
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.AnchoredPosition = (380, -20)
                AnchorData.SizeDelta = (620, 620)
            }
            Widget Rank : Block {
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.AnchoredPosition = (380, -20)
                AnchorData.SizeDelta = (360, 460)
                RenderRotation = (0, 16, 0)
                RectBlock RankFace : ui.Panel : ui.FillSlot
                RectBlock RankEdge : ui.AccentFade {
                    @slot { HorizontalAlignment = Fill  VerticalAlignment = Top  Padding = (16, 0, 16, 0)  Size = (0, 2) }
                }
                VerticalBox RankColumn : ui.FillSlot {
                    Padding = (36, 36, 36, 36)
                    Spacing = 4
                    Text RankLabel : ui.Overline { Text = "Rank";  HAlign = Center;  @slot HorizontalAlignment = Fill }
                    Text RankLetter : ui.Display {
                        Text = "A"
                        FontSize = 200
                        Color = @ui.Accent
                        HAlign = Center
                        @slot HorizontalAlignment = Fill
                    }
                    Text RankNote : ui.Lead { Text = "Better than 88 % of runs";  HAlign = Center;  FontSize = @ui.SizeBody;  @slot HorizontalAlignment = Fill }
                    Widget RankGap { AnchorData.SizeDelta = (0, 0);  @fill }
                    RectBlock RankRule : ui.Divider { @slot { HorizontalAlignment = Fill  Padding = (0, 0, 0, 16)  Size = (0, 2) } }
                    HorizontalBox Best {
                        @slot HorizontalAlignment = Fill
                        Text BestLabel : ui.Caption { Text = "Best time";  @fill }
                        Text BestValue : ui.Caption { Text = "17:58";  Color = @ui.Ink;  HAlign = Right }
                    }
                }
            }

            // ------------------------------------------------------------------ the run in numbers, centre
            Widget Numbers : Block {
                AnchorData.AnchorMin = (0.5, 0.5)
                AnchorData.AnchorMax = (0.5, 0.5)
                AnchorData.AnchoredPosition = (60, -20)
                AnchorData.SizeDelta = (520, 340)
                RectBlock NumbersFace : ui.Panel : ui.FillSlot
                VerticalBox NumbersColumn : ui.FillSlot {
                    Padding = (32, 26, 32, 26)
                    Spacing = 4
                    Text NumbersLabel : ui.Overline { Text = "The run";  @slot { HorizontalAlignment = Fill  Padding = (0, 0, 0, 8) } }
                    Widget Time : Tally {
                        Text TimeLabel : ui.Body : TallyLabel { Text = "Time" }
                        Text TimeValue : ui.Figure : TallyValue { Text = "18:42";  FontSize = @ui.SizeLead }
                    }
                    Widget Foes : Tally {
                        Text FoesLabel : ui.Body : TallyLabel { Text = "Foes driven off" }
                        Text FoesValue : ui.Figure : TallyValue { Text = "47";  FontSize = @ui.SizeLead }
                    }
                    Widget Hurt : Tally {
                        Text HurtLabel : ui.Body : TallyLabel { Text = "Damage taken" }
                        Text HurtValue : ui.Figure : TallyValue { Text = "1,280";  FontSize = @ui.SizeLead }
                    }
                    Widget Secrets : Tally {
                        Text SecretsLabel : ui.Body : TallyLabel { Text = "Secrets found" }
                        Text SecretsValue : ui.Figure : TallyValue { Text = "3 / 5";  FontSize = @ui.SizeLead }
                    }
                    Widget Spare { AnchorData.SizeDelta = (0, 0);  @fill }
                    StatBar Experience {
                        Label = "Level 14 to 15"
                        Value = "+2,400 xp"
                        Percent = 0.82
                        @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 52) }
                    }
                }
            }

            // ------------------------------------------------------------------ what was found, right
            VerticalBox Found {
                AnchorData.AnchorMin = (1, 0.5)
                AnchorData.AnchorMax = (1, 0.5)
                AnchorData.Pivot = (1, 0.5)
                AnchorData.AnchoredPosition = (-112, -20)
                AnchorData.SizeDelta = (352, 340)
                Spacing = 14
                Text FoundLabel : ui.Overline { Text = "Found" }
                HorizontalBox FoundCards {
                    Spacing = 16
                    ItemCard Garnet : CardCell { Name = "River Garnet";  Kind = "Gem";  Glyph = 3;  Count = "x2" }
                    ItemCard Tonic : CardCell { Name = "Fen Tonic";  Kind = "Consumable";  Glyph = 2;  Count = "x3" }
                }
                Text Crowns : ui.Body { Text = "and 350 crowns";  @slot HorizontalAlignment = Fill }
            }

            // ------------------------------------------------------------------ what next, bottom centre
            HorizontalBox Actions {
                AnchorData.AnchorMin = (0.5, 0)
                AnchorData.AnchorMax = (0.5, 0)
                AnchorData.Pivot = (0.5, 0)
                AnchorData.AnchoredPosition = (0, 72)
                AnchorData.SizeDelta = (360, 52)
                Spacing = 16
                Native.Button Retry : ui.ButtonSecondary { @fill;  Text : ui.Label { Text = "Run again" } }
                Native.Button Continue : ui.ButtonPrimary { @fill;  Text : ui.LabelOnAccent { Text = "Continue" } }
            }
        }
    }
}
```
