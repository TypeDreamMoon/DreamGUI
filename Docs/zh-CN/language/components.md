---
title: "编写组件"
description: "用 .dui 写一个在别的树里使用的 widget Blueprint——props、events 与 emit、默认槽位和具名槽位，以及宿主怎样设置、监听和填充它。"
---

<a id="writing-a-component"></a>

## 编写组件

组件就是一个在别的树里使用的 widget Blueprint。用 `.dui` 写的组件，除了它自己的树，还要声明**宿主**
能设什么、能听到什么。

<a id="props"></a>

### `props`

```
props {
    Text   Label
    Text   Value
    Number ValueIndex = 0
    Enum   /Script/MyGame.ERowKind Kind = Cycle
}
```

每行一个属性：一个类型、一个名字，可选的 `= default`。类型有 `Text`、`String`、`Number`、`Integer`、
`Bool`、`Color`、`Vector2`、`Asset`、`Class`，以及后面跟着枚举路径的 `Enum`。每一个都会成为这个类的一个 Blueprint
变量，可以在实例上编辑；如果 C++ 父类已经有同名同类型的属性，就直接用它，不再声明一个。

宿主像设置别的属性一样设置它们，组件用绑定读它们：

```
// in the component
Text LabelText { Text <- Label }

// in a host
Row Audio { Label = "Audio" }
Row Track { Label <- GetTrackName() }
```

宿主也可以**绑定** prop，不只可以设置它。prop 没有 setter——它是一个 Blueprint 变量——所以这条绑定在值变化时
把值直接写进实例并发出通知（props 是 FieldNotify 的），组件自己在这个 prop 上的绑定就会显示出变化。这是绑定
唯一接受的没有 setter 的属性：user widget 自己的变量，而且只能单向（`<-`，不能 `<->`）。

一行不是 `Type Name` 或 `Type Name = value` 就是 DUI2016；一个名字写两次是 DUI3019；编译时，类型没有
Blueprint pin 是 DUI6008，名字已被类使用是 DUI6009，默认值装不进它的类型是 DUI6013。

<a id="events-and-emit"></a>

### `events` 和 `emit`

```
events {
    Picked(Number Index)
    Closed
}
```

条目在换行或 `;` 处结束，所以短的列表一行就能写完：`events { Picked(Number Index); Closed }`。每个条目
都会成为这个类的一个事件分发器。组件在任何一条路由上用 `emit` 发出事件，参数写成
绑定表达式：

```
+ UIButton { OnClick -> emit Picked(ValueIndex) }
```

宿主像路由别的事件一样路由它：

```
Row Audio { Picked -> HandleAudioPicked }
```

DUI2017 是格式错误的条目，DUI3020 是重复。编译时，`emit` 一个本文件没有声明的事件是 DUI6010，
参数和事件的参数对不上是 DUI6011，`emit` 写在了编译不了的地方（循环体里，
或者事件的参数 Blueprint 接不住）是 DUI6012，事件名和类的另一个成员重名是
DUI6014。

<a id="slots"></a>

### 槽位

组件用 `slot` 开一个洞，让宿主往里放东西：

```
slot Footer
slot Rows default
slot Detail : DetailPanel {
    + VerticalBox { Spacing = 15 }
    @slot SizeRule = Fill
}
```

- `default` 标出宿主没指名时内容进的那个槽位。每个文件只能有一个（DUI3022）。
- 块或样式负责洞的布局：组件、属性和槽位行——**不能**有子节点，因为里面放什么是宿主的（DUI2019）。
  带布局容器的槽位能放多个 widget；不带的只放一个。
- `(was: OldName)` 给槽位改名，和节点一样。

宿主**嵌套**就是填**默认**槽位；在实例里写一个自己的 `slot` 块，就是填**具名**槽位：

```
ListPage Page_2 {
    Row Item_0 { Label = "Warped Wire" }        // into the default slot
    slot Detail {                               // into the slot named Detail
        Text Note { Text = "A length of wire bent out of shape." }
    }
}
```

填进去的只能是 widget。具名槽位放一个 widget——要放几个，先套一个容器。在组件实例之外填槽是 DUI5019；
填的槽位组件没有声明是 DUI5020。
