---
title: use
description: 合并一个库、给组件或类起短名、把库放进命名空间、库里的别名随库转发，以及旧的 @Name 写法。
---

`use` 让另一个文件或一个类在本文件里可用。一共四种写法：

```dui
use "UI/Library.dui"                     // 一个库，合并进来
use "UI/Components/Row.dui" as Row       // 一个组件，起个短名
use /Game/UI/WBP_Slider as Slider        // 一个没有 .dui 的类，起个短名
use "UI/NieR_Common.dui" as nier         // 一个库，放进命名空间

VerticalBox Root {
    Row Audio : Wide { Label = "Audio" }
    Slider Volume { }
    nier.Row Video : nier.Label { Label = "Video" }
}
```

文件路径加引号，写法和 Source File 一样（相对 `DUI/` 根、`Plugin.X:…`、绝对路径，见[概览](overview.md)）；类路径不加引号。
`use … as` 后面接的是文件还是类、文件有没有根节点，决定了它是哪一种。

<a id="合并use-"></a>

## 合并：`use "…"`

不带 `as` 地 `use` 一个文件，会把它的样式、资源和它声明的组件别名带进来。被引入文件自己的 `use` 行也会一起来，所以库可以一层叠一层。
本文件自己声明的名字遮住导入的同名者。

```dui
// DUI/UI/Library.dui —— 一个库：没有根节点
resources {
    Color Ink = #E6E9F0
}
style Wide { AnchorData.SizeDelta = (1100, 48) }
style Caption {
    FontSize = 18
    Color    = @Ink
}
```

```dui
// DUI/UI/About.dui
class /Game/UI/WBP_About
use "UI/Library.dui"

VerticalBox Root {
    Text Heading : Caption { Text = "About" }
    Text Body : Caption { Color = @Ink  Text = "Version 1.0.0" }
}
```

