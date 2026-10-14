---
title: 绑定与路由
description: 三种箭头把树接到类的代码上——绑定驱动属性、路由调用处理函数、双向绑定镜像变量——成员路径、绑定什么时候更新，以及每个 widget 都有的 Shown。
---

三种箭头把树接到类的代码上：

| 写法 | 方向 | 例子 |
| --- | --- | --- |
| `Prop <- Expr` | 类 → 树：属性跟着表达式走 | `Text <- GetTitle()` |
| `Event -> Handler` | 树 → 类：事件调用类的一个函数（也写作 `+=`、`=`） | `OnClicked -> HandleConfirm` |
| `Prop <-> Var` | 两个方向：属性和变量互相镜像 | `Value <-> Volume` |

这里的“类”就是这棵树编译进去的那个 user widget——UMG 也把事件处理放在那里。函数和变量可以写在 C++ 里，也可以写在 Blueprint 的图表里。
三种箭头的右边都可以经过一条**成员路径**走到类持有的对象上（`Player.Name`、`Settings.Apply()`），最常见的就是
[视图模型](viewmodels.md)。

<a id="--驱动属性"></a>

## `<-` 驱动属性

```dui
class /Game/UI/WBP_Inventory

Widget Root {
    + VerticalBox { Spacing = 8 }
    Text Title { Text <- GetTitle() }
    Image Glow { RenderOpacity <- Fade() * 0.8 }
    Widget Badge { Shown <- Count() > 0 && !IsLocked() }
}
```

右边是一个表达式，随着类的运行重新求值。表达式里可以有：

- 类的函数调用，`Func()` 或 `Func(a, b)`；
- 类的变量，`Volume`——包括 `props` 和 `viewmodels` 里的条目；
- 经过一个对象变量的成员路径，`Player.Name`、`Player.Stats.Title`，或者以调用结尾的路径，`Player.FormatGold(Player.Gold)`；
- 字面值：数字、字符串、`true`、`false`，或 `@Resource`；
- 运算符，从最松到最紧：`||`；`&&`；`==` `!=`；`<` `<=` `>` `>=`；`+` `-`；`*` `%`；以及前缀 `!` 和 `-`。括号分组。

没有 `/`——它属于路径和注释——也没有 `? :`、下标 `Items[0]`、数组或字典字面值。表达式里每加一个运算符，都得能降成 Blueprint 图、
教给 VS Code 扩展的扫描器、写进三处文档，所以除法和二选一写成类上的函数，下标写成一个带参数的函数；对 widget 来说，二选一就是一个
[`if`](control-flow.md)。

一个裸的 `Func()` 直接绑定那个函数；更复杂的表达式会被编译成一个生成的函数。对目标属性的要求：

