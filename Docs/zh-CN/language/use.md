---
title: "use"
description: "合并一个库、给组件或类起短名、把库放进命名空间、库里的别名随库转发，以及旧的 @Name 写法。"
---

<a id="use"></a>

## `use`

`use` 让另一个文件或一个类在本文件里可用。一共四种写法：

```
use "UI/Library.dui"                     // a library, merged
use "UI/Components/Row.dui" as Row       // a component, by a short name
use /Game/UI/WBP_Slider as Slider        // a class with no .dui, by a short name
use "UI/NieR_Common.dui" as nier         // a library, under a namespace
```

**合并。** 不带 `as` 地 `use` 一个文件，会把它的样式、资源和它声明的组件别名带进来。被引入文件自己的 `use` 行也会一起来，所以库可以一层叠一层。本文件自己声明的名字会遮住导入的同名者。

**一个组件。** 对**有根节点**的文件写 `use "…" as Row`，是给那个文件的类起名，别的什么也不拿：一个界面不会借用组件的样式。类就是该文件 `class` 行所指定的那个；对于没有 `class` 行的文件，编辑器会去找 Source File 是这个文件的 Blueprint（没有编辑器——比如在 commandlet 里——就是 DUI5018，补一行 `class` 即可解决）。此后 `Row` 就是一个节点类型：

```
Row Audio { Label = "Audio" }
```

**一个类。** `use /Game/UI/WBP_Slider as Slider` 给一个背后没有 `.dui` 的类起名。这里 `as` 是必须的。

**一个命名空间。** 对一个没有根节点的文件写 `use "…" as nier`，得到的是一个命名空间：库声明的、以及库带进来的一切都以 `nier.X` 的形式进入本文件。它的样式是 `: nier.Label`，它的资源是 `@nier.Ink`，它的组件别名是 `nier.Row`。这样两个都写了 `Label` 的库就可以并排使用。经由命名空间进来的样式仍然找得到它自己库里的名字：库里写的 `Color = @Ink` 指的是库自己的 `Ink`。

**转发。** 库自己的 `use … as Row` 行会随库一起走。一个给一族组件起名的库，会把这些名字交给每一个用它的界面——普通 `use` 之后是 `Row`，`use … as nier` 之后是 `nier.Row`：

```
// UI/NieR_Common.dui
use "UI/Components/NieR_Row.dui" as Row
use "UI/Components/NieR_Tab.dui" as Tab

// UI/NieR_Menu.dui
use "UI/NieR_Common.dui"
Widget Root {
    Row Item_0 : ListRow { Label = "Warped Wire" }
}
```

`as` 后面的名字是一个词，不是关键字（DUI2015），不是某个标签或容器的名字（DUI3018），并且每个文件只给一次（DUI3017）。一个没有 `use … as ns` 声明的 `ns.` 是 DUI3021。读不出、不能解析、或者兜回一个正在导入中的文件的导入，是 DUI2012。
