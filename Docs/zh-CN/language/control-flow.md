---
title: if 与 for
description: if / else if / else 按条件切换显示哪一支，for 和 each 按类的数据重复一个模板，以及两者都要守的规则。
---

`.dui` 里有两种控制语句，都写在节点块里：`if` 按条件决定显示哪一支的 widget，`for` / `each` 按类的数据把一个模板重复多次。
两者都是**运行时**的：文件里写的每个 widget 都会进入类，切换和重复发生在类运行的时候。

<a id="if-和-else"></a>

## `if` 和 `else`

```dui
class /Game/UI/WBP_Title
use "UI/Components/Row.dui" as Row

VerticalBox Root {
    Spacing = 12
    if HasSave() {
        Row Continue { Label = "Continue" }
    } else if IsLoading() {
        Native.Throbber Spinner { }
    } else {
        Text NoSave { Text = "No save data" }
    }
    Row NewGame { Label = "New game" }
}
```

条件是一个绑定表达式（写法见[绑定与路由](bindings.md)）。每一支装的是 widget。每一支里的每个 widget 都按顺序成为外层节点的
子节点，并被绑上一个 `Shown`，条件是“走的正是这一支”：

| widget | 绑上的 `Shown` |
| --- | --- |
| `Continue` | `HasSave()` |
| `Spinner` | `!HasSave() && IsLoading()` |
| `NoSave` | `!HasSave() && !IsLoading()` |

所以切换就是改可见性：没走的那支是折叠（Collapsed），不是销毁，它的状态都还在。这也意味着 `if` 不是编译期条件：每一支都会建出来，
文件里没有任何写法能把一个 widget 排除在类之外。一个根本不该存在的变体，应该是另一个类。