- 属性要有 setter（[DUI5005](../diagnostics/codes.md#dui5005)）——组件的 prop 例外，见[编写组件](components.md)。
- 只能绑定 widget、它的视觉或某个行为上的**整个**属性（[DUI5008](../diagnostics/codes.md#dui5008)）：不能绑槽位属性、
  容器属性，也不能绑结构体里的某个字段（`AnchorData.SizeDelta <- …` 不行）。

| 代码 | 情况 |
| --- | --- |
| [DUI2011](../diagnostics/codes.md#dui2011) | 右边解析不了 |
| [DUI5004](../diagnostics/codes.md#dui5004) | 类上没有这个函数，或者裸绑定的函数带参数 |
| [DUI5011](../diagnostics/codes.md#dui5011) | 编译器没法把这个表达式降成一个函数 |
| [DUI5023](../diagnostics/codes.md#dui5023) | 路径里有一段，它所在的类上没有，或者 Blueprint 看不到 |
| [DUI5024](../diagnostics/codes.md#dui5024) | 路径越过一个不是对象的值继续往下走 |

路径从类的一个变量开始（`viewmodels` 条目、prop 或者 Blueprint 变量），之后每一段都是前一段所持对象的类上的成员，必须对 Blueprint 可见。
路径上有对象没设好时，绑定不求值，属性保留它原来的值。

在 VS Code 里，编辑器运行时 `<-` 后面的补全会去问编辑器这个类到底声明了什么，函数调用有参数提示，悬停能看到函数和变量的类型；
`Player.` 后面按视图模型的类补全成员。

<a id="绑定什么时候更新"></a>

### 绑定什么时候更新

编译器记下表达式读到的每个变量和成员。它们**全都**是 FieldNotify 的——这个类上的变量，以及路径上实现了 `INotifyFieldValueChanged`
的对象的每个成员——绑定就订阅它们：其中一个宣布变化时才更新，变化之间不花任何代价。否则它每帧读一次。带实参的调用（`Format(Gold)`）
可能读任何东西，所以含有它的绑定总是每帧读；让视图模型把格式化好的值作为一个成员提供，就是出路。

`DreamUI.Binding.Dump` 列出每个活着的 widget 的绑定：多少在变化时更新、多少每帧读，以及为什么。`DreamUI.Binding.ForcePoll 1` 让它们全部
每帧读，用来区分“没人宣布这次变化”和“绑定写错了”。

**警告：**

只读 FieldNotify 变量的绑定，在它的 Blueprint 重新编译之后不再每帧读，而是等变量宣布变化。C++ 里给这样的变量赋值却不广播——FieldNotify
  从来不允许这样做——以前会被每帧的轮询掩盖过去，现在不会了；`DreamUI.Binding.ForcePoll 1` 能把它找出来。

<a id="--和--路由事件"></a>

## `->`、`+=` 和 `=` 路由事件

```dui
class /Game/UI/WBP_Dialog

Widget Root {
    + HorizontalBox { Spacing = 16 }
    Native.Button Confirm { OnClicked -> HandleConfirm }
    Widget Cancel {
        + UIButton { OnClick -> HandleCancel }
    }
}
```

左边是 widget、它的视觉或某个行为上的一个事件：`BlueprintAssignable` 多播委托（`Controls/` 那一族控件声明的）、
`FDreamUIEventDelegate` 属性（较老的 `Interaction/` 行为声明的），或者单播委托。每个 widget 都有 `OnVisibilityChanged`、`OnFocusReceived`、
`OnFocusLost`。

运算符说的是这个事件的其他监听者怎么办：

| 运算符 | 含义 | 用于 |
| --- | --- | --- |
| `+=` | 在其他监听者旁边再加上这一个 | 多播委托、`FDreamUIEventDelegate` |
| `=` | 这就是事件唯一的监听者 | 单播委托 |
| `->` | 按事件的种类，二者之一 | 任何事件 |

单播委托装不下第二个监听者，所以在它上面写 `+=`；多播事件上写 `=` 就得扔掉控件自己和 Blueprint 图表接上的监听者——这两种都是
[DUI5025](../diagnostics/codes.md#dui5025)。

右边是下面三种之一：

- 类的一个函数，不带括号（`HandleConfirm`）：它收事件发出的东西；
- `emit` 一个本类声明的事件，带上它的参数（见[编写组件](components.md)）；
- 类持有的某个对象上的函数，经过一条成员路径（`Settings.Apply`）。带括号时，括号里就是实参——类上的表达式，事件自己的参数按名字可用
  （`Settings.SetVolume(Value)`）；不带括号时，函数要么什么都不收，要么收的恰好是事件发出的那些。对象没设好时什么都不调用。

```dui
class /Game/UI/WBP_Audio

viewmodels {
    SettingsVM Settings = new
}
events {
    Picked(Number Index)
}

VerticalBox Root {
    Native.Slider Volume { OnValueChanged += Settings.SetVolume(Value) }
    Native.Button Apply  { OnClicked += Settings.Apply() }
    Native.Button Pick   { OnClicked -> emit Picked(0) }
}
```

| 代码 | 情况 |
| --- | --- |
| [DUI5010](../diagnostics/codes.md#dui5010) | 左边不是目标上的事件 |
| [DUI5025](../diagnostics/codes.md#dui5025) | `+=` 写在单播委托上，或者 `=` 写在多播事件上 |
| [DUI6004](../diagnostics/codes.md#dui6004) | 类上没有这个处理函数 |
| [DUI6005](../diagnostics/codes.md#dui6005) | 处理函数的参数和事件的参数不一致 |
| [DUI6018](../diagnostics/codes.md#dui6018) | 路径走到的对象的类上没有这个函数，或者 Blueprint 不能调用它 |
| [DUI6019](../diagnostics/codes.md#dui6019) | 实参和函数对不上 |

编译器把每条路由解析进 `UDreamWidgetBlueprint::EventBindings`，`UDreamUserWidget::BindEventBindings` 在 Initialize 时把它们接上。
在设计器里，同一件事就是细节面板的 *Events* 一节：那里的 `+` 会建一个签名正确的自定义事件，并写出指向它的路由。

**警告：**

`FDreamUIEventDelegate` 自带的逐实例事件列表是遗留功能，只读。存在里面的绑定照样会触发，也还能从面板上删掉，但不能再新建：
  那种绑定是在任意对象上调一个函数、带一个字面参数，UMG 里没有对应物，`.dui` 里也没有写法。打开带有这种绑定的面板会记一条警告点名它。
  不会自动改写——把“在那个行为上用这个值调 `Foo`”改成“调 user widget 上的一个处理函数”会改变游戏的行为——所以下次碰到时把它们重写成路由。

<a id="--双向镜像"></a>

## `<->` 双向镜像

```dui
class /Game/UI/WBP_Audio

Widget Root {
    + VerticalBox { Spacing = 8 }
    Native.Slider Volume { Value <-> MasterVolume }
    Native.Toggle Mute { bIsOn <-> bMuted }
}
```

属性跟着变量走，控件自己产生的变化再写回变量。变量必须是这个类的 FieldNotify 变量，或者类持有的对象上的一个成员：

```dui
Native.Slider Volume { Value <-> Settings.MasterVolume }
```

写回一个成员时，对象的类上有 `BlueprintCallable` 的 `Set<Member>`（一个参数）就调用它，否则直接写这个成员并在那个对象上宣布变化。
两种方式都写不了的成员是 [DUI6020](../diagnostics/codes.md#dui6020)。

`<->` 是两半拼起来的：正向是一个生成的 getter，和任何绑定函数一样；反向是一条生成的路由
`OnValueChangedBP -> 生成的 setter`。所有值类控件都通过 `OnValueChangedBP` 这个约定的名字广播变化，没有这个事件的目标会报
DUI5010。`<->` 右边的名字不是这个类的变量（以上一次编译为准）是 DUI5011。

VS Code 里 `<->` 后面的补全列出类的变量，FieldNotify 的排在前面。

<a id="shown"></a>

## `Shown`

每个 widget 都有 `Shown`：`true` 是 Visible，`false` 是 Collapsed。它自己不存任何东西——读它就是读 `Visibility`，写它就是写
`Visibility`——条件绑定的就是它：

```dui
class /Game/UI/WBP_Detail

Widget Root {
    + VerticalBox { }
    Widget Detail {
        Shown <- HasDetail()
    }
    Text Empty {
        Text = "Nothing selected"
        Shown = false
    }
}
```

一个 Hidden 或命中测试不可见的 widget 也算“显示”，`Shown` 为 `true` 时它保持原来的状态，不会被改成 Visible。`if` 给每个分支里的 widget 绑的也是 `Shown`，见
[if 与 for](control-flow.md)。`Shown` 和 `Visibility` 是同一个值，设计器写回时只比较文件写出来的那一面。

<a id="这两条是语言层的运行时还有一条"></a>

## 这两条是语言层的；运行时还有一条

上面讲的 `<-` / `->` / `<->` 都是**语言层**的绑定 —— 由编译器解析，认 FieldNotify 与视图模型。

另有一条**运行时**的路：`UDreamDataBinding` —— 一个**组件**，用**反射按属性名**连，所以它能接**任意两个 `UObject`**，不需要编译期知道类型；代价就是字符串属性名加反射。

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `SourceObject` / `SourceProperty` | 空 | 从谁读、读哪个属性 |
| `TargetObject` / `TargetProperty` | 空 | 写给谁、写哪个属性 —— ⭐ **`TargetObject` 不填时默认是「拥有这个组件的那个 widget」** |
| `UpdateMode` | **`OnAwake`** | `EDreamBindingUpdateMode`：`OnAwake` / `EveryFrame` / `Manual` |

入口是 `ApplyBinding()`（蓝图可调）。

<a id="按尺寸改外观udreamresponsivebehaviour"></a>

### 按尺寸改外观：`UDreamResponsiveBehaviour`

同一文件里的 `UDreamResponsiveBehaviour` 是一条**断点**行为：按**父级尺寸**（在根上则按自己的尺寸）应用**第一个匹配的**规则。

`FDreamResponsiveRule` 一条规则的字段：`Name`、`MinWidth` / `MaxWidth` / `MinHeight` / `MaxHeight`（默认 **0**）、`Visibility`、`RenderOpacity`（默认 **1**）。

⭐ **类注释里那句话是这块最该记住的**：

> 规则是对控件**编写时外观的覆盖**，不是对它的完整描述：**一个不匹配任何规则的尺寸会把控件放回原样**，而不是让它**穿着上一条规则的戏服**。

`ActiveRule`（`Transient`）记当前生效的是哪一条；`bHasBaselineAppearance` 加 `BaselineVisibility` / `BaselineRenderOpacity` 存的是**规则首次覆盖它之前**的外观 —— 那是「**这个组件借了什么**」的记录，**故意不是 `UPROPERTY`**：跨加载没有意义（`ActiveRule` 也是 `Transient`），所以**在它再次拿走什么之前，什么都不欠**。

⚠️ 这两个类型同住 `Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h` —— **文件名装的是 `UDreamDataBinding`**，别按名字去别处找。
