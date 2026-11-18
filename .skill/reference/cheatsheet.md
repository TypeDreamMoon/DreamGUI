# The .dui cheat sheet

> This page stands on its own: what an author needs is here, and an agent that cannot open its links loses nothing it
> needs. The links lead to the online manual for depth. Look names up (DreamMCP's `lookup`, or
> `DUI/.dui-symbols.json`) instead of guessing them. Built from `Docs/cheatsheet.md` by `.skill/build-reference.ps1`.
One page of the `.dui` language: the shape of a file, how nodes, properties and values are written, styles and
resources, bindings, control flow and timelines, and the codes that cost the most time. Every complete file on this
page — a block whose first line is `// DUI/<path>.dui` — is compiled and laid out by the test suite with no error and
no warning. For the rules behind a line, read [the language](https://gui.toolchain.64hz.cn/en/docs/dui/overview/); for a code,
[the diagnostics](https://gui.toolchain.64hz.cn/en/docs/diagnostics/) or the same data as JSON, [`diagnostics/codes.json`](https://github.com/TypeDreamMoon/DreamGUI/blob/main/Docs/diagnostics/codes.json).

`.dui` is not QML, XAML, UMG or CSS. Look every node type and property up (DreamMCP's `lookup`, or
`DUI/.dui-symbols.json`) instead of guessing it: an unknown name is an error, not a no-op.

## A file

A file lives under a `DUI/` folder (the project's or a plugin's), never under `Content/`. It is a list of statements,
one per line or separated by `;`, in any order:

| Statement | Purpose |
| :-- | :-- |
| `class /Game/UI/WBP_Settings` | the widget Blueprint this file is the source of; at most one |
| `use "Panels/Row.dui" as Row` | another file (a component, or a library of styles and resources) |
| `resources { … }` | named constants: `@Accent` |
| `style Name { … }` | a named set of lines a node wears with `: Name` |
| `props { … }` · `events { … }` · `viewmodels { … }` | what this file's class declares |
| `timeline Name { … }` | an animation |
| one node | the root of the tree; a file with no root is a library |

## The look: one preset, named once

Take every colour, size and look from a preset instead of writing hex colours and pixel sizes. A project names its
preset in one file, and every screen uses it as `ui` ([Presets](https://gui.toolchain.64hz.cn/en/docs/dui/presets/)):

```
// DUI/Theme.dui
// The one line that picks the look of every screen and component in the project.
use "Plugin.DreamGUI:Presets/Default.dui"
```

Then `Color = @ui.Ink`, `FontSize = @ui.SizeBody`, `Spacing = @ui.Gap4`, and styles: `Text : ui.Title`,
`RectBlock : ui.Panel`, `Native.Button : ui.ButtonPrimary`. Colours: `Base Surface Raised Sunken Edge EdgeStrong Ink
InkSoft InkFaint Accent AccentSoft AccentGlow OnAccent Good Bad Warn Scrim`. Sizes: `SizeCaption SizeBody SizeLead
SizeTitle SizeHeading SizeDisplay`, `Gap1`..`Gap8` (4 to 64), `RadiusSmall Radius RadiusLarge`. Styles: `Display
Heading Title Lead Body Caption Overline Figure AccentText GoodText BadText WarnText Label LabelOnAccent LabelQuiet` on
a `Text`; `ScreenBackdrop Panel Card CardSelected Well Plate Chip KeyCap Divider AccentBar AccentFade Glow` on a
`RectBlock`; `ButtonPrimary ButtonSecondary TabOn TabOff` on a `Native.Button`; `Meter MeterGood MeterBad MeterWarn` on
a `Native.ProgressBar`; `FillParent`, `FillSlot` on anything.

## A screen

A component — a file another file places by name — and a screen that places it. The screen's root is the one that fits
every viewport: a backdrop that fills it, and a 1920 × 1080 design canvas in a `ScaleBox` that tells the box its size
with `@slot Size`. Everything after the canvas is in design pixels, anchored to its edges.

```
// DUI/CheatSheet/StatLine.dui
class /Game/UI/CheatSheet/WBP_CheatSheetStatLine
use "Theme.dui" as ui

// A label, its value and a meter. Each instance sets its own three props.
props {
    Text   Label
    Text   Value
    Number Percent = 0.5
}

Widget Root : ui.FillParent {
    + VerticalBox { Spacing = 8 }
    HorizontalBox Top {
        Spacing = 8
        @slot HorizontalAlignment = Fill
        Text LabelText : ui.Body {
            Text <- Label
            @fill
        }
        Text ValueText : ui.Figure {
            Text <- Value
            FontSize = @ui.SizeBody
            HAlign = Right
        }
    }
    Native.ProgressBar Meter : ui.Meter {
        Percent <- Percent
        @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 8) }
    }
}
```

```
// DUI/CheatSheet/Pause.dui
class /Game/UI/CheatSheet/WBP_CheatSheetPause
use "Theme.dui" as ui
use "CheatSheet/StatLine.dui" as StatLine

style StatRow {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 48) }
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot

    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // A HUD group: on a see-through plate, so it reads over any scene. Top left; Y goes up.
            Widget Hud {
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (64, -56)
                AnchorData.SizeDelta = (360, 104)
                + Overlay { }
                RectBlock HudBack : ui.Plate : ui.FillSlot
                VerticalBox HudColumn : ui.FillSlot {
                    Padding = (20, 16, 20, 18)
                    Spacing = 10
                    HorizontalBox HudTop {
                        Spacing = 10
                        @slot HorizontalAlignment = Fill
                        Text HudName : ui.Lead {
                            Text = "Rowan"
                            FontSize = @ui.SizeBody
                            @fill
                        }
                        Text HudLevel : ui.AccentText {
                            Text = "Lv 14"
                            HAlign = Right
                        }
                    }
                    Native.ProgressBar Health : ui.MeterBad {
                        Percent = 0.72
                        @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 10) }
                    }
                }
            }

            // A soft light behind the panel. It is meant to spill past everything, so it says so.
            RectBlock Halo : ui.Glow {
                @tag Bleed
                AnchorData.AnchorMin = (0.5, 0.5)
                AnchorData.AnchorMax = (0.5, 0.5)
                AnchorData.SizeDelta = (960, 760)
            }

            // The panel: its face first, then the content on top, inside the padding.
            Widget Menu {
                AnchorData.AnchorMin = (0.5, 0.5)
                AnchorData.AnchorMax = (0.5, 0.5)
                AnchorData.SizeDelta = (720, 520)
                + Overlay { }
                RectBlock Face : ui.Panel : ui.FillSlot
                VerticalBox Column : ui.FillSlot {
                    Padding = (40, 36, 40, 36)
                    Spacing = 20
                    VerticalBox Header {
                        Spacing = 6
                        @slot HorizontalAlignment = Fill
                        Text Kicker : ui.Overline { Text = "Paused" }
                        Text Heading : ui.Title { Text = "The Ashen Road" }
                    }
                    RectBlock Rule : ui.Divider {
                        @slot { HorizontalAlignment = Fill  Size = (0, 2) }
                    }
                    Text Summary : ui.Body {
                        Text = "Chapter III. The ferry leaves at dusk; the toll keeper still wants his answer."
                        bAutoWrapText = true
                        @slot HorizontalAlignment = Fill
                    }
                    StatLine Journey : StatRow { Label = "Journey";  Value = "64 %";  Percent = 0.64 }
                    StatLine Supplies : StatRow { Label = "Supplies";  Value = "3 days";  Percent = 0.3 }
                    HorizontalBox Actions {
                        Spacing = 12
                        @slot { HorizontalAlignment = Right  VerticalAlignment = Bottom  SizeRule = Fill }
                        Native.Button Quit : ui.ButtonSecondary { Text : ui.Label { Text = "Quit" } }
                        Native.Button Resume : ui.ButtonPrimary { Text : ui.LabelOnAccent { Text = "Resume" } }
                    }
                }
            }
        }
    }
}
```

For more to start from: the [building blocks](https://gui.toolchain.64hz.cn/en/docs/dui/building-blocks/) (panel, card, tab, list row, stat bar,
placeholder icons, HUD plate, key prompt) and the [starter screens](https://gui.toolchain.64hz.cn/en/docs/dui/starter-screens/).

## Nodes

```
Text Title {
    Text = "Settings"
}
```

A node is `Type Id { … }`; the block holds properties, `@slot` lines, `@tag` lines, `+ Component` blocks, child
nodes, `if` and `for`/`each`. The id is a C++ identifier, unique in the file ignoring case. A node nothing refers to
may leave the id out if a block or a style follows: `Text : Caption { Text = "Ready" }`.

## Values

| Kind | Examples |
| :-- | :-- |
| Number | `24`, `0.95`, `-3` |
| String | `"Settings"` — localized on an `FText`; `@noloc`, `@note("…")`, `@max(16)` after it |
| Colour | `#1E1E1E`, `#1E1E1ECC` — **sRGB** |
| Tuple | `(400, 240)`, `(0, 8, 0, 0)` — a vector, a margin, whatever struct the property is |
| Word | `Left`, `Collapsed`, `true`, `None` |
| Asset path | `/Game/UI/T_Icon` |
| Resource | `@Accent` |
| List / map / struct | `[1, 2]` · `{ Hover: #FFCC00 }` · `{ Distance: 12, Angle: 45 }` |

**Positions:** outside a box a node is placed by `AnchorData` — `AnchorMin`/`AnchorMax` 0 to 1 with **(0, 0) the
bottom-left**, `Pivot`, `AnchoredPosition` (positive Y is **up**), `SizeDelta`. `AnchorMin = (0, 0)`,
`AnchorMax = (1, 1)`, `SizeDelta = (0, 0)` fills the parent. Inside a box the box places it through `@slot` lines.
`RenderTranslation = (X, Y, Z)`: X is depth (negative is nearer), Y right, Z up.
`RenderRotation = (Pitch, Yaw, Roll)`: a positive Yaw brings the right edge nearer, so a panel on the left faces the
middle with a negative Yaw and one on the right with a positive Yaw; a negative Pitch lays a plane back (top edge farther).
`layout` says which way each turned face looks.

## Slots and components

```
Text Label {
    @slot HorizontalAlignment = Fill
    @slot Padding = (0, 4, 0, 4)
}
```

`@slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }` sets several; `@fill` is `@slot SizeRule = Fill`, `@fill 2` adds a
weight. A `+ Class { … }` block adds a component (a behaviour) to the node.

## Styles and resources

```
style Caption {
    FontSize = 14
    Color    = #8C93A6
}

style Warning : Caption {
    Color = #E05A47
}

Text Note : Warning { Text = "Unsaved changes" }
```

```
resources {
    Color   Accent = #FF6600
    Number  Gap    = 8
    Vector2 Icon   = (24, 24)
    String  Brand  = "Dream"
    Asset   Row    = /Game/UI/WBP_Row
}
```

## Bindings

```
Text <- GetTitle()
RenderOpacity <- Fade() * 0.8
Shown <- Count() > 0 && !IsLocked()
```

- `<-` drives a property from a function, a variable, a member path or an expression (`||` `&&` `==` `<` `+` `*` `!`;
  no `/`, no `? :`).
- `->`, `+=` and `=` route an event to a function; `<->` mirrors a property both ways; `Shown <- …` shows or
  collapses a node.

## Control flow

```
if HasSave() {
    Row Continue { Label = "Continue" }
} else if IsLoading() {
    Native.Throbber Spinner { }
} else {
    Text NoSave { Text = "No save data" }
}
```

`for Item in Items() { … }` copies its body into the panel per element; `each Items() { … }` fills a list view's cells.

## Timelines

```
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (0, -40, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external
```

## Mistakes that cost the most time

| Code | Severity | Message |
| :-- | :-- | :-- |
| [DUI3003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3003) | error | A type that is no tag, container, alias, registered widget or path. |
| [DUI4001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4001) | error | No property of that name |
| [DUI5002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5002) | error | A property of a visual the node's type does not create. |
| [DUI5005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5005) | error | A bound property with no setter. |
| [DUI2006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2006) | error | No root node in a file compiled into a class, or more than one. |
| [DUI8001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8001) | warning | What can show of a node reaches more than 1 px past an edge of the viewport, or lies wholly outside it |
| [DUI8002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8002) | warning | A text cut short by its overflow policy, or laid out past its box |
| [DUI8003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8003) | warning | A node that draws or can be pressed under 1 px wide or high (an info for a plain widget with children), or a pressable control under 24 x 24 px. |
| [DUI8004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8004) | warning | Two pressable controls in one canvas overlapping on the screen by 2 px or more on both axes, wherever they are in the tree |
| [DUI8008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8008) | warning | A node that draws more than 4 px (and 5 % of its size) past the panel or card it sits in |
| [DUI8009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8009) | warning | A visual that takes no pointer, drawn after a text in its canvas, over 12 % or more of its glyphs -- or a small decoration lying mostly in the text's line |
| [DUI8010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8010) | info | A panel whose content keeps to one side (a band of 38 % or more empty), leaves a wide band empty across its middle, or spreads its rows thin |

- A `Style.X = v` line changes nothing while the project has a style sheet: write `StyleSource = Inline` first.
- Colour literals are sRGB; an `FLinearColor` property decodes them, so `#808080` is about 0.22.
- A 1920 × 1080 design canvas in a `ScaleBox` needs `@slot Size = (1920, 1080)` as well as its `SizeDelta`, and the
  backdrop goes outside the box. Without the slot size the box measures the canvas by its content and scales the whole
  screen off the edges. Copy the root of the screen above.
- A decoration that is meant to spill past its panel or lie over a text (a glow, a sheen) says so with `@tag Bleed`;
  otherwise it is DUI8008 or DUI8009.
- A screen that compiles is not done: `layout` it at `1280x720, 1920x1080, 2560x1440` and fix every DUI8xxx.

<!-- Generated by .skill/gen-knowledge.ps1 from .skill/cheatsheet/cheatsheet.src.md. Edit that file, not this one. -->
