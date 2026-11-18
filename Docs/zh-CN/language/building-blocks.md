---
title: "构建块"
description: "用于作为屏幕起点的完整 .dui 文件：将 1920 x 1080 设计缩放到任意视口的根节点、带有头部、主体和页脚的面板、属性条、选项卡、列表行、物品卡和物品槽、HUD 底板、按键提示、菜单行，以及由形状绘制的占位图标。"
---

<a id="building-blocks"></a>

## 构建块

下列每个块都是一个完整文件：将其复制到项目的 `DUI/` 文件夹中，路径以其第一行为准。它们通过 `DUI/Theme.dui` 套用[预设](presets.md)，因此外观会随其变化；该文件也要一并复制。每个块在每种预设下编译和布局时都没有错误和警告，而末尾的两个演示屏幕会把它们全部摆出来；三个旋转到 3D 的屏幕各自独立。 [起始屏幕](starter-screens.md) 由它们构建。

除自身之外，它们还展示了：

- **屏幕的根节点**会将设计画布缩放到视口，因此按 1920 x 1080 布局一次的屏幕，能同样适配 1280 x 720 和 2560 x 1440。
- **装饰绝不会覆盖文本。** 面板的强调边、卡片的辉光和选中行的底色会先绘制在文本下方、由内边距留出的空间里——绝不会盖在文本上，也绝不会超出面板。
- **内容看起来像真实数据。** 各项数值彼此不同，读起来像游戏中的数据：名称、地点、数量。
- **状态是显示出来的，而非重建的。** 选中的选项卡、行或卡片都是同一个块，只是带有 `Selected = true`：用于标记它的部件始终存在，而 `Shown <- Selected` 会启用它们。

<a id="the-root-of-a-screen"></a>

### 屏幕的根节点

```
// DUI/Blocks/Skeleton.dui
class /Game/UI/Blocks/WBP_Skeleton
use "Theme.dui" as ui

Widget Root : ui.FillParent {
    + Overlay { }

    // Full bleed: the backdrop fills the whole viewport, whatever its shape.
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot

    // The design canvas: everything is laid out on a 1920 x 1080 canvas, which the scale box shrinks or grows to fit
    // the viewport. The canvas must say its size to the scale box with @slot Size; without it the scale box measures
    // the canvas by what is inside it and scales the whole screen off the edges.
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            // From here on, place things in design pixels. Anchors hold them to the canvas's edges; Y goes up.
            Text Heading : ui.Heading {
                Text = "A screen"
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (112, -72)
                AnchorData.SizeDelta = (800, 56)
            }
        }
    }
}
```

画布让固定设计适配各种尺寸。有两点很重要。背景位于缩放框之外，因此能填满任意形状的视口；在宽于 16 : 9 的视口中，画布居中，两侧为背景。并且，画布通过 `@slot Size = (1920, 1080)` 以及 `SizeDelta` 告知缩放框自身尺寸：没有它，缩放框会按内部内容来测量画布，并把屏幕放大到超出应有尺寸数倍。

<a id="panel"></a>

### 面板

一个带有头部（标题上方的小标签）、主体和页脚的面板。主体是默认槽位：宿主在实例中嵌套的内容会放在那里，并逐项纵向排列。页脚是具名槽位，右对齐，用于放置面板的按钮。

```
// DUI/Blocks/Panel.dui
class /Game/UI/Blocks/WBP_Panel
use "Theme.dui" as ui

props {
    Text Kicker
    Text Heading
}

Widget Root : ui.FillParent {
    + Overlay { }

    // Drawn first, so everything after it is on top: the face, then a thin accent along the top edge, inset
    // from the rounded corners. Neither can reach the text, which starts below the padding.
    RectBlock Face : ui.Panel : ui.FillSlot
    RectBlock TopEdge : ui.AccentFade {
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Top  Padding = (16, 0, 16, 0)  Size = (0, 2) }
    }

    VerticalBox Column : ui.FillSlot {
        Padding = (32, 28, 32, 28)
        Spacing = 20

        VerticalBox Header {
            Spacing = 6
            @slot HorizontalAlignment = Fill
            Text KickerText : ui.Overline {
                Text <- Kicker
                @slot HorizontalAlignment = Fill
            }
            Text HeadingText : ui.Title {
                Text <- Heading
                @slot HorizontalAlignment = Fill
            }
        }
        RectBlock Rule : ui.Divider {
            @slot { HorizontalAlignment = Fill  Size = (0, 2) }
        }
        slot Body default {
            + VerticalBox { Spacing = 14 }
            @slot { HorizontalAlignment = Fill  SizeRule = Fill }
        }
        slot Footer {
            + HorizontalBox { Spacing = 12 }
            @slot HorizontalAlignment = Right
        }
    }
}
```

<a id="stat-bar"></a>

### 属性条

一个标签、其数值、比较时会增加或减少的量，以及一个计量条。数值和计量条的填充量都归各实例自身所有。

