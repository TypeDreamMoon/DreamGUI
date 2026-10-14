---
title: "if 与 for"
description: "if / else if / else 切换显示哪一支，for 和 each 按类的数据重复一个模板，以及两者都要守的规则。"
---

<a id="if-and-else"></a>

## `if` 和 `else`

```
if HasSave() {
    Row Continue { Label = "Continue" }
} else if IsLoading() {
    Native.Throbber Spinner { }
} else {
    Text NoSave { Text = "No save data" }
}
```

条件是一个绑定表达式。每一支装的是 widget。每一支里的每个 widget 都按顺序成为外层节点的子节点，并被绑上一个 `Shown`，条件是“走的正是这一支”：第一支是 `HasSave()`，第二支是 `!HasSave() && IsLoading()`，最后一支是 `!HasSave() && !IsLoading()`。切换就是改可见性：隐藏的那一支是折叠（Collapsed），不是销毁，它的状态都还在。

- 一支里只能有 widget：属性、槽位行或 `+` 块写在里面都是 DUI2018，槽位和循环也是。
- 分支里的 widget 自己也写了 `Shown` 时，要它所在的那支被选中、并且它自己的条件成立，它才显示。
- `if` 只能写在节点里面；写在文件顶层，或者前面没有 `if` 的 `else`，都是 DUI2018。
- `else` 可以写在 `}` 闭合的那一行，也可以写在下一行。

`if` 和 `else` 只在开启一个分支的位置才是关键字：一个叫 `if` 的属性仍然是属性。


<a id="for-and-each"></a>

## `for` 和 `each`

两者都是按一个源的每一项重复一个模板 widget。源是一个函数 `GetOptions()`，或者一个变量 `Options`——FieldNotify 的数组变了，副本会跟着刷新——也可以经过一条成员路径，`Inventory.Items`、`Inventory.Filtered()`，路径上任何一处变了都会刷新。循环体里，`Prop <- Item.Member` 把每个副本的一个属性绑到它那一项的一个成员上，`Event -> Item.Func()` 把每个副本的一个事件路由到它那一项的一个函数上：

```
for Item in Inventory.Items {
    HorizontalBox {
        Text { Text <- Item.Name }
        Native.Button { OnClicked += Item.Use() }
    }
}
```

一项宣布它的某个被副本显示的成员变了时（这一项实现了 `INotifyFieldValueChanged`，成员是 FieldNotify 的），只更新那一个副本，列表的其余部分不动。没人宣布的成员，在列表刷新时重新读。

<a id="for-copies-in-the-panel"></a>

### `for`：面板里的副本

```
VerticalBox Options {
    Text Header { Text = "Options" }
    for Option in GetOptions() {
        Row { Label <- Option.Label  Kind <- Option.Kind }
    }
    Text Footer { Text = "…" }
}
```

运行时模板留在树里，处于折叠状态；每一项一个副本，按项的顺序占在宿主的子节点里——也就是 `for` 两边的兄弟节点之间。不虚拟化，也不额外造任何东西：宿主自己的容器像排普通子节点一样排这些副本。适合短列表：设置页的选项、一排标签页。

<a id="each-a-list-views-cells"></a>

### `each`：列表视图的单元格

```
Widget Inventory {
    + UIListView { }
    each Item in GetItems() {
        Row Cell { Label <- Item.Name }
    }
}
```

`each` 填的是它所在那个 widget 的列表视图：单元格随着列表滚动被创建和回收。适合长列表。

<a id="the-rules-both-keep"></a>

### 两者都要守的规则

- 循环体恰好是一个 widget，即模板（DUI5021、DUI5012）。要重复几个 widget，就把它们包进一个容器。
- 循环不能是根，也不能嵌套在另一个循环里（DUI5021、DUI5012）。要嵌套，就写一个组件，内层循环写在它自己的文件里，列表通过一个 `props` 条目交给它。
- `for` 要求宿主能接任意多个子节点；`each` 要求宿主上有 `+ UIListView`（或者一个可回收的滚动视图）。
- 循环体里唯一能写的绑定是单跳的 `Item.Member`，唯一能写的对象路由是 `Item.Func` /
  `Item.Func()`；写表达式、`<->`，或者路由到别的对象，是 DUI5014。
- 源必须在类上存在（DUI6006），而且是一个对象数组（DUI6007）。
- 数组的元素类已知时（`TArray<UItemVM*>`，而不是 `TArray<UObject*>`），`Item.Member` 必须是它的成员
  （DUI6021），`Item.Func` 必须是它的函数，并且什么都不收或者收的恰好是事件发出的那些（DUI6022）。元素是 `UObject` 的数组时，项的类要到运行时才知道，对不上的那一行在那时被跳过。
