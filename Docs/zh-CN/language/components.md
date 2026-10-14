---
title: 编写组件
description: 用 .dui 写一个在别的树里使用的 widget Blueprint——props、events 与 emit、默认槽位和具名槽位，以及宿主怎样设置、监听和填充它。
---

组件就是一个在别的树里使用的 widget Blueprint。用 `.dui` 写的组件，除了它自己的树，还要声明**宿主**（放置它的那个文件）
能设什么、能听到什么、能往哪里放东西：

| 声明 | 给宿主的 | 编译成 |
| --- | --- | --- |
| `props { … }` | 可以设置或绑定的属性 | Blueprint 变量，可在实例上编辑 |
| `events { … }` | 可以路由的事件 | 事件分发器 |
| `slot Name` | 可以往里放 widget 的洞 | 具名槽位 |

一个完整的组件和它的宿主：

```dui
// DUI/UI/Components/Row.dui
class /Game/UI/Components/WBP_Row

props {
    Text   Label
    Text   Value
    Number ValueIndex = 0
}
events {
    Picked(Number Index)
}

Widget Root {
    + HorizontalBox { Spacing = 12 }
    + UIButton { OnClick -> emit Picked(ValueIndex) }

    Text LabelText {
        Text <- Label
        @fill
    }
    Text ValueText { Text <- Value }
}
```

```dui
// DUI/UI/Settings.dui
class /Game/UI/WBP_Settings
use "UI/Components/Row.dui" as Row

VerticalBox Root {
    Spacing = 8
    Row Audio {
        Label = "Audio"
        Value = "On"
        Picked -> HandleAudioPicked
    }
    Row Track { Label <- GetTrackName() }
}
```

<a id="props"></a>

## `props`

```dui
class /Game/UI/Components/WBP_Cycle

props {
    Text   Label
    Text   Value
    Number ValueIndex = 0
    Enum   /Script/MyGame.ERowKind Kind = Cycle
}

Widget Root {
    + HorizontalBox { }
    Text LabelText { Text <- Label }
}
```

每行一个属性：一个类型、一个名字，可选的 `= 默认值`。类型有 `Text`、`String`、`Number`、`Integer`、`Bool`、`Color`、
`Vector2`、`Asset`、`Class`，以及后面跟着枚举路径的 `Enum`。每一个都会成为这个类的一个 Blueprint 变量，可以在实例上编辑，
也是 Expose on Spawn 的——在蓝图里用 **Create Dream Widget** 创建这个类时，每个 prop 都是节点上的一个引脚，在 widget 初始化之前赋值；
如果 C++ 父类已经有同名同类型的属性，就直接用它，不再声明一个。

宿主像设置别的属性一样设置它们，组件用绑定读它们（组件里的 `Text <- Label`）。宿主也可以**绑定** prop：

```dui
use "UI/Components/Row.dui" as Row

VerticalBox Root {
    Row Audio { Label = "Audio" }
    Row Track { Label <- GetTrackName() }
}
```

prop 没有 setter——它是一个 Blueprint 变量——所以这条绑定在值变化时把值直接写进实例并发出通知（props 是 FieldNotify 的），
组件自己在这个 prop 上的绑定就会显示出变化。这是绑定唯一接受的没有 setter 的属性：user widget 自己的变量，而且只能单向
（`<-`，不能 `<->`）。