```
// DUI/Blocks/StatBar.dui
class /Game/UI/Blocks/WBP_StatBar
use "Theme.dui" as ui

// A labelled meter with its value, and what a comparison would gain or lose (leave both empty for none).
props {
    Text   Label
    Text   Value
    Text   Gain
    Text   Loss
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
        Text GainText : ui.GoodText {
            Text <- Gain
            HAlign = Right
        }
        Text LossText : ui.BadText {
            Text <- Loss
            HAlign = Right
        }
    }
    Native.ProgressBar Meter : ui.Meter {
        Percent <- Percent
        @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 8) }
    }
}
```

<a id="tab"></a>

### 选项卡

选项卡栏中的一个选项卡。将多个放入 `HorizontalBox`；选中的那个会标记为 `Selected = true`。

```
// DUI/Blocks/Tab.dui
class /Game/UI/Blocks/WBP_Tab
use "Theme.dui" as ui

props {
    Text Label
    Bool Selected = false
}
events {
    Picked
}

Widget Root : ui.FillParent {
    + Overlay { }
    // Two faces, one shown at a time: the selected one is filled with the weak accent and underlined.
    Native.Button On : ui.TabOn : ui.FillSlot {
        Shown <- Selected
        OnClicked -> emit Picked()
        Text OnLabel : ui.Label { Text <- Label }
    }
    Native.Button Off : ui.TabOff : ui.FillSlot {
        Shown <- !Selected
        OnClicked -> emit Picked()
        Text OffLabel : ui.LabelQuiet { Text <- Label }
    }
    RectBlock Underline : ui.AccentBar {
        Shown <- Selected
        @slot { HorizontalAlignment = Fill  VerticalAlignment = Bottom  Padding = (12, 0, 12, 0)  Size = (0, 3) }
    }
}
```

<a id="list-row"></a>

### 列表行

列表中的一行：宿主提供的徽章、详情行上方的名称，以及右侧的数值。没有布局框的槽位会根据锚点来放置放入其中的内容，因此徽章会用 `ui.FillParent` 填满其空位（参见演示）。

```
// DUI/Blocks/ListRow.dui
class /Game/UI/Blocks/WBP_ListRow
use "Theme.dui" as ui

props {
    Text  Label
    Text  Detail
    Text  Value
    Bool  Selected = false
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Wash : ui.CardSelected : ui.FillSlot {
        Shown <- Selected
    }
    RectBlock Edge : ui.AccentBar {
        Shown <- Selected
        @slot { HorizontalAlignment = Left  VerticalAlignment = Fill  Padding = (0, 10, 0, 10)  Size = (3, 0) }
    }
    HorizontalBox Line : ui.FillSlot {
        Padding = (20, 0, 20, 0)
        Spacing = 16
        // What the host puts here marks the row: a dot of a rarity or state colour, a small icon.
        slot Badge {
            @slot { VerticalAlignment = Center  Size = (12, 12) }
        }
        VerticalBox Words {
            Spacing = 2
            @fill
            @slot VerticalAlignment = Center
            Text LabelText : ui.Lead {
                Text <- Label
                FontSize = @ui.SizeBody
                @slot HorizontalAlignment = Fill
            }
            Text DetailText : ui.Caption {
                Text <- Detail
                Color = @ui.InkSoft
                @slot HorizontalAlignment = Fill
            }
        }
        Text ValueText : ui.Figure {
            Text <- Value
            FontSize = @ui.SizeBody
            HAlign = Right
            @slot VerticalAlignment = Center
        }
    }
}
```

<a id="placeholder-icons"></a>

### 占位图标

由矩形块、多边形和圆环绘制的中性图标，使屏幕在美术资源到位之前看起来已经完成：剑、盾、药水、宝石、技能、硬币、卷轴和钥匙。它们采用预设的颜色。之后可将该块替换为美术资源；放置它的屏幕无需更改。

