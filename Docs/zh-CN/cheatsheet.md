<a id="dui-cheat-sheet"></a>

# .dui 速查表

`.dui` 语言的一页速查：文件的形态，节点、属性与值的写法，样式与资源，绑定，控制流与时间轴，以及最耗时间的那些代码。本页中每个完整文件——首行为 `// DUI/<path>.dui` 的块——都由测试套件编译并布局，无错误也无警告。要了解某一行背后的规则，请阅读[语言](language/overview.md)；要了解某个代码，请阅读[诊断](diagnostics/index.md)，或同一份数据的 JSON 形式，[`diagnostics/codes.json`](../diagnostics/codes.json)。

`.dui` 不是 QML、XAML、UMG 或 CSS。请查阅每个节点类型和属性（DreamMCP 的 `lookup`，或 `DUI/.dui-symbols.json`），而不是靠猜：未知名称是错误，而不是空操作。

<a id="a-file"></a>

## 文件

文件位于 `DUI/` 文件夹下（项目的或某个插件的），绝不位于 `Content/` 下。它是语句的列表，每行一条或由 `;` 分隔，顺序任意：

| 语句 | 用途 |
| :-- | :-- |
| `class /Game/UI/WBP_Settings` | 此文件作为其源的控件蓝图；至多一个 |
| `use "Panels/Row.dui" as Row` | 另一个文件（一个组件，或一个样式与资源库） |
| `resources { … }` | 命名常量：`@Accent` |
| `style Name { … }` | 一组命名语句，节点通过 `: Name` 套用 |
| `props { … }` · `events { … }` · `viewmodels { … }` | 此文件的类所声明的内容 |
| `timeline Name { … }` | 一段动画 |
| 一个节点 | 树的根；没有根的文件是一个库 |

<a id="the-look-one-preset-named-once"></a>

## 外观：一个预设，只命名一次

从预设中取用每种颜色、尺寸和外观，而不是写十六进制颜色和像素尺寸。项目在一个文件中命名其预设，每个屏幕都以 `ui` 使用它（[预设](language/presets.md)）：

```
// DUI/Theme.dui
// The one line that picks the look of every screen and component in the project.
use "Plugin.DreamGUI:Presets/Default.dui"
```

然后 `Color = @ui.Ink`、`FontSize = @ui.SizeBody`、`Spacing = @ui.Gap4`，以及样式：`Text : ui.Title`、`RectBlock : ui.Panel`、`Native.Button : ui.ButtonPrimary`。颜色：`Base Surface Raised Sunken Edge EdgeStrong Ink InkSoft InkFaint Accent AccentSoft AccentGlow OnAccent Good Bad Warn Scrim`。尺寸：`SizeCaption SizeBody SizeLead SizeTitle SizeHeading SizeDisplay`、`Gap1`..`Gap8`（4 到 64）、`RadiusSmall Radius RadiusLarge`。样式：在 `Text` 上用 `Display Heading Title Lead Body Caption Overline Figure AccentText GoodText BadText WarnText Label LabelOnAccent LabelQuiet`；在 `RectBlock` 上用 `ScreenBackdrop Panel Card CardSelected Well Plate Chip KeyCap Divider AccentBar AccentFade Glow`；在 `Native.Button` 上用 `ButtonPrimary ButtonSecondary TabOn TabOff`；在 `Native.ProgressBar` 上用 `Meter MeterGood MeterBad MeterWarn`；`FillParent`、`FillSlot` 可用于任何对象。

<a id="a-screen"></a>

## 屏幕

一个组件——一个被其他文件按名称放置的文件——以及一个放置它的屏幕。屏幕的根是适配所有视口的那个：一个填满它的背景，以及一个位于 `ScaleBox` 中的 1920 × 1080 设计画布，用 `@slot Size` 告诉该盒子其尺寸。画布之后的一切都以设计像素计，锚定到它的边缘。

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

更多起点：[构建块](language/building-blocks.md)（面板、卡片、标签页、列表行、状态条、占位图标、HUD 板、按键提示）以及[起始屏幕](language/starter-screens.md)。

<a id="nodes"></a>

## 节点

```
Text Title {
    Text = "Settings"
}
```

节点是 `Type Id { … }`；块中包含属性、`@slot` 行、`@tag` 行、`+ Component` 块、子节点、`if` 以及 `for`/`each`。id 是一个 C++ 标识符，在文件中忽略大小写唯一。没有东西引用的节点，如果后面跟有块或样式，可以省略 id：`Text : Caption { Text = "Ready" }`。

<a id="values"></a>

## 值

| 种类 | 示例 |
| :-- | :-- |
| 数字 | `24`, `0.95`, `-3` |
| 字符串 | `"Settings"` — 在 `FText` 上本地化；其后可跟 `@noloc`、`@note("…")`、`@max(16)` |
| 颜色 | `#1E1E1E`、`#1E1E1ECC` — **sRGB** |
| 元组 | `(400, 240)`、`(0, 8, 0, 0)` — 一个向量、一个边距，或该属性所需的任何结构体 |
| 词 | `Left`, `Collapsed`, `true`, `None` |
| 资产路径 | `/Game/UI/T_Icon` |
| 资源 | `@Accent` |
| 列表/映射/结构体 | `[1, 2]` · `{ Hover: #FFCC00 }` · `{ Distance: 12, Angle: 45 }` |