| 代码 | 情况 |
| --- | --- |
| [DUI2016](../diagnostics/codes.md#dui2016) | 一行不是 `Type Name` 或 `Type Name = value`，或者 `props` 写在节点里面 |
| [DUI3019](../diagnostics/codes.md#dui3019) | 两行声明了同一个名字 |
| [DUI6008](../diagnostics/codes.md#dui6008) | 编译时：类型没有对应的 Blueprint pin |
| [DUI6009](../diagnostics/codes.md#dui6009) | 编译时：名字已被类的另一个成员以别的类型占用，或者是某个 widget 的 id |
| [DUI6013](../diagnostics/codes.md#dui6013) | 编译时：默认值装不进它的类型 |

<a id="events-和-emit"></a>

## `events` 和 `emit`

```dui
class /Game/UI/Components/WBP_Picker

props {
    Number ValueIndex = 0
}
events {
    Picked(Number Index)
    Closed
}

Widget Root {
    + VerticalBox { }
    Widget Choose {
        + UIButton { OnClick -> emit Picked(ValueIndex) }
    }
    Native.Button Close { OnClicked -> emit Closed }
}
```

条目在换行或 `;` 处结束，所以短的列表一行就能写完：`events { Picked(Number Index); Closed }`。参数的类型和 props 的一样。
每个条目都会成为这个类的一个事件分发器。

组件在任何一条路由上用 `emit` 发出事件，参数写成绑定表达式（见[绑定与路由](bindings.md)）。宿主像路由别的事件一样路由它：

```dui
use "UI/Components/Picker.dui" as Picker

Widget Root {
    + VerticalBox { }
    Picker Audio {
        Picked -> HandleAudioPicked
        Closed -> HandleClosed
    }
}
```

| 代码 | 情况 |
| --- | --- |
| [DUI2017](../diagnostics/codes.md#dui2017) | 条目不是 `Name` 或 `Name(Type Param, …)`，或者 `events` 写在节点里面 |
| [DUI3020](../diagnostics/codes.md#dui3020) | 两个同名条目，或者一个参数写了两次 |
| [DUI6010](../diagnostics/codes.md#dui6010) | `emit` 了一个本文件没有声明的事件 |
| [DUI6011](../diagnostics/codes.md#dui6011) | `emit` 的参数和事件的参数对不上 |
| [DUI6012](../diagnostics/codes.md#dui6012) | `emit` 写在了编译不了的地方：循环体里，或者事件的参数 Blueprint 接不住 |
| [DUI6014](../diagnostics/codes.md#dui6014) | 事件名和类的另一个成员重名 |

<a id="槽位"></a>

## 槽位

组件用 `slot` 开一个洞，让宿主往里放东西：

```dui
// DUI/UI/Components/ListPage.dui
class /Game/UI/Components/WBP_ListPage

style DetailPanel {
    AnchorData.SizeDelta = (600, 0)
}

HorizontalBox Root {
    Spacing = 24
    slot Rows default {
        + VerticalBox { Spacing = 15 }
        @slot SizeRule = Fill
    }
    slot Detail : DetailPanel
    slot Footer
}
```

- `slot` 写在树里洞所在的位置，名字和节点 id 共用一个空间：两个同名的槽位是 [DUI3001](../diagnostics/codes.md#dui3001)
  （原来的 [DUI3007](../diagnostics/codes.md#dui3007) 已退役）。
- `default` 标出宿主没指名时内容进的那个槽位，替代了重写 `GetDefaultSlotName` 的做法。每个文件只能有一个
  （[DUI3022](../diagnostics/codes.md#dui3022)；同一个槽位写两次 `default` 是 [DUI2019](../diagnostics/codes.md#dui2019)）。
- 块或样式负责洞的布局：组件、属性和槽位行——**不能**有子节点，因为里面放什么是宿主的事（DUI2019）。
- 带布局容器的槽位能放多个 widget；不带的只放一个。
- `(was: OldName)` 给槽位改名，和节点一样。

<a id="填槽"></a>

### 填槽

宿主**嵌套**就是填默认槽位；在实例里写一个同名的 `slot` 块，就是填具名槽位：

```dui
use "UI/Components/ListPage.dui" as ListPage
use "UI/Components/Row.dui" as Row

Widget Root {
    ListPage Page_2 {
        Row Item_0 { Label = "Warped Wire" }        // 进默认槽位 Rows
        Row Item_1 { Label = "Large Gear" }
        slot Detail {                               // 进名为 Detail 的槽位
            Text Note { Text = "A length of wire bent out of shape." }
        }
    }
}
```

填进去的只能是 widget。具名槽位放一个 widget——要放几个，先套一个容器（除非组件给这个槽位声明了布局容器）。

| 代码 | 情况 |
| --- | --- |
| [DUI5019](../diagnostics/codes.md#dui5019) | 在不是组件实例的节点下面写 `slot Name { … }` 填槽 |
| [DUI5020](../diagnostics/codes.md#dui5020) | 填的槽位组件没有声明 |
| [DUI3009](../diagnostics/codes.md#dui3009) | 父节点不接这个子节点：已经放满的内容 widget，或者同一个具名槽位填了两次 |
| [DUI2019](../diagnostics/codes.md#dui2019) | 一个块既在声明又在填充 |

在设计器里把 widget 拖进一个实例的具名槽位，写回时会写进那个槽位的 `slot Detail { … }`；实例还没有这个块时会先写出它。
见[设计器写回](write-back.md)。

**提示：**

在面板里给一个组件做动画，用它的渲染变换：`RenderTranslationTo`、`RenderOffsetTo`、`RenderScaleTo`、`RenderAngleTo`。
  它们移动、缩放、旋转画出来的东西，不动布局；面板下一次排布时会把锚定位置和尺寸写回去，直接动布局属性的补间会被它覆盖。
  见[动画](../concepts/animation.md)。
