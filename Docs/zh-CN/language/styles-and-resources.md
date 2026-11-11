---
title: "样式与资源"
description: "样式是一组具名的行，可以继承、可以带组件和槽位行；资源是带类型的具名常量。"
---

<a id="styles"></a>

## 样式

样式是一组具名的行，用 `: Name` 套到节点上：

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

- 样式可以继承另一个样式（`style Warning : Caption`）：先套基样式的行，再套派生样式的，最后是节点自己的。兜回自己的基样式是 DUI3015。样式可以命名多个基样式，节点也可以；参见[多个样式](#several-styles)。
- 样式可以带 `+ Component` 块和槽位行，这样一种栏目就是一个名字：

  ```
  style RowColumn {
      + VerticalBox { Spacing = 15 }
      @fill
  }

  Widget Left : RowColumn { … }
  ```

  样式的组件先挂。节点自己也写了同一个类的组件时，它们是同一个对象：先套样式的值，再套节点的。节点自己的 `@slot` 行赢过样式的。
- 本地样式会遮住导入的同名样式。两个同名的本地样式是 DUI3005。
- **控件的 `Style` 不属于这些。** `Native.Button` 及其同类拥有一个名为 `Style` 的属性，它是控件自身的一个结构体，写作 `Style.TickChecked = #FF3355`，并由 `StyleSource` 选择。这样的行是否生效，以及字段旁的 `bOverride_` 位有什么作用，见[样式和样式表](../controls/styles.md#in-dui-and-at-run-time)。


<a id="several-styles"></a>

### 多个样式

样式、节点和 `rows` 头各自接受任意数量的样式，每一个都跟在各自的冒号之后：

```
style Danger : Button : Loud { Color = #FF4040 }
Text Title : Heading : Glow { }
rows Row : ListRow : Dense (Label, Value) { … }
```

- **顺序。**名称从左到右依次应用，每个名称先递归应用它自己的基样式，因此基样式先于继承它的样式。一个样式只在其首次出现的位置应用一次。靠后的样式覆盖靠前的样式，节点自身的行最后应用。对于 `style A : Base` 和 `style B : Base`，写成 `: A : B` 的节点会得到 Base、A、B。
- 未知的名称是 DUI3004，每个未知的名称报一次。通过其基样式绕回自身的样式是 DUI3015。
- **命名一个已经应用的样式**不会改变任何内容，并会触发 DUI3025，即一个警告。它会在靠后的名称上触发，无论较早的名称是把它作为基样式应用（`: A : Base`，其中 `A` 继承 `Base`），还是这个名字只是被写了两次（`: A : A`）。每个子句只检查一次——节点的或 `rows` 头的子句，以及样式声明的子句，无论有多少节点使用它——其中每个多余的名称都会被报告。从其他文件导入的样式在这里永远不会被警告；它来源的文件在编译时会收到警告。消息会说明属于哪种情况：`style 'X' is already applied through 'Y', so naming it again changes nothing`，以及 `style 'A' is named twice in this clause, so the second changes nothing`。

<a id="resources"></a>

## 资源

```
resources {
    Color   Accent = #FF6600
    Number  Gap    = 8
    Vector2 Icon   = (24, 24)
    String  Brand  = "Dream"
    Asset   Row    = /Game/UI/WBP_Row
}
```

之后凡是能写值的地方，`@Accent` 就代表 `#FF6600`，绑定表达式里也一样。类型对照的是条目自己的值（DUI4008）；引用一个不存在的条目是 DUI4007。每个条目还会成为编译出来的类的一个变量，所以图表可以读取它。

指向一个 widget 类的 `Asset` 条目可以当节点类型用：`@Row Audio { … }`。`use … as` 用更少的字做同一件事。

条目在多个 `resources` 块之间累积；同一个名字声明两次是 DUI3014。
