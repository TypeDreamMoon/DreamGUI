---
title: rows 表格
description: 同一个组件的多个实例写成一张表——类型、样式和属性名只写一次，每行一个实例，按第一个值命名。
---

设置页、菜单的条目列表，往往是同一个组件写 N 次，彼此只差几个值。`rows` 把它写成一张表：类型、样式和属性名只写一次，
然后每个实例一行。

```dui
class /Game/UI/WBP_Map
use "UI/NieR_Common.dui"

Widget Root {
    ListPage Page_0 : PageSize {
        Icon = @IconMap
        rows Row : ListRow (Label, Description) {
            "City Ruins",      "The overgrown remains of a city. The Resistance camp lies to the east."
            "Desert Zone",     "Sand and wind as far as the eye can see."
            "Factory",         "An abandoned factory, still running somewhere deep inside." { Kind = Count }
        }
    }
}
```

除了 id 的来历（见下文），它和在表格的位置写三个无名的 `Row : ListRow { … }` 是同一棵树：

```dui
class /Game/UI/WBP_Map
use "UI/NieR_Common.dui"

Widget Root {
    ListPage Page_0 : PageSize {
        Icon = @IconMap
        Row : ListRow { Label = "City Ruins"   Description = "The overgrown remains of a city. The Resistance camp lies to the east." }
        Row : ListRow { Label = "Desert Zone"  Description = "Sand and wind as far as the eye can see." }
        Row : ListRow {
            Label = "Factory"
            Description = "An abandoned factory, still running somewhere deep inside."
            Kind = Count
        }
    }
}
```

运行时什么也不多跑：表格在解析时就展开成这些 widget，所以一行就是一个普通的 widget，建树、绑定、布局都和它所代表的那行写法完全一样。

（自 1.0.0 起）

<a id="表头"></a>

## 表头

表头是 `rows`、一个节点类型（标签、容器、别名、`@Name`、路径都行）、可选的 `: Style`，然后是括号里的**列**：属性名，可以是点分路径。
每一列只能写一次（[DUI2020](../diagnostics/codes.md#dui2020)；比较不区分大小写，两个只差大小写的列名指的是同一个属性）。

<a id="行"></a>

## 行

一行是它的各个值，按列的顺序、用逗号分开。属性能取的值都可以：字符串、数字、元组、颜色、词、`@Resource`。一行一个实例，或者在一行里写几个、
用 `;` 分开：

```dui
style Swatch { AnchorData.SizeDelta = (48, 48) }

HorizontalBox Palette {
    Spacing = 8
    rows Image : Swatch (Brush.TintColor, AnchorData.SizeDelta) {
        #E6E9F0, (48, 48); #8C93A6, (48, 48)
        #1B1E26, (64, 48)
    }
}
```

- 一行的值的个数必须和列数**完全**一样（DUI2020）。读不出来的行不产生 widget：少一个值，就只能按样式或类默认值去猜；多一个值，
  就是丢了作者写的东西——两种都是猜。
- 两个值之间漏了逗号（`"A" "B"`）报的是语法错误（[DUI2001](../diagnostics/codes.md#dui2001)），而不是值的个数不对。
- 值是**写**进去的，不是绑定：一列相当于 `=`，永远不是 `<-`。

<a id="行尾的块"></a>

## 行尾的块

一行可以以一个块结尾（上例的 `{ Kind = Count }`），装这一行在列之外还需要的东西：更多属性、`@slot` 行、`+` 组件、子节点。
块里一行如果写的正是某一列，它赢过格子里的值。

<a id="id-从第一个值来"></a>

## id 从第一个值来

每一行的 id 由它的第一个值——这一行的**键**——生成：`<外层节点 id>__<类型>_<键>`。键里每一段 id 装不下的字符变成一个 `_`，
两头不留 `_`，最长 32 个字符。所以第一个例子里的三行是 `Page_0__Row_City_Ruins`、`Page_0__Row_Desert_Zone`、`Page_0__Row_Factory`。

这样插入一行或调换行序，**别的行**的 id 不会动，从 id 生成的本地化键（`Page_0__Row_City_Ruins.Label`）也不会动。

- 两行的键生成了同一个 id 时会提示（[DUI3023](../diagnostics/codes.md#dui3023)，警告）：第二行得到 `_1` 后缀，而这个后缀会随行序变化。
  给各行不同的第一个值。
- 键里没有一个字符是 id 能装的（比如 `"--"`，或者一个元组），这一行就像普通无名节点一样按计数编号。
- 类型里 id 装不下的字符同样换成 `_`：`nier.Row` 写作 `nier_Row`。

```text
rows Row : ListRow (Label) {
    "City Ruins"
    "City-Ruins"
}
```

这张表写在 `Page_0` 里时，第二行也得到 `Page_0__Row_City_Ruins`，于是是 DUI3023，它变成 `Page_0__Row_City_Ruins_1`。

代码要按名字拿某一行，就把那一行写成一个单独的节点。

<a id="rows-只在一处是关键字"></a>

## `rows` 只在一处是关键字

`rows` 只有后面跟着一个类型和一个列表时才是关键字：叫 `rows` 的属性、或者类型叫 `rows` 的节点，照旧按原来的意思读。

<a id="设计器和编辑器"></a>

## 设计器和编辑器

设计器把一列的值写回它的格子里；行尾块里的行也能写回。行自己的那一行只写得下它的列，所以别的改动——给行改名、加 `+` 块、
删除或移动行、在表格旁边放节点——都会被拒绝并说明原因（[DUI7004](../diagnostics/codes.md#dui7004)），见[设计器写回](write-back.md)。

VS Code 扩展按编译器的方式读表格：每行一个以第一个值命名的无名 widget，读不通的表格报 DUI2020，键冲突报 DUI3023。格式化时每行保持在自己的那一行，
保留作者对齐的列。
