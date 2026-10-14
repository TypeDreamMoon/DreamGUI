---
title: 样式与资源
description: 样式是一组具名的行，可以继承、可以带组件和槽位行；资源是带类型的具名常量。
---

样式和资源都是为了不把同一个值写很多遍，但它们是两种东西：**样式**是一组按名字套到节点上的行，**资源**是一个按名字引用的值。

<a id="样式"></a>

## 样式

样式是一组具名的行，用 `: Name` 套到节点上：

```dui
style Caption {
    FontSize = 14
    Color    = #8C93A6
}

style Warning : Caption {
    Color = #E05A47
}

Widget Root {
    + VerticalBox { }
    Text Note : Warning { Text = "Unsaved changes" }
}
```

`Note` 最后是 14 号字、颜色 `#E05A47`。规则：

- 样式可以继承另一个样式（`style Warning : Caption`）：先套基样式的行，再套派生样式的，最后是节点自己的。
  兜回自己的继承链是 [DUI3015](../diagnostics/codes.md#dui3015)。
- `: Name` 指向一个本文件看不到的样式是 [DUI3004](../diagnostics/codes.md#dui3004)。
- 本地样式会遮住 `use` 进来的同名样式。一个文件里两个同名的本地样式是 [DUI3005](../diagnostics/codes.md#dui3005)。
- 样式的声明顺序随意，可以写在穿它的节点后面。

样式只是一袋值，按名字套用，它本身没有身份：它的值落到穿它的节点上，样式自己不会成为类的变量。

<a id="样式里的组件和槽位行"></a>

### 样式里的组件和槽位行

样式还可以带 `+ Component` 块和槽位行，这样一种栏目就是一个名字：

```dui
style RowColumn {
    + VerticalBox { Spacing = 15 }
    @fill
}

Widget Root {
    + HorizontalBox { Spacing = 24 }
    Widget Left : RowColumn {
        Text { Text = "Left" }
    }
    Widget Right : RowColumn {
        + VerticalBox { Spacing = 30 }
        @slot FillWeight = 2
        Text { Text = "Right" }
    }
}
```

- 样式的组件先挂。节点自己也写了同一个类的组件时，它们是**同一个**对象：先套样式的值，再套节点的。上例 `Right` 的竖排盒子最后
  `Spacing = 30`，而且没有第二个布局容器。
- 节点自己的 `@slot` 行赢过样式的。上例 `Right` 是 `Fill`（来自样式）加权重 2（来自节点）。
- 样式里没有子节点：它是一袋行，不是一棵树。

在设计器里改一个来自样式 `+` 行的值，不会被写回（[DUI7005](../diagnostics/codes.md#dui7005)）：样式是共用的，节点自己又没有
`+` 行来装这个改动。在节点上补一行（`+ VerticalBox { Spacing = 20 }`），或者去改样式。见[设计器写回](write-back.md)。

<a id="跨文件的样式"></a>

### 跨文件的样式

样式经由 `use` 跨文件共享：`use "UI/Library.dui"` 后直接写 `: Caption`，`use "UI/Library.dui" as ui` 后写
`: ui.Caption`。见 [`use`](use.md)。

VS Code 扩展可以把选中的几行属性提取成一个样式让节点穿上，也可以把一个穿着的样式内联回去（基样式在前、派生覆盖），连同样式的
`+` 组件和槽位行一起。

<a id="资源"></a>

## 资源

```dui
class /Game/UI/WBP_Palette

resources {
    Color   Accent = #FF6600
    Number  Gap    = 8
    Vector2 Icon   = (24, 24)
    String  Brand  = "Dream"
    Asset   Body   = /DreamGUI/DefaultFont_DistanceField
}

VerticalBox Root {
    Spacing = @Gap
    Text Title {
        Text  = @Brand
        Font  = @Body
        Color = @Accent
    }
    Image Logo {
        Brush.ImageSize = @Icon
    }
}
```

每个条目是一个类型、一个名字和一个值。类型有五种：`Color`、`Number`、`Vector2`、`String`、`Asset`。之后凡是能写值的地方，
`@Accent` 就代表 `#FF6600`，绑定表达式里也一样。`Asset` 条目也接受加引号的路径。

- 类型对照的是条目**自己的值**（`Color Accent = 8` 在条目那一行报错，而不是在每个用到它的地方报形状不符）：
  对不上是 [DUI4008](../diagnostics/codes.md#dui4008)，写了五种以外的类型名也是。
- 引用一个不存在的条目是 [DUI4007](../diagnostics/codes.md#dui4007)。
- 条目在多个 `resources` 块之间累积；同一个名字声明两次是 [DUI3014](../diagnostics/codes.md#dui3014)。
- 每个条目都会成为编译出来的类的一个变量，所以图表和 Class Defaults 面板能看到 `@Accent` 是什么。`use` 进来的条目也一样；
  同名的本地条目遮住导入的。

为什么资源要写类型，而别的地方不写：属性上的 `8` 从它落到的 `FProperty` 知道自己是什么，资源块里的 `8` 没有落点，
要等到有人写 `@Gap` 才知道——到那时类型错误就指向用的地方，而不是写错的地方了。

<a id="在绑定里用资源"></a>

### 在绑定里用资源

```dui
class /Game/UI/WBP_Fade

resources {
    Number Dim = 0.8
}

Widget Root {
    + Overlay { }
    Image Glow {
        RenderOpacity <- Fade() * @Dim
    }
}
```

<a id="资源作为节点类型"></a>

### 资源作为节点类型

指向一个 widget 类的 `Asset` 条目可以当节点类型用：`@Row Audio { … }`。不是 `Asset` 的条目当类型用是 DUI4008。
`use … as` 能用更少的字做同一件事，见 [`use`](use.md)。

VS Code 里可以把一个字面值提取成资源（只替换这一处，或者全部替换），未声明的资源有快速修复帮你声明。