```
// DUI/Blocks/Icon.dui
class /Game/UI/Blocks/WBP_Icon
use "Theme.dui" as ui

// A neutral placeholder icon drawn from shapes, in a 64 x 64 box. Kind picks the drawing:
//   0 Sword (a weapon)   1 Shield (armour)   2 Potion   3 Gem   4 Skill   5 Coin (currency)   6 Scroll (a quest)   7 Key
// Size the instance; the drawing scales with it. Swap it for art later without touching the screens.
// A rect block's CornerRadius runs bottom-right, top-right, top-left, bottom-left.
props {
    Integer Kind = 0
}

style Part {
    Raycastable = Disabled
}
style Metal : Part {
    BodyColor = @ui.Ink
}
style Shade : Part {
    BodyColor = @ui.InkFaint
}
style Tint : Part {
    BodyColor = @ui.Accent
}
style Round {
    CornerRadius = (1, 1, 1, 1)
}
style Glyph : ui.FillParent {
    Raycastable = Disabled
}

Widget Root : ui.FillParent {
    + ScaleBox { Stretch = ScaleToFit }
    Widget Box {
        AnchorData.SizeDelta = (64, 64)
        @slot Size = (64, 64)

        Widget Sword : Glyph {
            Shown <- Kind == 0
            RenderRotation = (0, 0, 45)
            RectBlock Blade : Metal {
                AnchorData.AnchoredPosition = (0, 8)
                AnchorData.SizeDelta = (8, 42)
                CornerRadiusUnitMode = Value
                CornerRadius = (1, 4, 4, 1)
            }
            RectBlock Guard : Shade {
                AnchorData.AnchoredPosition = (0, -14)
                AnchorData.SizeDelta = (26, 6)
                CornerRadiusUnitMode = Value
                CornerRadius = (3, 3, 3, 3)
            }
            RectBlock Grip : Tint {
                AnchorData.AnchoredPosition = (0, -22)
                AnchorData.SizeDelta = (6, 12)
                CornerRadius = (0, 0, 0, 0)
            }
            RectBlock Pommel : Shade : Round {
                AnchorData.AnchoredPosition = (0, -30)
                AnchorData.SizeDelta = (9, 9)
            }
        }

        Widget Shield : Glyph {
            Shown <- Kind == 1
            RectBlock Rim : Shade {
                AnchorData.SizeDelta = (44, 52)
                CornerRadiusUnitMode = Value
                CornerRadius = (22, 6, 6, 22)
            }
            RectBlock Face : Metal {
                AnchorData.AnchoredPosition = (0, 1)
                AnchorData.SizeDelta = (36, 44)
                CornerRadiusUnitMode = Value
                CornerRadius = (18, 4, 4, 18)
            }
            RectBlock Boss : Tint {
                AnchorData.AnchoredPosition = (0, 2)
                AnchorData.SizeDelta = (6, 30)
                CornerRadiusUnitMode = Value
                CornerRadius = (3, 3, 3, 3)
            }
        }

        Widget Potion : Glyph {
            Shown <- Kind == 2
            RectBlock Flask : Metal : Round {
                AnchorData.AnchoredPosition = (0, -8)
                AnchorData.SizeDelta = (40, 40)
            }
            RectBlock Liquid : Tint : Round {
                AnchorData.AnchoredPosition = (0, -12)
                AnchorData.SizeDelta = (30, 26)
                CornerRadius = (1, 0.3, 0.3, 1)
            }
            RectBlock Neck : Metal {
                AnchorData.AnchoredPosition = (0, 16)
                AnchorData.SizeDelta = (14, 14)
                CornerRadius = (0, 0, 0, 0)
            }
            RectBlock Cork : Shade {
                AnchorData.AnchoredPosition = (0, 25)
                AnchorData.SizeDelta = (18, 7)
                CornerRadiusUnitMode = Value
                CornerRadius = (2, 2, 2, 2)
            }
        }

        Widget Gem : Glyph {
            Shown <- Kind == 3
            RectBlock Stone : Tint {
                AnchorData.SizeDelta = (36, 36)
                RenderRotation = (0, 0, 45)
                CornerRadiusUnitMode = Value
                CornerRadius = (3, 3, 3, 3)
            }
            RectBlock Facet : Metal {
                AnchorData.AnchoredPosition = (0, 6)
                AnchorData.SizeDelta = (16, 16)
                RenderRotation = (0, 0, 45)
                CornerRadiusUnitMode = Value
                CornerRadius = (2, 2, 2, 2)
                RenderOpacity = 0.85
            }
        }

        Widget Skill : Glyph {
            Shown <- Kind == 4
            Polygon Frame : Part {
                AnchorData.SizeDelta = (52, 52)
                Sides = 6
                Color = @ui.Accent
            }
            Polygon Core : Part {
                AnchorData.SizeDelta = (40, 40)
                Sides = 6
                Color = @ui.Sunken
            }
            RectBlock Spark : Metal {
                AnchorData.SizeDelta = (16, 16)
                RenderRotation = (0, 0, 45)
                CornerRadius = (0, 0, 0, 0)
            }
        }

        Widget Coin : Glyph {
            Shown <- Kind == 5
            RectBlock Disc : Tint : Round {
                AnchorData.SizeDelta = (46, 46)
            }
            Ring Edge : Part {
                AnchorData.SizeDelta = (34, 34)
                StartAngle = 0
                EndAngle = 360
                LineWidth = 3
                Segment = 48
                Color = @ui.OnAccent
                RenderOpacity = 0.45
            }
            RectBlock Mark : Part {
                AnchorData.SizeDelta = (6, 16)
                BodyColor = @ui.OnAccent
                CornerRadiusUnitMode = Value
                CornerRadius = (3, 3, 3, 3)
                RenderOpacity = 0.6
            }
        }

        Widget Scroll : Glyph {
            Shown <- Kind == 6
            RectBlock Sheet : Metal {
                AnchorData.SizeDelta = (38, 48)
                CornerRadiusUnitMode = Value
                CornerRadius = (3, 3, 3, 3)
            }
            VerticalBox Lines {
                AnchorData.SizeDelta = (24, 26)
                Spacing = 6
                RectBlock L1 : Shade { CornerRadius = (0, 0, 0, 0);  @slot { HorizontalAlignment = Fill  Size = (0, 3) } }
                RectBlock L2 : Shade { CornerRadius = (0, 0, 0, 0);  @slot { HorizontalAlignment = Fill  Size = (0, 3) } }
                RectBlock L3 : Tint { CornerRadius = (0, 0, 0, 0);  @slot { HorizontalAlignment = Left  Size = (14, 3) } }
            }
        }

        Widget Key : Glyph {
            Shown <- Kind == 7
            RenderRotation = (0, 0, -45)
            Ring Bow : Part {
                AnchorData.AnchoredPosition = (0, 14)
                AnchorData.SizeDelta = (20, 20)
                StartAngle = 0
                EndAngle = 360
                LineWidth = 6
                Segment = 40
                Color = @ui.Accent
            }
            RectBlock Shaft : Metal {
                AnchorData.AnchoredPosition = (0, -8)
                AnchorData.SizeDelta = (6, 30)
                CornerRadius = (0, 0, 0, 0)
            }
            RectBlock Bit : Metal {
                AnchorData.AnchoredPosition = (6, -18)
                AnchorData.SizeDelta = (8, 6)
                CornerRadius = (0, 0, 0, 0)
            }
        }
    }
}
```

