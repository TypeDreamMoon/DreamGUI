# Presets

> This page stands on its own: what an author needs is here, and an agent that cannot open its links loses nothing it
> needs. The links lead to the online manual for depth. Look names up (DreamMCP's `lookup`, or
> `DUI/.dui-symbols.json`) instead of guessing them. Built from `Docs/language/presets.md` by `.skill/build-reference.ps1`.
## Presets

DreamGUI ships four presets: libraries of colours, type sizes, spacing and styles that a screen wears instead of writing
its own hex colours and pixel sizes. They are in the plugin's `DUI/Presets/` folder. Each is a library -- `resources` and
`style` blocks, no root node and no `class` line -- so it compiles into nothing on its own and writes no asset.

| Preset | Look | Accent |
| --- | --- | --- |
| `Default` | Graphite greys, rounded corners | soft blue |
| `Ember` | Warm charcoal and parchment-coloured ink, small corners | amber |
| `Signal` | Blue-black glass, tight corners | mint |
| `Paper` | Warm off-white paper and dark ink (a light theme) | vermilion |

All four are low in saturation with one accent, have three steps of text colour and three of surface, and pass a 4.5 : 1
contrast for every text colour on every surface it is meant for. They declare the same names; only the values differ.

### Naming the preset once

A project names its preset in one file, `DUI/Theme.dui`, which holds nothing but the `use` line. Every screen and every
component takes the preset through it, under the namespace `ui`:

```
// DUI/Theme.dui
// The one line that picks the look of every screen and component in the project.
use "Plugin.DreamGUI:Presets/Default.dui"
```

```
use "Theme.dui" as ui
```

Changing `Default` to `Ember`, `Signal` or `Paper` in `Theme.dui` changes the look of every screen and component that
uses it, at their next compile. A single screen can also name a preset directly,
`use "Plugin.DreamGUI:Presets/Ember.dui" as ui`, but then its components keep the look their own `use` line gives them.

To make a preset of your own, copy one into your project's `DUI/` folder (not `DUI/Presets/`, which would shadow the
plugin's), change its values and keep its names, and point `Theme.dui` at it.

### Using the names

- Write `Color = @ui.Ink`, `BodyColor = @ui.Surface`, `FontSize = @ui.SizeBody`, `Spacing = @ui.Gap4`: never a hex colour
  or a raw size that a preset already has a name for. A screen that names only presets' values changes look with them.
- Wear styles: `Text : ui.Title`, `RectBlock : ui.Panel`, `Native.Button : ui.ButtonPrimary`. A node's own lines win over
  its styles', so `Text : ui.Caption { Color = @ui.Ink }` is a caption in the main text colour.
- Every size is in pixels at the 1920 x 1080 design size. Lay screens out on a design canvas that scales to the viewport
  (the [building blocks](https://gui.toolchain.64hz.cn/en/docs/dui/building-blocks/#the-root-of-a-screen) start with one): at 1280 x 720 a 20 px body text is
  drawn at 13 px, as in a game.
- A resource stands for a whole value. It cannot be one part of a tuple: `Padding = (@ui.Gap4, 0, @ui.Gap4, 0)` is
  DUI4003. Write the numbers, or set `Spacing` and the box's `Padding` from a style.

### Colours

| Name | For |
| --- | --- |
| `Base` | The screen behind everything (`ScreenBackdrop` grades it) |
| `Surface` | A panel's body |
| `Raised` | A card on a panel, a key cap, a secondary button |
| `Sunken` | Sunk into a panel: an empty slot, a well |
| `Edge`, `EdgeStrong` | A panel's hairline edge and a divider; a stronger edge for what is framed |
| `Ink`, `InkSoft`, `InkFaint` | Text: what to read first, what supports it, what can be skipped |
| `Accent` | The one accent: the primary button, the selected tab's bar, a meter's fill |
| `AccentSoft` | A weak fill of the accent: a selected row, a selected tab, a chip |
| `AccentGlow`, `AccentEdge` | The accent see-through: a glow, the edge of a selected card |
| `OnAccent` | Text on the accent (the primary button's label) |
| `Good`, `Bad`, `Warn` | States: healed, gained, done; hurt, lost; running out |
| `Common`, `Uncommon`, `Rare`, `Epic`, `Legendary` | Item rarity |
| `Scrim` | The see-through plate under a HUD group, readable over any scene |
| `Shadow` | The shadow under a panel |

### Sizes

| Name | Value |
| --- | --- |
| `SizeCaption`, `SizeBody`, `SizeLead`, `SizeTitle`, `SizeHeading`, `SizeDisplay` | 16, 20, 24, 30, 44, 72 |
| `Gap1` to `Gap8` | 4, 8, 12, 16, 24, 32, 48, 64 |
| `RadiusSmall`, `Radius`, `RadiusLarge` | The preset's corners, in pixels (Signal's are the tightest) |
| `EdgeWidth` | The width of a hairline edge |
| `FontRegular`, `FontBold` | The plugin's distance-field fonts |

### Styles

| Style | Wear it on | For |
| --- | --- | --- |
| `Display`, `Heading`, `Title` | `Text` | A screen's name; a page's heading; a panel's or card's title. Bold, `Ink` |
| `Lead`, `Body`, `Caption` | `Text` | An opening line; running text (`InkSoft`); small print (`InkFaint`) |
| `Overline` | `Text` | A small spaced-out label in capitals over a group: SECTION, SLOT |
| `Figure` | `Text` | A number that is the point of its box: a level, a price |
| `AccentText`, `GoodText`, `BadText`, `WarnText` | `Text` | Text in the accent or a state colour |
| `Label`, `LabelOnAccent`, `LabelQuiet` | `Text` | The label inside a button, a tab or a key cap, centred |
| `ScreenBackdrop` | `RectBlock` | What a full screen sits on: the base colour, lighter where the eye lands |
| `Panel` | `RectBlock` | A panel: graded body, hairline edge, rounded corners, a soft shadow |
| `Card`, `CardSelected` | `RectBlock` | A card on a panel; the same card picked |
| `Well` | `RectBlock` | Sunk into a panel: an empty slot, a frame for an icon |
| `Plate` | `RectBlock` | The see-through plate under a HUD group |
| `Chip`, `KeyCap` | `RectBlock` | A small tag; a key prompt's cap |
| `Divider`, `AccentBar`, `AccentFade` | `RectBlock` | A hairline; a short solid accent bar; the accent fading out to the right. No size of their own: give them `@slot Size` or a `SizeDelta` |
| `Glow` | `RectBlock` | A soft round light of the accent, behind a figure or an icon. Never over text |
| `ButtonPrimary`, `ButtonSecondary` | `Native.Button` | The one action a screen wants most; every other action |
| `TabOn`, `TabOff` | `Native.Button` | A selected tab; the others |
| `Meter`, `MeterGood`, `MeterBad`, `MeterWarn` | `Native.ProgressBar` | A meter in the accent or a state colour. Give it a height in its slot |
| `FillParent` | any node | Anchored to its parent's four corners: it fills the parent |
| `FillSlot` | any node in a panel | Fills its slot both ways, with no size of its own |

The control styles set `StyleSource = Inline`, so they take effect whether or not the project has a style sheet.
