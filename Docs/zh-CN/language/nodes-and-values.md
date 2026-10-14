---
title: 节点与值
description: 节点怎么写、id 的规则、不写 id 的节点、类型名按什么顺序解析，以及属性能取的每一种值。
---

树由节点组成。一个节点是一个**类型**、一个 **id** 和一个可选的**块**：

```dui
Widget Root {
    Text Title {
        Text = "Settings"
    }
    Image Divider
}
```

块里可以按任意顺序写：属性、`@slot` 行（见[槽位行与组件](slots-and-components.md)）、`+ Component` 块、
子节点，以及控制语句 `if` 和 `for` / `each`（见 [if 与 for](control-flow.md)）。没什么要说的节点不需要块：
单独一行的 `Image Divider` 就是一个节点。

<a id="id"></a>

## id

id 是节点的身份。它同时是 widget 的名字、Blueprint 图表读的那个类成员变量、绑定解析时用的键、节点上字符串的本地化键。所以：

- 它是一个 C++ 标识符：字母、数字和 `_`，不以数字开头（[DUI3002](../diagnostics/codes.md#dui3002)）。
  ASCII 以外的字符算字母，所以 id 可以是中文。
- 它在文件里唯一，比较时**不区分大小写**（[DUI3001](../diagnostics/codes.md#dui3001)）——它变成的变量是 `FName`，`FName` 不区分大小写。
- 它不是关键字：`class`、`style`、`resources`、`slot`、`for`、`each`、`in`、`was`、`use`、`timeline`、`external`、`ease`。

```dui
Widget Root {
    Text 标题 { Text = "设置" }
    Text Subtitle { Text = "音频与画面" }
}
```

下面这样写就是 DUI3001——`title` 和 `Title` 是同一个 id：

```text
Widget Root {
    Text Title
    Text title
}
```

太长、`FName` 装不下的名字是 [DUI1006](../diagnostics/codes.md#dui1006)。

<a id="样式子句与改名子句"></a>

### 样式子句与改名子句

id 后面可以跟两个子句，顺序随意：

- `: StyleName`——节点穿上一个样式，见[样式与资源](styles-and-resources.md)。
- `(was: OldId)`——节点改过名。下一次编译会把指向旧 id 的东西——图表引用、绑定、动画轨道——都搬到新 id 上。

```dui
style Caption { FontSize = 14 }

Widget Root {
    Text Heading (was: Title) : Caption { Text = "Settings" }
}
```

这个子句要保留到文件至少编译过一次。编译之前再改一次名，保留**第一个**旧 id。改名可能被拒绝的几种情况：

| 代码 | 情况 |
| --- | --- |
| [DUI3010](../diagnostics/codes.md#dui3010) | `(was: X)` 时 X 仍是文件里某个节点的 id |
| [DUI3011](../diagnostics/codes.md#dui3011) | 两个节点都声称 `(was: X)` |
| [DUI3012](../diagnostics/codes.md#dui3012) | 名为 X 的节点写了 `(was: X)` |
| [DUI3013](../diagnostics/codes.md#dui3013) | 旧 id 同时是类的另一个成员的名字，图表引用不搬（警告） |
| [DUI2008](../diagnostics/codes.md#dui2008) | `(was: …)` 里不是恰好一个 id |

在 VS Code 里按 F2 给节点改名会替你写上这个子句，改回去时再把它删掉；设计器里改名也会写它。

<a id="不写-id-的节点"></a>

## 不写 id 的节点

没有谁按名字引用的节点可以省掉 id，只要类型后面跟着一个块或一个样式子句：

```dui
style Caption { FontSize = 14 }

Widget Root {
    HorizontalBox {
        Spacing = 14
        Text { Text = "Status" }
        Text : Caption { Text = "Ready" }
    }
}
```

解析器会给这样的节点造一个 id：`<父 id>__<类型><n>`——最近一个有 id 的外层节点的 id（没有就是 `Root`）、两个下划线、
类型（id 装不下的字符都换成 `_`）、以及它前面同类型的无名兄弟节点的个数。上例里那个 `HorizontalBox` 是
`Root__HorizontalBox0`，两个 `Text` 是 `Root__HorizontalBox0__Text0` 和 `Root__HorizontalBox0__Text1`。类型 `nier.Row`
在 id 里写作 `nier_Row`，`@Row` 写作 `_Row`。造出来的 id 撞上作者写的 id 时，后面再加 `_<n>`。

同样的文本总是得到同样的 id，所以只要文件的形状不变，它们和手写的 id 一样稳定；但在某个无名节点**前面**插入一个同类型的无名兄弟，
后面那些就会重新编号。VS Code 里悬停在无名节点上会显示它编译成的 id。

无名节点仍然有类成员变量——运行时靠它找到绑定所在的 widget——但那是一个隐藏变量：Blueprint 图表看不到它。
代码要用到某个节点，就给它一个 id。无名节点不能写 `(was: …)`（[DUI2004](../diagnostics/codes.md#dui2004)）：
没有旧名可改。

一行里只有一个类型、没有 id、没有块也没有样式（如单独的 `Text`）仍然是 DUI2004：它同样可能是一个漏了 `=` 的属性。

```text
Widget Root {
    Text
}
```

<a id="节点类型"></a>

## 节点类型

节点类型的含义按下面的顺序判定，先匹配的算数：

1. **内置标签**：`Widget`（一个没有视觉的矩形，最常用的容器），或者某个视觉的标签——`Text`、`Image`、`RectBlock`、
   `Sprite`、`Texture`、`Ring`、`Polygon`、`PolygonLine`、`Line2DRaw`、`Line2DChildren`、`Empty`、`StaticMesh`、
   `BackgroundBlur`、`BackgroundPixelate`、`PixelSort`、`PostProcessRenderElement`、`PostProcessRenderElementText`、
   `CanvasRenderTargetPreviewer`、`UMGWidget`。插件可以用 `DECLARE_DREAM_GUI_VISUAL` 加视觉。
2. **布局容器**：`VerticalBox`、`HorizontalBox`、`StackBox`、`Overlay`、`CanvasPanel`、`GridPanel`、`UniformGridPanel`、
   `WrapBox`、`SizeBox`、`ScaleBox`、`SafeZone`、`ScrollBox`、`WidgetSwitcher`、`Border`、`MenuAnchor`。
   这样的节点是一个挂着该容器的普通 widget，它自己的行既能设容器的属性，也能设 widget 的属性。
3. **组件别名**：`use … as` 起的名字——`Row`，或者命名空间带进来的 `nier.Row`。见 [`use`](use.md)。
4. **`@Name`**：某个 `resources` 块里 `Asset` 条目指向的 widget 类。这是用短名指代组件的旧写法，`use … as` 才是首选。
5. **注册标签**：`Scope.Name`，用 `DECLARE_DREAM_GUI_WIDGET` 注册的 widget 类。插件的控件都在 `Native` 下：
   `Native.Button`、`Native.Toggle`、`Native.Slider`、`Native.SpinBox`、`Native.ProgressBar`、`Native.Dropdown`、
   `Native.TextInput`、`Native.EditableText`、`Native.MultiLineEditableText`、`Native.RichText`、`Native.ScrollBox`、
   `Native.ScrollBar`、`Native.List`、`Native.TileView`、`Native.TreeView`、`Native.TabView`、`Native.ExpandableArea`、
   `Native.Border`、`Native.Dialog`、`Native.MenuAnchor`、`Native.RingMenu`、`Native.RadioButton`、
   `Native.InputKeySelector`、`Native.Throbber`、`Native.NativeWidgetHost`。见[控件](../controls/index.md)。
6. **资产路径**：`/Game/UI/WBP_Row`，原生类则写 `/Script/MyGame.MyWidget`。

六种写在一起：

```dui
use "UI/Components/Row.dui" as Row
use "UI/Common.dui" as ui

resources {
    Asset Badge = /Game/UI/WBP_Badge
}

VerticalBox Root {
    Text Title { Text = "Types" }                       // 1 内置标签
    HorizontalBox Bar { Spacing = 8 }                   // 2 布局容器
    Row Audio { Label = "Audio" }                       // 3 组件别名
    ui.Row Video { Label = "Video" }                    // 3 命名空间里的别名
    @Badge NewBadge { }                                 // 4 Asset 资源
    Native.Button Confirm { }                           // 5 注册标签
    /Game/UI/WBP_Row Language { Label = "Language" }    // 6 资产路径
}
```

没有一种对得上的类型是 [DUI3003](../diagnostics/codes.md#dui3003)。

<a id="容器作为类型"></a>

### 容器作为类型

```dui
use "UI/Components/Row.dui" as Row

style Lists { AnchorData.SizeDelta = (0, 400) }

VerticalBox Categories : Lists {
    Spacing = 29
    Row Audio { Label = "Audio" }
}
```

和下面是同一棵树：

```dui
use "UI/Components/Row.dui" as Row

style Lists { AnchorData.SizeDelta = (0, 400) }

Widget Categories : Lists {
    + VerticalBox { Spacing = 29 }
    Row Audio { Label = "Audio" }
}
```

widget 和容器都有的属性名，算 widget 的。一个节点只有一个布局容器：类型已经是容器的节点再写 `+ HorizontalBox`
是 [DUI5022](../diagnostics/codes.md#dui5022)。

<a id="组件实例"></a>

### 组件实例

类型是一个类（上面 3 到 6）的节点，是那个组件的一个**实例**：它的内容来自那个类，它的行设的是那个类的属性。
这个类必须是一个具体的 DreamUI user widget（[DUI5006](../diagnostics/codes.md#dui5006)）；路径加载不到是
[DUI5001](../diagnostics/codes.md#dui5001)。怎样写一个组件见[编写组件](components.md)。

因为内置标签和容器最先判定，别名永远改变不了 `Text` 或 `VerticalBox` 的含义；试图这么做的别名会被拒绝
（[DUI3018](../diagnostics/codes.md#dui3018)）。

编辑器启动时、以及执行 `DreamUI.ExportSymbols` 时，会写出 `DUI/.dui-symbols.json`：每个标签、容器和注册 widget
连同它们的属性，供编辑器补全使用。

<a id="属性"></a>

## 属性

```dui
Widget Root {
    AnchorData.SizeDelta = (400, 240)
    + Overlay { }
    Text Title { FontSize = 24 }
}
```

`Name = Value`（没有 `=` 或箭头、或者后面什么也没有，是 [DUI2005](../diagnostics/codes.md#dui2005)）。名字是一个反射属性，或者一条点分路径，指进一个结构体属性里（`AnchorData.SizeDelta`）。
裸名字的查找顺序是：先 widget，再它的视觉，再（类型是容器的节点）它的容器，最后它的各个行为；第一个有这个属性的拿走。

- 谁都没有的名字是 [DUI4001](../diagnostics/codes.md#dui4001)，并给出最接近的名字。比如 `Width = 400` 会指向
  `AnchorData.SizeDelta`。
- 点分路径的头能解析、尾不能是 [DUI4002](../diagnostics/codes.md#dui4002)。
- 节点类型并不创建的视觉上的属性是 [DUI5002](../diagnostics/codes.md#dui5002)。
- 文本写不了的属性——transient、已废弃、对象图的一部分、委托——是 [DUI4006](../diagnostics/codes.md#dui4006)。

同一个块里把一个属性写两次，后写的赢。节点自己的行赢过它样式里的行。

<a id="值"></a>

## 值

| 种类 | 例子 | 说明 |
| --- | --- | --- |
| 数字 | `24`、`0.95`、`-3`、`1e-45` | 按属性的数值类型读。 |
| 字符串 | `"Settings"`、`"a \"quote\""` | 转义：`\"`、`\\`、`\n`、`\t`、`\r`。写在 `FText` 上时会被本地化。 |
| 颜色 | `#FFF`、`#FFFF`、`#1E1E1E`、`#1E1E1EFF` | 3、4、6 或 8 位十六进制，sRGB。 |
| 元组 | `(400, 240)`、`(0, 8, 0, 0)` | 向量、边距、旋转——属性是什么结构体就是什么。可以跨行。 |
| 词 | `Left`、`Collapsed`、`true`、`None` | 枚举值、布尔值，或表示空引用的 `None`。 |
| 资产路径 | `/Game/UI/T_Icon`、`"/Game/UI/T_Icon.T_Icon"` | 对象或类引用。加引号也行。 |
| 资源 | `@Accent` | 某个 `resources` 条目的值，见[样式与资源](styles-and-resources.md)。 |
| 节点 id | `CheckMark` | 写在持有 widget、视觉或行为的属性上时，指本文件里那个 id 的节点。 |

AST 只记录值的**形状**，不记录含义：`Left` 在解析时只是一个词，`(400, 240)` 只是一个二元组；它最终是枚举值还是
`FVector2D`，由它写到的那个属性决定。

```dui
class /Game/UI/WBP_Values

resources {
    Color Accent = #FF6600
}

Widget Root {
    AnchorData.SizeDelta = (400, 240)                // 元组，写进结构体 AnchorData
    + Overlay { }

    Text Title {
        Text     = "Settings"                        // 字符串，写在 FText 上会被本地化
        FontSize = 24                                // 数字
        Color    = @Accent                           // 资源
        Font     = /DreamGUI/DefaultFont_DistanceField   // 资产路径
        HAlign   = Center                            // 词：枚举值
    }

    Image Swatch {
        Brush.TintColor = #1E1E1EFF                  // 颜色
        @slot Padding = (0, 8,
                         0, 0)                       // 元组可以跨行
    }

    Widget Mute {
        + UIToggle {
            bIsOn = false                            // 词：布尔值
            ToggleTransitionTarget = Check           // 节点 id：下面那个 Image 的视觉
        }
        Image Check
    }
}
```

每种值各自出错的方式：

- **数字**读不出来——两个小数点、结尾一个点、后面粘着单位（`24px`）——是 [DUI1004](../diagnostics/codes.md#dui1004)。
- **字符串**到行尾还没闭合是 [DUI1002](../diagnostics/codes.md#dui1002)。没有 `\uXXXX`：`.dui` 文件是 UTF-8，直接写那个字符。
- **颜色**位数不是 3、4、6、8 是 [DUI1005](../diagnostics/codes.md#dui1005)。
- **元组**元素个数不对是 [DUI4004](../diagnostics/codes.md#dui4004)；形状根本不可能是这个属性的类型是
  [DUI4003](../diagnostics/codes.md#dui4003)。
- **词**不是属性的枚举里声明过的值是 [DUI4005](../diagnostics/codes.md#dui4005)。
- **资产路径**加载不到是 [DUI5001](../diagnostics/codes.md#dui5001)，长到 `FName` 装不下是
  [DUI1007](../diagnostics/codes.md#dui1007)。
- **资源**名不存在是 [DUI4007](../diagnostics/codes.md#dui4007)。
- **节点 id** 在文件里找不到对应节点是 [DUI5013](../diagnostics/codes.md#dui5013)。

<a id="标志位枚举"></a>

### 标志位枚举

语言里没有 `|`。标志位枚举的组合写成各个标志相加得到的**数字**。编译器会对照枚举校验这个数，写回时也按数字打印，
所以来回一趟不丢东西。

<a id="本地化键"></a>

### 本地化键

字符串可以带上它的本地化键：

```dui
class /Game/UI/WBP_Dialog

Widget Root {
    + HorizontalBox { Spacing = 16 }
    Text Confirm { Text = "OK" @key("Dialog.Confirm") }
    Text Hint { Text = "Press any key" }
}
```

不写时，键是 `<节点 id>.<属性>`（上例的 `Hint.Text`）；`+` 块里的行，键的两段之间还会加上组件的类和它的位置。
命名空间是 `class` 行给的路径（这里是 `/Game/UI/WBP_Dialog`），没有 `class` 行就用文件名。所以给节点改 id 会改变它的键：
VS Code 改名时会提醒这一点，并告诉你可以用 `@key` 把旧键钉住。`@key(…)` 里不是恰好一个字符串、或者跟在一个非字符串的值后面，
是 [DUI2009](../diagnostics/codes.md#dui2009)。