<a id="item-card-and-item-slot"></a>

### 物品卡和物品槽

用于可供选择的事物列表的卡片，以及用于物品栏网格的方格。二者都把图标放在凹槽中、柔和光晕之后，那里没有任何东西能碰到文字。物品槽的稀有度条是五个部件之一，每个部件都按其各自的 `Rarity` 值显示。

```
// DUI/Blocks/ItemCard.dui
class /Game/UI/Blocks/WBP_ItemCard
use "Theme.dui" as ui
use "Blocks/Icon.dui" as Icon

props {
    Text   Name
    Text   Kind
    Integer Glyph = 0
    Text   Count
    Bool   Selected = false
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Face : ui.Card : ui.FillSlot
    RectBlock Picked : ui.CardSelected : ui.FillSlot {
        Shown <- Selected
    }
    VerticalBox Column : ui.FillSlot {
        Padding = (16, 16, 16, 14)
        Spacing = 10
        // The icon sits in a well of its own, so nothing drawn for it can reach the words below.
        Widget Art {
            + Overlay { }
            @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 112) }
            RectBlock ArtWell : ui.Well : ui.FillSlot
            RectBlock Halo : ui.Glow {
                @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Size = (104, 104) }
            }
            Icon Picture {
                Kind <- Glyph
                @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Size = (72, 72) }
            }
            Text CountText : ui.Caption {
                Text <- Count
                Color = @ui.Ink
                HAlign = Right
                @slot { HorizontalAlignment = Right  VerticalAlignment = Bottom  Padding = (0, 0, 8, 6) }
            }
        }
        Text NameText : ui.Lead {
            Text <- Name
            FontSize = @ui.SizeBody
            @slot HorizontalAlignment = Fill
        }
        Text KindText : ui.Caption {
            Text <- Kind
            Color = @ui.InkSoft
            @slot HorizontalAlignment = Fill
        }
    }
}
```

```
// DUI/Blocks/ItemSlot.dui
class /Game/UI/Blocks/WBP_ItemSlot
use "Theme.dui" as ui
use "Blocks/Icon.dui" as Icon

// One square of an inventory grid. Glyph is the Icon's Kind; Rarity 0 to 4 is common to legendary and colours the
// strip along the bottom; Empty shows the bare well.
props {
    Integer Glyph = 0
    Integer Rarity = 0
    Text    Count
    Bool    Empty = false
    Bool    Selected = false
}

style Strip {
    Raycastable = Disabled
    CornerRadius = (0, 0, 0, 0)
    @slot { HorizontalAlignment = Fill  VerticalAlignment = Bottom  Padding = (8, 0, 8, 6)  Size = (0, 3) }
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Well : ui.Well : ui.FillSlot
    RectBlock Picked : ui.CardSelected : ui.FillSlot {
        Shown <- Selected
    }
    Icon Picture {
        Kind <- Glyph
        Shown <- !Empty
        @slot { HorizontalAlignment = Center  VerticalAlignment = Center  Padding = (0, 0, 0, 6)  Size = (56, 56) }
    }
    RectBlock Common : Strip { BodyColor = @ui.Common;  Shown <- !Empty && Rarity == 0 }
    RectBlock Uncommon : Strip { BodyColor = @ui.Uncommon;  Shown <- !Empty && Rarity == 1 }
    RectBlock Rare : Strip { BodyColor = @ui.Rare;  Shown <- !Empty && Rarity == 2 }
    RectBlock Epic : Strip { BodyColor = @ui.Epic;  Shown <- !Empty && Rarity == 3 }
    RectBlock Legendary : Strip { BodyColor = @ui.Legendary;  Shown <- !Empty && Rarity == 4 }
    Text CountText : ui.Caption {
        Text <- Count
        Color = @ui.Ink
        HAlign = Right
        @slot { HorizontalAlignment = Right  VerticalAlignment = Top  Padding = (0, 6, 8, 0) }
    }
}
```

<a id="hud-plate"></a>