- 导入的**资源**也会成为本类的变量，图表和 Class Defaults 面板都看得到。导入的**样式**不会：样式只是按名字套用的一袋值。
- **时间线**不随 `use` 合并：一个时间线会编译成这个类树上的一个对象和一个以它命名的类成员变量，导入它就等于两个类拥有同一个动画。
- 同一个库经由两条路径到达（A 用了 B 和 C，B 和 C 都用了 D）只合并一次，不报错：为一个两条路都能到的库写两遍 `use` 不是错误。
- 读不出、不能解析、或者兜回一个正在导入中的文件（循环），是 [DUI2012](../diagnostics/codes.md#dui2012)。

<a id="组件use--as-row"></a>

## 组件：`use "…" as Row`

对一个**有根节点**的文件写 `use "…" as Row`，是给那个文件的类起名，别的什么也不拿：一个界面不会借用组件的样式。

```dui
use "UI/Components/Row.dui" as Row

VerticalBox Root {
    Spacing = 29
    Row Audio { Label = "Audio" }
    Row Video { Label = "Video" }
}
```

此后 `Row` 就是一个节点类型，`Row Audio { … }` 是一个实例，它的行设的是那个类的属性（见[节点与值](nodes-and-values.md)）。

类从哪里来：

- 优先用那个文件的 `class` 行。
- 没有 `class` 行时，由编辑器去找 Source File 是这个文件的 Blueprint。没有编辑器——比如在 commandlet 里——就找不到，报
  [DUI5018](../diagnostics/codes.md#dui5018)；补一行 `class` 就好。

只读那个文件的**头部**：它有没有根节点、`class` 行写的是什么。它自己的 `use` 行不往下跟。这不只是省事：一个给组件起名的库
（`use "Row.dui" as Row`），往往也正是这些组件拿样式的地方（`Row.dui` 里写着 `use "Common.dui"`），要是跟下去就会绕回库本身、
报一个并不存在的循环。组件的类就是导入方从它那里拿的全部，而它的类不依赖它的样式。

一个文件把自己当组件 `use`，是 DUI2012。组件文件仍会记成本文件的一个导入，所以它的 `class` 行变了，用它的界面会跟着重新编译。

<a id="类use-game-as-slider"></a>

## 类：`use /Game/… as Slider`

```dui
use /Game/UI/WBP_Slider as Slider

Widget Root {
    + VerticalBox { }
    Slider Volume { }
}
```

给一个背后没有 `.dui` 的类起短名。这里 `as` 是必须的：没有 `as` 的类路径是 [DUI2015](../diagnostics/codes.md#dui2015)。

<a id="命名空间use--as-nier"></a>

## 命名空间：`use "…" as nier`

对一个**没有根节点**的文件（一个库）写 `use "…" as nier`，得到的是一个命名空间：库声明的、以及库带进来的一切都以 `nier.X`
的形式进入本文件。

| 种类 | 写法 |
| --- | --- |
| 样式 | `: nier.Label` |
| 资源 | `@nier.Ink` |
| 组件别名 | `nier.Row` |

这样两个都有 `Label` 的库可以并排使用。经由命名空间进来的样式仍然找得到它自己库里的名字：库里写的 `Color = @Ink`，
指的是库自己的 `Ink`，而不是本文件的。

```dui
// DUI/UI/Settings.dui
use "UI/Library.dui" as lib
use "UI/NieR_Common.dui" as nier

VerticalBox Root {
    Text Heading : lib.Caption { Text = "Settings" }
    Text Note : nier.Label { Color = @nier.Ink  Text = "Two libraries, side by side" }
}
```

本文件没有用 `use … as ns` 声明过的 `ns.` 是 [DUI3021](../diagnostics/codes.md#dui3021)。不带 `as` 地 `use` 一个本身用了命名空间的库，
那些命名空间也会一起来：`@pal.Ink` 在这里的解析和在声明 `pal` 的那个库里一样。

<a id="转发库里的-use--as"></a>

## 转发：库里的 `use … as`

库自己的 `use … as Row` 行会随库一起走。一个给一族组件起名的库，会把这些名字交给每一个用它的界面——普通 `use` 之后是 `Row`，
`use … as nier` 之后是 `nier.Row`：

```dui
// DUI/UI/NieR_Common.dui
use "UI/Components/NieR_Row.dui" as Row
use "UI/Components/NieR_Tab.dui" as Tab

style ListRow { AnchorData.SizeDelta = (1100, 48) }
```

```dui
// DUI/UI/NieR_Menu.dui
use "UI/NieR_Common.dui"

Widget Root {
    + VerticalBox { Spacing = 8 }
    Row Item_0 : ListRow { Label = "Warped Wire" }
    Tab Weapons { Label = "Weapons" }
}
```

于是一族组件只在给它们定样式的那个库里起一次名，每个界面都不必再写资产路径。

<a id="名字的规则"></a>

## 名字的规则

`as` 后面的名字：

- 是一个词，不是关键字（[DUI2015](../diagnostics/codes.md#dui2015)，`use … as` 后面没有名字也是它）；
- 不是某个内置标签或容器的名字（[DUI3018](../diagnostics/codes.md#dui3018)）——别名永远改变不了 `Text` 或 `VerticalBox` 的含义；
- 每个文件只给一次（[DUI3017](../diagnostics/codes.md#dui3017)）。

别名找不到类——文件没有 `class` 行也没有对应的 Blueprint、或者路径加载不到东西——是 [DUI5018](../diagnostics/codes.md#dui5018)。
VS Code 里遇到一个不认识的组件类型（DUI3003），如果工作区里恰好只有一个同名的组件文件，会提供补一行 `use "…" as X` 的快速修复。

<a id="name旧写法"></a>

## `@Name`：旧写法

`use … as` 出现之前，组件的短名靠 `resources` 块里的 `Asset` 条目：

```dui
resources {
    Asset Row = /Game/UI/WBP_Row
}

VerticalBox Root {
    @Row Audio { Label = "Audio" }
    @Row Video { Label = "Video" }
}
```

条目可以是本文件的，也可以是 `use` 进来的，所以同样可以在库里起一次名。条目不存在是 [DUI4007](../diagnostics/codes.md#dui4007)，
不是 `Asset` 是 [DUI4008](../diagnostics/codes.md#dui4008)，指向的不是 user widget 是 [DUI5006](../diagnostics/codes.md#dui5006)。
这种写法仍然有效，但 `use … as` 字更少，是首选。
