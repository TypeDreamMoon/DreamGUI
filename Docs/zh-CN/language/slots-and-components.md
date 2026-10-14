---
title: "槽位行与组件"
description: "用 @slot 和 @fill 告诉父面板怎样摆这个节点，用 + Class 给节点挂行为、布局容器和自身布局对象。"
---

<a id="slot-lines-slot-and-fill"></a>

## 槽位行：`@slot` 和 `@fill`

一个 widget 在面板里——父节点的容器负责排布子节点（盒子、网格、叠放）——时，它有一个**面板
槽位**：父节点怎么放它。`@slot` 行设的就是它：

```
Text Label {
    @slot HorizontalAlignment = Fill
    @slot Padding = (0, 4, 0, 4)
}
```

几条槽位行可以合成一个块，块里的写法和 `+` 块一样：

```
@slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }
```

盒子布局大部分就是这两行，所以它们有简写：

| 简写 | 等于 |
|---|---|
| `@fill` | `@slot SizeRule = Fill` |
| `@fill 2` | `@slot SizeRule = Fill` 加 `@slot FillWeight = 2` |

`@fill` 独占一行，或者后面只跟它的权重。父节点不排布面板的节点写了槽位行，是 DUI5003。


<a id="components--class"></a>

## 组件：`+ Class`

`+ Class { … }` 给节点挂一个行为、一个布局容器或一个自身布局对象，块里设那个对象的属性：

```
Widget Confirm {
    + UIButton { TransitionType = None }
    + VerticalBox { Spacing = 10  Padding = (24, 28, 24, 28) }
}
```

类按名字查找，会依次试插件的各种前缀：`VerticalBox` 找到竖排盒子容器，
`UIButton` 找到按钮行为，写 `Button` 也行。完整的 `/Script/Module.Class` 路径同样可以。解析不到任何 widget 能挂的东西，是 DUI3006。

一个节点只有一个布局容器。在普通 `Widget` 上写 `+ VerticalBox` 和把类型写成容器类型是同一件事的两种写法，后者更短。
