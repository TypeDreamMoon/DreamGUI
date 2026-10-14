---
title: 槽位行与组件
description: 用 @slot 和 @fill 告诉父面板怎样摆这个节点，用 + Class 给节点挂行为、布局容器和自身布局对象。
---

一个节点块里有两种行不是在设节点自己：`@slot` 行设的是**父面板**怎么摆它，`+ Class` 块设的是挂在它身上的某个对象。

<a id="面板槽位slot"></a>

## 面板槽位：`@slot`

一个 widget 在面板里——父节点的容器负责排布子节点（盒子、网格、叠放）——时，它有一个**面板槽位**
（`UDreamPanelSlot`）：父节点怎么放它。`@slot` 行设的就是它：

```dui
Widget Root {
    + HorizontalBox { Spacing = 8 }
    Text Label {
        Text = "Volume"
        @slot HorizontalAlignment = Fill
        @slot Padding = (0, 4, 0, 4)
    }
}
```

这个区别值得读两遍：`AnchorData.SizeDelta` 是节点自己的矩形，`@slot SizeRule` 是父面板对它的安排。一个节点被放进不同的面板，
它自己的行不用变，变的是 `@slot` 行。

槽位上常用的属性（完整列表见 [`UDreamPanelSlot`](../Reference/DreamPanelSlot.md)）：

| 属性 | 作用 |
| --- | --- |
| `Padding` | 子节点四周留的空，`(左, 上, 右, 下)`。 |
| `HorizontalAlignment`、`VerticalAlignment` | `Fill`、`Left` / `Top`、`Center`、`Right` / `Bottom`。 |
| `SizeRule`、`FillWeight` | `Auto` 按子节点想要的尺寸，`Fill` 按权重分剩下的空间。 |
| `MinDesiredSize`、`MaxDesiredSize` | 子节点量出来的尺寸的下限和上限，按轴，0 表示不限。 |
| `Row`、`Column`、`RowSpan`、`ColumnSpan` | 网格里的位置和跨度。 |
| `ZOrder` | 兄弟节点间的绘制顺序。Overlay 和 GridPanel 总是读它；CanvasPanel 只在开了 `SortChildrenByZOrder` 时读；别的面板只按兄弟顺序排。 |
| `bFillEmptySpace`、`FillSpanWhenLessThan`、`bForceNewLine` | 只有 WrapBox 读。 |
| `Nudge` | 在面板算好的位置上再加一个固定偏移。 |

父节点不排布面板的节点写了槽位行，是 [DUI5003](../diagnostics/codes.md#dui5003)。

<a id="写成一块"></a>

### 写成一块

几条槽位行可以合成一个块，块里的写法和 `+` 块一样，几条可以写在同一行：

```dui
Widget Root {
    + VerticalBox { }
    Text Note {
        Text = "Unsaved changes"
        @slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }
    }
}
```

<a id="fill-和-fill-2"></a>

### `@fill` 和 `@fill 2`

盒子布局大部分就是这两行，所以它们有简写：

| 简写 | 等于 |
| --- | --- |
| `@fill` | `@slot SizeRule = Fill` |
| `@fill 2` | `@slot SizeRule = Fill` 加 `@slot FillWeight = 2` |

`@fill` 独占一行，或者后面只跟它的权重：

```dui
Widget Root {
    + HorizontalBox { Spacing = 12 }
    Text Label {
        Text = "Master volume"
        @fill
    }
    Text Value {
        Text = "80 %"
        @fill 2
    }
}
```

`Value` 分到的宽度是 `Label` 的两倍。注意设计器写回的一个限制：在设计器里把 `@fill 2` 的 `SizeRule` 改成 Fill 以外的值，
简写写不下这个改动，会被拒绝（[DUI7004](../diagnostics/codes.md#dui7004)），见[设计器写回](write-back.md)。

<a id="组件-class"></a>

## 组件：`+ Class`

`+ Class { … }` 给节点挂一个行为、一个布局容器或一个自身布局对象，块里设那个对象的属性：

```dui
Widget Confirm {
    + UIButton { TransitionType = None }
    + VerticalBox { Spacing = 10  Padding = (24, 28, 24, 28) }
    Text Label { Text = "Confirm" }
}
```

| 挂上去的是 | 例子 | 做什么 |
| --- | --- | --- |
| 行为 | `UIButton`、`UIToggle`、`UIListView` | 交互和状态：点击、开关、列表 |
| 布局容器 | `VerticalBox`、`Overlay`、`SizeBox` | 排布这个节点的子节点 |
| 自身布局 | `Spacer`、`AspectRatio` | 决定这个节点自己的尺寸 |

类按名字查找，会依次试插件的各种前缀：`VerticalBox` 找到竖排盒子容器，`UIButton` 找到按钮行为，写 `Button` 也行。
完整的 `/Script/Module.Class` 路径同样可以（如 `/Script/DreamGUIControls.UIButton`）。解析不到任何 widget 能挂的东西——
不存在、抽象、已废弃——是 [DUI3006](../diagnostics/codes.md#dui3006)。

自身布局对象的一个例子：一个 `Spacer` 放在竖排盒子的 Fill 槽位里，占掉上下两条留下的高度：

```dui
VerticalBox Column {
    Image TopBar {
        Brush.TintColor = #3D7DD8
        @slot SizeRule = Auto
    }
    Widget Gap {
        + Spacer { Size = (32, 32) }
        @fill
    }
    Image BottomBar {
        Brush.TintColor = #3D7DD8
        @slot SizeRule = Auto
    }
}
```

<a id="一个节点只有一个布局容器"></a>

### 一个节点只有一个布局容器

在普通 `Widget` 上写 `+ VerticalBox` 和把类型写成 `VerticalBox` 是同一件事的两种写法，后者更短。第二个布局容器是
[DUI5022](../diagnostics/codes.md#dui5022)：

```text
VerticalBox Column {
    + HorizontalBox { }
}
```

<a id="块里还能写什么"></a>

### 块里还能写什么

行为的 `+` 块里除了属性，还可以写事件路由和绑定，它们找的是这个行为的事件和属性。布局容器的属性不能绑定
（[DUI5008](../diagnostics/codes.md#dui5008)）。

```dui
class /Game/UI/WBP_Menu

Widget Root {
    + VerticalBox { Spacing = 8 }
    Widget Start {
        + UIButton { OnClick -> HandleStart }
        Text { Text = "Start" }
    }
}
```

路由和绑定的写法见[绑定与路由](bindings.md)。样式也能带 `+` 块和槽位行，这样一种栏目就是一个名字，见
[样式与资源](styles-and-resources.md)。