### HUD 底板

HUD 组：透明底板上的名称、等级和两个计量条。HUD 绘制在游戏之上，因此每个组都立在 `ui.Plate` 上，并且在明亮天空或黑暗洞穴上方都保持可读。

```
// DUI/Blocks/HudPlate.dui
class /Game/UI/Blocks/WBP_HudPlate
use "Theme.dui" as ui

// A HUD corner: a name, a level and two meters on a see-through plate that keeps them readable over any scene.
props {
    Text   Name
    Text   Level
    Number Health = 0.8
    Number Stamina = 0.6
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Back : ui.Plate : ui.FillSlot
    VerticalBox Column : ui.FillSlot {
        Padding = (20, 16, 20, 18)
        Spacing = 10
        HorizontalBox Top {
            Spacing = 10
            @slot HorizontalAlignment = Fill
            Text NameText : ui.Lead {
                Text <- Name
                FontSize = @ui.SizeBody
                @fill
            }
            Text LevelText : ui.AccentText {
                Text <- Level
                HAlign = Right
            }
        }
        Native.ProgressBar HealthBar : ui.MeterBad {
            Percent <- Health
            @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 10) }
        }
        Native.ProgressBar StaminaBar : ui.MeterGood {
            Percent <- Stamina
            @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 6) }
        }
    }
}
```

<a id="key-prompt-and-menu-line"></a>

### 按键提示和菜单行

一个键帽及其功能说明，用于屏幕底部的提示；以及垂直菜单中的一行。节点默认是 100 x 100，除非另行指定，而随子项调整自身尺寸的框会把这计算在内，因此键帽及其标签会设置 `AnchorData.SizeDelta = (0, 0)`，使其宽度与按键名称一致。

```
// DUI/Blocks/KeyHint.dui
class /Game/UI/Blocks/WBP_KeyHint
use "Theme.dui" as ui

// A prompt: a key cap and what it does. Give the instance a size in its slot, e.g. @slot Size = (150, 36).
props {
    Text Key
    Text Action
}

Widget Root : ui.FillParent {
    + HorizontalBox { Spacing = 10 }
    Widget Cap {
        // No size of its own (a node's default is 100 x 100): the cap is as wide as its label.
        AnchorData.SizeDelta = (0, 0)
        + Overlay { }
        @slot { VerticalAlignment = Center  MinDesiredSize = (36, 32) }
        RectBlock CapFace : ui.KeyCap : ui.FillSlot
        // Filling the cap, the label measures the cap: as wide as the key's name and its margin, never narrower
        // than the cap's 36.
        Text KeyText : ui.Label {
            Text <- Key
            AnchorData.SizeDelta = (0, 0)
            FontSize = @ui.SizeCaption
            Margin = (10, 0, 10, 0)
            @slot { HorizontalAlignment = Fill  VerticalAlignment = Fill }
        }
    }
    Text ActionText : ui.Caption {
        Text <- Action
        Color = @ui.InkSoft
        @fill
        @slot VerticalAlignment = Center
    }
}
```

```
// DUI/Blocks/MenuItem.dui
class /Game/UI/Blocks/WBP_MenuItem
use "Theme.dui" as ui

// One line of a vertical menu: a label, and a short note on the right. The selected line gets the weak accent fill
// and an accent bar on its left edge.
props {
    Text Label
    Text Note
    Bool Selected = false
}
events {
    Picked
}

Widget Root : ui.FillParent {
    + Overlay { }
    Native.Button Face : ui.TabOff : ui.FillSlot {
        OnClicked -> emit Picked()
    }
    RectBlock Wash : ui.AccentFade : ui.FillSlot {
        Shown <- Selected
        RenderOpacity = 0.25
        Raycastable = Disabled
    }
    RectBlock Edge : ui.AccentBar {
        Shown <- Selected
        Raycastable = Disabled
        @slot { HorizontalAlignment = Left  VerticalAlignment = Fill  Padding = (0, 8, 0, 8)  Size = (3, 0) }
    }
    HorizontalBox Line : ui.FillSlot {
        Padding = (24, 0, 20, 0)
        Raycastable = Disabled
        Text LabelText : ui.Lead {
            Text <- Label
            @fill
            @slot VerticalAlignment = Center
        }
        Text NoteText : ui.Caption {
            Text <- Note
            Color = @ui.InkSoft
            HAlign = Right
            @slot VerticalAlignment = Center
        }
    }
}
```

<a id="turned-in-3d"></a>

### 旋转到 3D

游戏屏幕反复需要的三种旋转，每种都是一个完整屏幕。旋转的方向很容易弄反，本来要朝向中间的面板反而背对它。以下是这些正负号，已对照图片核对：

| 旋转 | 写法 | 图片所示 |
| --- | --- | --- |
| 左侧朝向中间的面板 | `RenderRotation = (0, -14, 0)` | 其右边缘（内侧边缘）更远且绘制得更短 |
| 右侧朝向中间的面板 | `RenderRotation = (0, 14, 0)` | 其左边缘（内侧边缘）更远且绘制得更短 |
| 像从上方俯视的桌面一样向后倾斜的平面 | `RenderRotation = (-35, 0, 0)` | 其上边缘更远且更窄 |
| 朝玩家弯曲的一排卡片 | 中间左侧为负 Yaw，右侧为正 Yaw，两端最大；中间的卡片为 `0` | 每张卡片的内侧边缘更远；中间那张正对玩家 |

