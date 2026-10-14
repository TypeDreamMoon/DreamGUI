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

- 样式可以继承另一个样式（`style Warning : Caption`）：先套基样式的行，再套派生样式的，最后是节点自己的。
  兜回自己的基样式是 DUI3015。
- 样式可以带 `+ Component` 块和槽位行，这样一种栏目就是一个名字：

  ```
  style RowColumn {
      + VerticalBox { Spacing = 15 }
      @fill
  }

  Widget Left : RowColumn { … }
  ```

  样式的组件先挂。节点自己也写了同一个类的组件时，它们是同一个对象：先套样式的值，再套节点的。节点自己的 `@slot` 行
  赢过样式的。
- 本地样式会遮住导入的同名样式。两个同名的本地样式是 DUI3005。


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

之后凡是能写值的地方，`@Accent` 就代表 `#FF6600`，绑定表达式里也一样。类型对照的是条目自己的值
（DUI4008）；引用一个不存在的条目是 DUI4007。每个条目还会成为编译出来的类的一个变量，所以图表可以读取它。

指向一个 widget 类的 `Asset` 条目可以当节点类型用：`@Row Audio { … }`。`use … as` 用更少的字做同一件事。

条目在多个 `resources` 块之间累积；同一个名字声明两次是 DUI3014。
