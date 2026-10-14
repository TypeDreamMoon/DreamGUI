---
title: "rows 表格"
description: "同一个组件的多个实例写成一张表——类型、样式和属性名只写一次，每行一个实例，每个实例按它的第一个值命名。"
---

<a id="rows-a-table-of-instances"></a>

## `rows`：一张实例表

同一个组件写若干次，彼此只差几个值，这就是一张表：类型、样式和属性名只写一次，然后每个实例一行。

```
ListPage Page_0 : PageSize {
    Icon = @IconMap
    rows Row : ListRow (Label, Description) {
        "City Ruins",      "The overgrown remains of a city. The Resistance camp lies to the east."
        "Desert Zone",     "Sand and wind as far as the eye can see."
        "Factory",         "An abandoned factory, still running somewhere deep inside." { Kind = Count }
    }
}
```

和在表格的位置写三个无名的 `Row : ListRow { Label = "…"  Description = "…" }` 是同一棵树。
运行时什么也不跑：表格被读成那些控件，所以一行就是一个普通的控件——建树、绑定和布局都和它所代表的那行写法完全一样。

- **表头**是 `rows`，一个节点类型（标签、容器、别名、`@Name`、路径），一个可选的 `: Style`，以及
  若干列：属性名，可以是点分路径，写在括号里。每一项只写一次（DUI2020）。
- **一行**是它的各个值，按列的顺序排列，用逗号分开——属性能取的值都可以：字符串、数字、
  元组、颜色、词、`@Resource`。一行一个实例，或者在一行里写几个、用 `;` 分开。一行的值的个数必须和列数
  **完全**一样（DUI2020），读不出来的行不产生控件。值是**写**进去的，
  不是绑定：一列相当于 `=`，永远不是 `<-`。
- **行尾的块**（`{ Kind = Count }`）装这一行在列之外还需要的东西：更多
  属性、`@slot` 行、`+` 组件、子节点。块里一行如果写的正是某一列，它赢过格子里的值。
- **id 从第一个值来**，也就是这一行的键：`<id of the enclosing node>__<type>_<key>`，键里每一段 id 装不下的字符变成一个 `_`，
  并截到 32 个字符——`Page_0__Row_City_Ruins`。因此插入一行或
  移动一行时，**别的行**的 id，以及从 id 生成的本地化键（`Page_0__Row_City_Ruins.Label`），
  都留在原地。两行的键生成了同一个 id 时会提示（DUI3023，警告）：第二行得到 `_1`，
  而这个后缀会随行序变化。键里没有一个字符是 id 能装的，这一行就像普通无名节点一样按计数编号。
- `rows` 只有后面跟着一个类型和一个列名列表时才是关键字：叫 `rows` 的属性、或者类型叫它的节点，照旧按原来的意思读。

代码要按名字拿某一行，就把那一行写成一个单独的节点。
