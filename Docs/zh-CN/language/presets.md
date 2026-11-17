---
title: "预设"
description: "四种现成的 .dui 界面外观 —— Default、Ember、Signal 和 Paper —— 以库的形式声明相同的颜色、尺寸和样式，项目只需改动一行就能更换整体外观；以及每个名称的用途。"
---

<a id="presets"></a>

## 预设

DreamGUI 附带四个预设：它们是颜色、字号、间距和样式的库，界面直接套用它们，而不必自己编写十六进制颜色和像素尺寸。它们位于插件的 `DUI/Presets/` 文件夹中。每个预设都是一个库 —— 只有 `resources` 和 `style` 块，没有根节点，也没有 `class` 行 —— 因此它本身不会编译出任何内容，也不会写入任何资产。

| 预设 | 外观 | 强调色 |
| --- | --- | --- |
| `Default` | 石墨灰，圆角 | 柔和的蓝色 |
| `Ember` | 暖炭灰与羊皮纸色的墨色，小圆角 | 琥珀色 |
| `Signal` | 蓝黑玻璃，紧凑的圆角 | 薄荷绿 |
| `Paper` | 暖白纸张与深色墨色（浅色主题） | 朱红 |

这四个预设都低饱和度、只用一种强调色，各有三级文字颜色和三级表面颜色，并且每种文字颜色在其适用的每个表面上都达到 4.5 : 1 的对比度。它们声明相同的名称，只有值不同。

<a id="naming-the-preset-once"></a>

### 只命名一次预设

项目在一个文件中命名自己的预设，即 `DUI/Theme.dui`，该文件只包含 `use` 行。每个界面和每个组件都通过它获取预设，命名空间为 `ui`：

```
// DUI/Theme.dui
// The one line that picks the look of every screen and component in the project.
use "Plugin.DreamGUI:Presets/Default.dui"
```

```
use "Theme.dui" as ui
```

把 `Theme.dui` 中的 `Default` 改为 `Ember`、`Signal` 或 `Paper`，会在下次编译时改变所有使用它的界面和组件的外观。单个界面也可以直接命名预设，即 `use "Plugin.DreamGUI:Presets/Ember.dui" as ui`，但这样一来，它的组件会保持各自 `use` 行所赋予的外观。

要制作自己的预设，请将一个预设复制到项目的 `DUI/` 文件夹中（不要复制到 `DUI/Presets/`，那会遮蔽插件的预设），修改其值并保留其名称，然后让 `Theme.dui` 指向它。

<a id="using-the-names"></a>

### 使用这些名称