正 Yaw 会使节点的右边缘更近，正 Pitch 则使其上边缘更近。`layout` 和 `preview` 用文字说明每个旋转后的面按绘制方向朝向哪里——`Panel RosterLeft, on the left: turned 14 degrees: faces toward the screen centre (right edge farther)`——因此每次旋转后都要读取那一行。

```
// DUI/Blocks/FacingPanels.dui
class /Game/UI/Blocks/WBP_FacingPanels
use "Theme.dui" as ui
use "Blocks/Panel.dui" as Panel
use "Blocks/ListRow.dui" as ListRow

// Two panels that face the middle of the screen, as two team rosters do. A positive Yaw brings a node's right edge
// nearer, so the panel on the left takes a negative Yaw (its inner, right edge goes back) and the panel on the right a
// positive one (its inner, left edge goes back). Each turns about its own centre.
style RowSlot {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 64) }
}
style Dot : ui.FillParent {
    CornerRadius = (1, 1, 1, 1)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            Panel RosterLeft {
                Kicker = "Team one"
                Heading = "Aurora"
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (120, 0)
                AnchorData.SizeDelta = (440, 480)
                RenderRotation = (0, -14, 0)

                ListRow A1 : RowSlot {
                    Label = "Wren Halloway";  Detail = "Vanguard";  Value = "Lv 31"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
                ListRow A2 : RowSlot {
                    Label = "Ossian Marr";  Detail = "Marksman";  Value = "Lv 27"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
                ListRow A3 : RowSlot {
                    Label = "Tamsin Vey";  Detail = "Medic";  Value = "Lv 29"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                }
                ListRow A4 : RowSlot {
                    Label = "Bram Hollis";  Detail = "Sapper";  Value = "Lv 24"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
            }

            Panel RosterRight {
                Kicker = "Team two"
                Heading = "Vesper"
                AnchorData.AnchorMin = (1, 0.5)
                AnchorData.AnchorMax = (1, 0.5)
                AnchorData.Pivot = (1, 0.5)
                AnchorData.AnchoredPosition = (-120, 0)
                AnchorData.SizeDelta = (440, 480)
                RenderRotation = (0, 14, 0)

                ListRow B1 : RowSlot {
                    Label = "Kestrel Dune";  Detail = "Vanguard";  Value = "Lv 30"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
                ListRow B2 : RowSlot {
                    Label = "Ilse Corran";  Detail = "Scout";  Value = "Lv 26"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                }
                ListRow B3 : RowSlot {
                    Label = "Maro Quill";  Detail = "Marksman";  Value = "Lv 28"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
                ListRow B4 : RowSlot {
                    Label = "Sable Arkwright";  Detail = "Engineer";  Value = "Lv 25"
                    slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                }
            }
        }
    }
}
```

```
// DUI/Blocks/TableTop.dui
class /Game/UI/Blocks/WBP_TableTop
use "Theme.dui" as ui

// A plane laid back like a table seen from above at an angle. A negative Pitch takes the top edge away, so the far
// rows recede and draw narrower; a positive one would tip the top edge toward the player. Everything on the board is
// in its plane and leans with it.
style Region {
    AnchorData.AnchorMin = (0.5, 0.5)
    AnchorData.AnchorMax = (0.5, 0.5)
    AnchorData.Pivot = (0.5, 0.5)
    AnchorData.SizeDelta = (240, 132)
    + Overlay { }
}
style RegionName : ui.Lead {
    HAlign = Center
    @slot { HorizontalAlignment = Fill  VerticalAlignment = Center  Padding = (12, 0, 12, 0) }
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            Text Heading : ui.Heading {
                Text = "Campaign Table"
                AnchorData.AnchorMin = (0, 1)
                AnchorData.AnchorMax = (0, 1)
                AnchorData.Pivot = (0, 1)
                AnchorData.AnchoredPosition = (112, -72)
                AnchorData.SizeDelta = (800, 56)
            }

            Widget Board {
                AnchorData.SizeDelta = (1240, 680)
                AnchorData.AnchoredPosition = (0, -60)
                RenderRotation = (-35, 0, 0)

                RectBlock Table : ui.Panel : ui.FillParent
                Widget Saltmarsh : Region {
                    AnchorData.AnchoredPosition = (-380, 150)
                    RectBlock SaltmarshFace : ui.Card : ui.FillSlot
                    Text SaltmarshName : RegionName { Text = "Saltmarsh" }
                }
                Widget Greyhook : Region {
                    AnchorData.AnchoredPosition = (0, 150)
                    RectBlock GreyhookFace : ui.CardSelected : ui.FillSlot
                    Text GreyhookName : RegionName { Text = "Greyhook Pass" }
                }
                Widget Reedbank : Region {
                    AnchorData.AnchoredPosition = (380, 150)
                    RectBlock ReedbankFace : ui.Card : ui.FillSlot
                    Text ReedbankName : RegionName { Text = "Reedbank" }
                }
                Widget Ferry : Region {
                    AnchorData.AnchoredPosition = (-380, -150)
                    RectBlock FerryFace : ui.Card : ui.FillSlot
                    Text FerryName : RegionName { Text = "Old Ferry" }
                }
                Widget Ironmonger : Region {
                    AnchorData.AnchoredPosition = (0, -150)
                    RectBlock IronmongerFace : ui.Card : ui.FillSlot
                    Text IronmongerName : RegionName { Text = "Ironmonger's Row" }
                }
                Widget HeronFen : Region {
                    AnchorData.AnchoredPosition = (380, -150)
                    RectBlock HeronFenFace : ui.Card : ui.FillSlot
                    Text HeronFenName : RegionName { Text = "Heron Fen" }
                }
            }
        }
    }
}
```