**位置：** 在盒子之外，节点由 `AnchorData` 放置——`AnchorMin`/`AnchorMax` 为 0 到 1，**(0, 0) 为左下角**、`Pivot`、`AnchoredPosition`（正 Y 为**向上**）、`SizeDelta`。`AnchorMin = (0, 0)`、`AnchorMax = (1, 1)`、`SizeDelta = (0, 0)` 填满父级。在盒子内部，盒子通过 `@slot` 行来放置它。`RenderTranslation = (X, Y, Z)`：X 是深度（负值更近），Y 向右，Z 向上。`RenderRotation = (Pitch, Yaw, Roll)`：正的 Yaw 会让右边缘更近，因此位于左侧的面板用负的 Yaw 朝向中间，位于右侧的面板用正的 Yaw；负的 Pitch 会让平面向后倾倒（上边缘更远）。`layout` 说明每个转过方向的面朝向哪边。

<a id="slots-and-components"></a>

## 插槽与组件

```
Text Label {
    @slot HorizontalAlignment = Fill
    @slot Padding = (0, 4, 0, 4)
}
```

`@slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }` 一次设置多个；`@fill` 即 `@slot SizeRule = Fill`，`@fill 2` 增加权重。`+ Class { … }` 块为节点添加一个组件（一种行为）。

<a id="styles-and-resources"></a>

## 样式与资源

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

<a id="bindings"></a>

## 绑定

```
Text <- GetTitle()
RenderOpacity <- Fade() * 0.8
Shown <- Count() > 0 && !IsLocked()
```

- `<-` 由函数、变量、成员路径或表达式驱动属性（`||` `&&` `==` `<` `+` `*` `!`；没有 `/`，没有 `? :`）。
- `->`、`+=` 和 `=` 将事件路由到函数；`<->` 双向镜像属性；`Shown <- …` 显示或折叠节点。

<a id="control-flow"></a>

## 控制流

```
if HasSave() {
    Row Continue { Label = "Continue" }
} else if IsLoading() {
    Native.Throbber Spinner { }
} else {
    Text NoSave { Text = "No save data" }
}
```

`for Item in Items() { … }` 按元素将其主体复制到面板中；`each Items() { … }` 填充列表视图的单元格。

<a id="timelines"></a>

## 时间轴

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

<a id="mistakes-that-cost-the-most-time"></a>

## 最耗时间的错误

| 代码 | 严重性 | 消息 |
| :-- | :-- | :-- |
| [DUI3003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui3xxx#dui3003) | 错误 | 类型既不是标签、容器、别名、已注册控件，也不是路径。 |
| [DUI4001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui4xxx#dui4001) | 错误 | 没有该名称的属性 |
| [DUI5002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5002) | 错误 | 节点的类型并不创建的视觉元素的属性。 |
| [DUI5005](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui5xxx#dui5005) | 错误 | 没有 setter 的绑定属性。 |
| [DUI2006](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui2xxx#dui2006) | 错误 | 编译为类的文件中没有根节点，或存在多个根节点。 |
| [DUI8001](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8001) | 警告 | 节点可见的部分超出视口边缘 1 px 以上，或整体位于视口之外 |
| [DUI8002](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8002) | 警告 | 因溢出策略而被截断，或排版超出其盒子的文本 |
| [DUI8003](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8003) | 警告 | 绘制或可被按下的节点宽度或高度不足 1 px（对带子项的普通控件为信息级），或小于 24 x 24 px 的可按下控件。 |
| [DUI8004](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8004) | 警告 | 同一画布中的两个可按下控件在屏幕上于两个轴向均重叠 2 px 或更多，无论它们位于树中的何处 |
| [DUI8008](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8008) | 警告 | 绘制超出其所在面板或卡片超过 4 px（及其尺寸的 5%）的节点 |
| [DUI8009](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8009) | 警告 | 不接收指针输入的视觉元素，在其画布中绘制于某段文本之后并覆盖其 12% 或更多的字形——或是一个大部分落在文本行内的小装饰。 |
| [DUI8010](https://gui.toolchain.64hz.cn/en/docs/diagnostics/dui8xxx#dui8010) | 信息 | 内容偏居一侧（空出 38% 或更多的条带）、中间横贯一条宽空白带，或各行分布过稀的面板 |

- 项目存在样式表时，`Style.X = v` 行不会产生任何效果：请先写入 `StyleSource = Inline`。
- 颜色字面量是 sRGB；`FLinearColor` 属性会对其解码，因此 `#808080` 约为 0.22。
- `ScaleBox` 中的 1920 × 1080 设计画布除了需要其 `SizeDelta` 外，还需要 `@slot Size = (1920, 1080)`，且背景要放在盒子之外。没有槽尺寸时，盒子会按内容测量画布，并把整个屏幕缩放到边缘之外。请复制上文中该屏幕的根节点。
- 本意是要溢出其面板或覆盖在文本之上的装饰（发光、光泽）会用 `@tag Bleed` 声明这一点；否则就会报 DUI8008 或 DUI8009。
- 屏幕能编译并不代表完成：请在 `1280x720, 1920x1080, 2560x1440` 下对它执行 `layout`，并修复每一个 DUI8xxx。

<!-- Generated by .skill/gen-knowledge.ps1 from .skill/cheatsheet/cheatsheet.src.md. Edit that file, not this one. -->