- 写成 `Color = @ui.Ink`、`BodyColor = @ui.Surface`、`FontSize = @ui.SizeBody`、`Spacing = @ui.Gap4`：绝不要使用预设已经命名的十六进制颜色或原始尺寸。只引用预设值的界面会随预设一起改变外观。
- 套用样式：`Text : ui.Title`、`RectBlock : ui.Panel`、`Native.Button : ui.ButtonPrimary`。节点自身的行优先于其样式中的行，因此 `Text : ui.Caption { Color = @ui.Ink }` 是使用主文字颜色的说明文字。
- 所有尺寸都是以 1920 x 1080 设计尺寸为基准的像素值。请在会缩放至视口的设计画布上布局界面（[构建块](building-blocks.md#the-root-of-a-screen) 就从这样的画布开始）：在 1280 x 720 下，20 px 的正文文字会绘制为 13 px，与游戏中一致。
- 资源代表一个完整的值。它不能作为元组的一部分：`Padding = (@ui.Gap4, 0, @ui.Gap4, 0)` 会得到 DUI4003。请写出具体数字，或者通过样式设置 `Spacing` 和该盒子的 `Padding`。

<a id="colours"></a>

### 颜色

| 名称 | 用途 |
| --- | --- |
| `Base` | 所有内容背后的屏幕（由 `ScreenBackdrop` 调节） |
| `Surface` | 面板的主体 |
| `Raised` | 面板上的卡片、键帽、次要按钮 |
| `Sunken` | 凹陷进面板的部分：空槽位、凹槽 |
| `Edge`, `EdgeStrong` | 面板的细线边缘和分隔线；为被框住的内容提供更强的边缘 |
| `Ink`, `InkSoft`, `InkFaint` | 文字：最先阅读的内容、辅助阅读的内容、可以跳过的内容 |
| `Accent` | 唯一的强调色：主要按钮、选中标签页的条、进度条的填充 |
| `AccentSoft` | 强调色的浅填充：选中行、选中的标签页、chip |
| `AccentGlow`, `AccentEdge` | 透明版本的强调色：光晕、选中卡片的边缘 |
| `OnAccent` | 强调色上的文字（主要按钮的标签） |
| `Good`, `Bad`, `Warn` | 状态：治疗、获得、完成；受伤、失去；即将耗尽 |
| `Common`, `Uncommon`, `Rare`, `Epic`, `Legendary` | 物品稀有度 |
| `Scrim` | HUD 组下方的透明底板，在任何场景上都清晰可读 |
| `Shadow` | 面板下方的阴影 |

<a id="sizes"></a>

### 尺寸

| 名称 | 值 |
| --- | --- |
| `SizeCaption`, `SizeBody`, `SizeLead`, `SizeTitle`, `SizeHeading`, `SizeDisplay` | 16, 20, 24, 30, 44, 72 |
| `Gap1` 到 `Gap8` | 4, 8, 12, 16, 24, 32, 48, 64 |
| `RadiusSmall`, `Radius`, `RadiusLarge` | 预设的圆角，以像素为单位（Signal 的最紧凑） |
| `EdgeWidth` | 细线边缘的宽度 |
| `FontRegular`, `FontBold` | 插件的距离场字体 |

<a id="styles"></a>

### 样式

| 样式 | 应用于 | 用途 |
| --- | --- | --- |
| `Display`, `Heading`, `Title` | `Text` | 屏幕的名称；页面的标题；面板或卡片的标题。粗体，`Ink` |
| `Lead`, `Body`, `Caption` | `Text` | 开篇的一行；正文文本（`InkSoft`）；小字说明（`InkFaint`） |
| `Overline` | `Text` | 一组内容上方的小号加宽大写标签：SECTION、SLOT |
| `Figure` | `Text` | 作为其方框重点的数字：等级、价格 |
| `AccentText`, `GoodText`, `BadText`, `WarnText` | `Text` | 使用强调色或状态色的文本 |
| `Label`, `LabelOnAccent`, `LabelQuiet` | `Text` | 按钮、标签页或键帽内的标签，居中 |
| `ScreenBackdrop` | `RectBlock` | 整屏所处的底色：基础色，视线落点处更亮 |
| `Panel` | `RectBlock` | 面板：渐变的主体、细线边缘、圆角、柔和阴影 |
| `Card`, `CardSelected` | `RectBlock` | 面板上的卡片；同一张卡片被选中时 |
| `Well` | `RectBlock` | 凹陷于面板之中：空槽位、图标的边框 |
| `Plate` | `RectBlock` | HUD 分组下方的半透明底板 |
| `Chip`, `KeyCap` | `RectBlock` | 小标签；按键提示的键帽 |
| `Divider`, `AccentBar`, `AccentFade` | `RectBlock` | 细线；短的实心强调色条；向右淡出的强调色。它们没有自身尺寸：需为它们指定 `@slot Size` 或 `SizeDelta` |
| `Glow` | `RectBlock` | 强调色的柔和圆形光晕，位于数字或图标之后。绝不覆盖在文本上 |
| `ButtonPrimary`, `ButtonSecondary` | `Native.Button` | 界面最希望用户执行的那个操作；其他所有操作 |
| `TabOn`, `TabOff` | `Native.Button` | 选中的标签页；其他标签页 |
| `Meter`, `MeterGood`, `MeterBad`, `MeterWarn` | `Native.ProgressBar` | 使用强调色或状态色的进度条。需在其槽位中指定高度 |
| `FillParent` | 任意节点 | 锚定到父级的四个角：填满父级 |
| `FillSlot` | 面板中的任意节点 | 双向填满其槽位，没有自身尺寸 |

控件样式会设置 `StyleSource = Inline`，因此无论项目是否有样式表，它们都会生效。