```
// DUI/Blocks/CardArc.dui
class /Game/UI/Blocks/WBP_CardArc
use "Theme.dui" as ui
use "Blocks/ItemCard.dui" as ItemCard

// A row of cards that curves toward the player: every card faces the middle, the outer ones turned the most, and the
// middle card square to the player and a little nearer. Left of the middle the Yaw is negative (a card's inner, right
// edge goes back), right of it positive. A positive RenderTranslation X sets the outer cards back; a negative one
// brings the middle card forward.
style CardSlot {
    @slot Size = (200, 250)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            Text Heading : ui.Heading {
                Text = "Choose a Relic"
                HAlign = Center
                AnchorData.AnchorMin = (0.5, 1)
                AnchorData.AnchorMax = (0.5, 1)
                AnchorData.Pivot = (0.5, 1)
                AnchorData.AnchoredPosition = (0, -120)
                AnchorData.SizeDelta = (800, 56)
            }

            HorizontalBox Arc {
                Spacing = 40
                AnchorData.SizeDelta = (1160, 250)
                ItemCard FarLeft : CardSlot {
                    Name = "Tidecaller";  Kind = "Relic";  Glyph = 3
                    RenderRotation = (0, -24, 0)
                    RenderTranslation = (60, 0, 0)
                }
                ItemCard Left : CardSlot {
                    Name = "Ashen Crown";  Kind = "Relic";  Glyph = 4
                    RenderRotation = (0, -12, 0)
                    RenderTranslation = (20, 0, 0)
                }
                ItemCard Middle : CardSlot {
                    Name = "Heron Lantern";  Kind = "Relic";  Glyph = 6;  Selected = true
                    RenderTranslation = (-40, 0, 0)
                }
                ItemCard Right : CardSlot {
                    Name = "Ferryman's Coin";  Kind = "Relic";  Glyph = 5
                    RenderRotation = (0, 12, 0)
                    RenderTranslation = (20, 0, 0)
                }
                ItemCard FarRight : CardSlot {
                    Name = "Saltglass Eye";  Kind = "Relic";  Glyph = 2
                    RenderRotation = (0, 24, 0)
                    RenderTranslation = (60, 0, 0)
                }
            }
        }
    }
}
```

<a id="two-demo-screens"></a>

### 两个演示屏幕

一个屏幕上是面板、属性条、选项卡和列表行；另一个屏幕上是物品卡、每个图标和 HUD 底板。