- 一支里只能有 widget：属性、槽位行、`+` 块写在里面都是 [DUI2018](../diagnostics/codes.md#dui2018)，槽位声明和循环也是。
- 分支里的 widget 自己也写了 `Shown` 时，要它所在的那支被选中、**并且**它自己的条件成立，它才显示。
- `if` 只能写在节点里面。写在文件顶层，或者前面没有 `if` 的 `else`，都是 DUI2018；缺条件或缺块也是。
- `else` 可以写在 `}` 所在的那一行，也可以写在下一行。
- `if` 和 `else` 只在开启一个分支的位置才是关键字：一个叫 `if` 的属性仍然是属性。

下面这样写会被拒绝，因为分支里是一个属性而不是 widget：

```text
Widget Root {
    if HasSave() {
        Text = "Continue"
    }
}
```

在设计器里改一个由 `if` 决定可见性的 widget 的可见性，写回会拒绝（[DUI7004](../diagnostics/codes.md#dui7004)）：
没有哪一行写着它。改条件，或者把 widget 移出分支。见[设计器写回](write-back.md)。

<a id="for-和-each"></a>

## `for` 和 `each`

两者都是按一个**源**的每一项重复一个模板 widget。源是一个函数 `GetOptions()`，或者一个变量 `Options`——FieldNotify 的数组变了，
副本会跟着刷新——也可以经过一条成员路径，`Inventory.Items`、`Inventory.Filtered()`，路径上任何一处变了都会刷新。循环体里，
`Prop <- Item.Member` 把每个副本的一个属性绑到它那一项的一个成员上，`Event -> Item.Func()` 把每个副本的一个事件路由到它那一项的一个函数上：

```dui
class /Game/UI/WBP_Inventory

viewmodels {
    InventoryVM Inventory = global
}

VerticalBox Root {
    for Item in Inventory.Items {
        HorizontalBox {
            Text { Text <- Item.Name }
            Native.Button { OnClicked += Item.Use() }
        }
    }
}
```

`Item.Func()` 什么都不传；`Item.Func` 不带括号时把事件发出的东西交给它。在 `each` 里，列表视图回收一个单元格时，它的路由跟着挪到新的那一项上。

一项宣布它的某个被副本显示的成员变了时（这一项实现了 `INotifyFieldValueChanged`，成员是 FieldNotify 的），只更新那一个副本，列表的其余部分不动。
没人宣布的成员，在列表刷新时重新读。

<a id="for面板里的副本"></a>

### `for`：面板里的副本

```dui
class /Game/UI/WBP_Options
use "UI/Components/Row.dui" as Row

VerticalBox Options {
    Text Header { Text = "Options" }
    for Option in GetOptions() {
        Row { Label <- Option.Label  Kind <- Option.Kind }
    }
    Text Footer { Text = "Press A to change" }
}
```

运行时模板留在树里，处于折叠状态；每一项一个副本，按项的顺序占在宿主的子节点里——也就是 `for` 两边的兄弟节点之间。不虚拟化，
也不额外造任何东西：宿主自己的容器像排普通子节点一样排这些副本。适合短列表：设置页的选项、一排标签页。

<a id="each列表视图的单元格"></a>

### `each`：列表视图的单元格

```dui
class /Game/UI/WBP_Inventory
use "UI/Components/Row.dui" as Row

Widget Inventory {
    + UIListView { }
    each Item in GetItems() {
        Row Cell { Label <- Item.Name }
    }
}
```

`each` 填的是它所在那个 widget 的列表视图：单元格随着列表滚动被创建和回收。适合长列表。

<a id="两者都要守的规则"></a>

### 两者都要守的规则

- 循环体**恰好是一个** widget，即模板（[DUI5021](../diagnostics/codes.md#dui5021)、[DUI5012](../diagnostics/codes.md#dui5012)）。
  要重复几个 widget，就把它们包进一个容器。
- 循环不能是根，也不能嵌套在另一个循环里（DUI5021、DUI5012）。要嵌套，就写一个组件，内层循环写在它自己的文件里，列表通过一个
  `props` 条目交给它。
- `for` 要求宿主能接任意多个子节点（固定格位的宿主不行，DUI5021）；`each` 要求宿主上有 `+ UIListView`（或者一个可回收的滚动视图），
  否则是 DUI5012。
- 循环体里唯一能写的绑定是单跳的 `Item.Member`，唯一能写的对象路由是 `Item.Func` / `Item.Func()`；写表达式、`<->`、路由到别的对象，
  是 [DUI5014](../diagnostics/codes.md#dui5014)。循环体里也不能 `emit`（[DUI6012](../diagnostics/codes.md#dui6012)）。
- 源必须在类上存在（[DUI6006](../diagnostics/codes.md#dui6006)），而且是一个对象数组（[DUI6007](../diagnostics/codes.md#dui6007)）。
- 数组的元素类已知时（`TArray<UItemVM*>`，而不是 `TArray<UObject*>`），`Item.Member` 必须是它的成员
  （[DUI6021](../diagnostics/codes.md#dui6021)），`Item.Func` 必须是它的函数，并且什么都不收或者收的恰好是事件发出的那些
  （[DUI6022](../diagnostics/codes.md#dui6022)）。元素是 `UObject` 的数组时，项的类要到运行时才知道，对不上的那一行在那时被跳过。

其余相关的代码：

| 代码 | 情况 |
| --- | --- |
| [DUI2010](../diagnostics/codes.md#dui2010) | 循环头不是 `<关键字> Var in Source` 或 `Source()` 的形状 |
| [DUI3008](../diagnostics/codes.md#dui3008) | 循环变量遮住了外层循环的变量（警告） |
| [DUI5007](../diagnostics/codes.md#dui5007) | 建树的调用方没有地方记录循环，循环被跳过（警告） |

`for` 运行时造出来的副本不是文件里的任何一行，设计器写回会跳过它们；循环的模板也不会被设计器删掉或移出循环——没有东西可重复的循环建不起来。
VS Code 在 `each` 循环体里输入 `Item.` 时，会按源的元素类型补全成员。