```
// DUI/Blocks/PanelsDemo.dui
class /Game/UI/Blocks/WBP_PanelsDemo
use "Theme.dui" as ui
use "Blocks/Panel.dui" as Panel
use "Blocks/StatBar.dui" as StatBar
use "Blocks/Tab.dui" as Tab
use "Blocks/ListRow.dui" as ListRow

style RowSlot {
    @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 68) }
}
// A slot with no layout box places what is put in it by its anchors: fill the hole, or it keeps its own size.
style Dot : ui.FillParent {
    CornerRadius = (1, 1, 1, 1)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            Panel Gear {
                Kicker = "Equipment"
                Heading = "Ashwood Longbow"
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (120, 0)
                AnchorData.SizeDelta = (560, 540)

                Text Blurb : ui.Body {
                    Text = "Drawn from a single stave of seasoned ash. Light enough to carry all day."
                    bAutoWrapText = true
                    OverflowType = VerticalOverflow
                    @slot HorizontalAlignment = Fill
                }
                StatBar Draw { Label = "Draw weight";  Value = "48";  Percent = 0.64;  @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 52) } }
                StatBar Reach { Label = "Range";  Value = "62 m";  Percent = 0.78;  @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 52) } }
                StatBar Speed { Label = "Handling";  Value = "31";  Percent = 0.31;  @slot { HorizontalAlignment = Fill  MinDesiredSize = (0, 52) } }
                slot Footer {
                    HorizontalBox Actions {
                        Spacing = 12
                        Native.Button Compare : ui.ButtonSecondary { Text : ui.Label { Text = "Compare" } }
                        Native.Button Equip : ui.ButtonPrimary { Text : ui.LabelOnAccent { Text = "Equip" } }
                    }
                }
            }

            Widget Journal {
                AnchorData.AnchorMin = (0, 0.5)
                AnchorData.AnchorMax = (0, 0.5)
                AnchorData.Pivot = (0, 0.5)
                AnchorData.AnchoredPosition = (760, 0)
                AnchorData.SizeDelta = (640, 540)
                + Overlay { }
                RectBlock JournalFace : ui.Panel : ui.FillSlot
                VerticalBox JournalColumn : ui.FillSlot {
                    Padding = (24, 20, 24, 24)
                    Spacing = 16
                    HorizontalBox Tabs {
                        Spacing = 4
                        Tab Active { Label = "Active";  Selected = true;  @slot Size = (120, 44) }
                        Tab Done { Label = "Done";  @slot Size = (120, 44) }
                        Tab Lore { Label = "Lore";  @slot Size = (120, 44) }
                    }
                    VerticalBox Rows {
                        Spacing = 6
                        @slot HorizontalAlignment = Fill
                        ListRow Q1 : RowSlot {
                            Label = "The Drowned Bell";  Detail = "Saltmarsh, east of the ferry";  Value = "Lv 12";  Selected = true
                            slot Badge { RectBlock : Dot { BodyColor = @ui.Accent } }
                        }
                        ListRow Q2 : RowSlot {
                            Label = "A Debt in Copper";  Detail = "Ironmonger's Row";  Value = "Lv 9"
                            slot Badge { RectBlock : Dot { BodyColor = @ui.Good } }
                        }
                        ListRow Q3 : RowSlot {
                            Label = "Lanterns for the Pass";  Detail = "Greyhook Pass";  Value = "Lv 15"
                            slot Badge { RectBlock : Dot { BodyColor = @ui.Warn } }
                        }
                        ListRow Q4 : RowSlot {
                            Label = "The Ferryman's Coin";  Detail = "Old ferry landing";  Value = "Lv 11"
                            slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                        }
                        ListRow Q5 : RowSlot {
                            Label = "Where the Herons Nest";  Detail = "Reedbank";  Value = "Lv 13"
                            slot Badge { RectBlock : Dot { BodyColor = @ui.InkFaint } }
                        }
                    }
                }
            }
        }
    }
}
```

```
// DUI/Blocks/ItemsDemo.dui
class /Game/UI/Blocks/WBP_ItemsDemo
use "Theme.dui" as ui
use "Blocks/ItemCard.dui" as ItemCard
use "Blocks/HudPlate.dui" as HudPlate
use "Blocks/Icon.dui" as Icon

style CardSlot {
    @slot Size = (168, 220)
}
style IconCell {
    @slot Size = (64, 64)
}

Widget Root : ui.FillParent {
    + Overlay { }
    RectBlock Backdrop : ui.ScreenBackdrop : ui.FillSlot
    ScaleBox Fit : ui.FillSlot {
        Stretch = ScaleToFit
        Widget Design {
            AnchorData.SizeDelta = (1920, 1080)
            @slot Size = (1920, 1080)

            HudPlate Vitals {
                Name = "Wren Halloway"
                Level = "Lv 14"
                Health = 0.72
                Stamina = 0.45
                AnchorData.AnchorMin = (0, 0)
                AnchorData.AnchorMax = (0, 0)
                AnchorData.Pivot = (0, 0)
                AnchorData.AnchoredPosition = (48, 48)
                AnchorData.SizeDelta = (360, 104)
            }

            HorizontalBox Cards {
                Spacing = 16
                AnchorData.SizeDelta = (1136, 220)
                AnchorData.AnchoredPosition = (0, 60)
                ItemCard A : CardSlot { Name = "Longbow";  Kind = "Weapon";  Selected = true }
                ItemCard B : CardSlot { Name = "Buckler";  Kind = "Armour";  Glyph = 1 }
                ItemCard C : CardSlot { Name = "Fen Tonic";  Kind = "Consumable";  Glyph = 2;  Count = "x3" }
                ItemCard D : CardSlot { Name = "River Garnet";  Kind = "Gem";  Glyph = 3 }
                ItemCard E : CardSlot { Name = "Second Wind";  Kind = "Skill";  Glyph = 4 }
                ItemCard F : CardSlot { Name = "Crowns";  Kind = "Currency";  Glyph = 5;  Count = "1,240" }
            }

            HorizontalBox Icons {
                Spacing = 24
                AnchorData.SizeDelta = (704, 64)
                AnchorData.AnchoredPosition = (0, -200)
                Icon I1 : IconCell { Kind = 0 }
                Icon I2 : IconCell { Kind = 1 }
                Icon I3 : IconCell { Kind = 2 }
                Icon I4 : IconCell { Kind = 3 }
                Icon I5 : IconCell { Kind = 4 }
                Icon I6 : IconCell { Kind = 5 }
                Icon I7 : IconCell { Kind = 6 }
                Icon I8 : IconCell { Kind = 7 }
            }
        }
    }
}
```
