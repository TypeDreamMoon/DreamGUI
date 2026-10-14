---
title: "绑定与路由"
description: "把树接到类的代码上的三种箭头——绑定驱动属性、路由调用处理函数、双向绑定镜像变量——成员路径、绑定何时更新，以及每个控件都有的 Shown。"
---

<a id="bindings-and-routes"></a>

## 绑定与路由

三种箭头把树接到类的代码上。

<a id="--drives-a-property"></a>

### `<-` 驱动属性

```
Text <- GetTitle()
RenderOpacity <- Fade() * 0.8
Shown <- Count() > 0 && !IsLocked()
```

右边是一个表达式，随着类的运行重新求值：

- 对类的函数的调用，`Func()` 或 `Func(a, b)`；
- 类的变量，`Volume` ——包括 `props` 条目和 `viewmodels` 条目；
- 经过一个对象变量的成员路径，`Player.Name`、`Player.Stats.Title`，或者末端带一次调用的路径，
  `Player.FormatGold(Player.Gold)`；
- 字面值：数字、字符串、`true`、`false`，或 `@Resource`；
- 运算符，从最松到最紧：`||`；`&&`；`==` `!=`；`<` `<=` `>` `>=`；`+` `-`；`*` `%`；以及前缀 `!` 和 `-`。
  括号分组。没有 `/`——它属于路径和注释——也没有 `? :`；除法和二选一写成
  一个函数。

一个裸的 `Func()` 直接绑定那个函数。更复杂的表达式会被编译成一个生成的函数。属性
需要一个 setter（DUI5005）——组件的 prop 例外，见[`props`](components.md#props)——而且只能绑定控件、它的视觉或某个行为上的**整个**属性（DUI5008）：
不能绑槽位属性，也不能绑结构体里的某个字段。类上没有的函数是 DUI5004；编译器
无法降下的表达式是 DUI5011。路径里有一段它所在的类上没有、或者无法向蓝图展示，就是 DUI5023；路径
越过一个不是对象的值继续往下走，就是 DUI5024。

**绑定什么时候更新。** 编译器记下表达式读到的每个变量和成员。当它们每一个都是
FieldNotify——这个类上的变量，以及路径上实现了
`INotifyFieldValueChanged` 的对象的每个成员——绑定就在其中之一宣布变化时更新，变化之间不花任何代价。
否则它每帧读一次。带实参的调用（`Format(Gold)`）可能读任何东西，所以含有它的绑定
总是每帧读；让视图模型把格式化好的值作为成员提供，就是出路。路径上某个对象
没设好时，绑定不求值，属性保留它原来的值。

`DreamUI.Binding.Dump` 列出每个活着的控件的绑定：多少在变化时更新、多少每帧读，以及
为什么。`DreamUI.Binding.ForcePoll 1` 让它们全部每帧读，用来区分没人宣布的变化和写错的
绑定。

<a id="---and--route-an-event"></a>

### `->`、`+=` 和 `=` 路由事件

```
OnClicked -> HandleConfirm
OnClick -> emit Picked(ValueIndex)
OnClicked += Settings.Apply()
OnValueChanged += Settings.SetVolume(Value)
OnValueChanged += Settings.SetVolume
OnInit = HandleInit
```

左边是控件、它的视觉或某个行为上的事件：一个 `BlueprintAssignable` 多播委托、一个
`DreamUIEventDelegate` 属性，或一个单播委托。运算符说的是这个事件的其他
监听者怎么办：

| 运算符 | 含义 | 用于 |
|---|---|---|
| `+=` | 在其他监听者旁边再加上这一个。 | 多播委托或 `DreamUIEventDelegate`。 |
| `=` | 这就是事件唯一的监听者。 | 单播委托。 |
| `->` | 按事件的种类，二者之一。 | 任何事件。 |

单播委托上写 `+=`（它装不下第二个监听者），或多播事件上写 `=`（那就得
扔掉控件和蓝图图表接上的监听者），都是 DUI5025。

右边是下面三种之一：

- 类的一个函数，不带括号（`HandleConfirm`）：它收事件发出的东西；
- `emit` 一个本类声明的事件，带上它的参数（见 `emit`）；
- 类持有的某个对象上的函数，经过一条成员路径（`Settings.Apply`）。带括号时，括号里就是它的
  实参——类上的表达式，事件自己的参数按名字可用（`Settings.SetVolume(Value)`）；
  不带括号时，函数要么什么都不收，要么收的恰好是事件发出的那些。对象没设好时什么都不调用。
  对象上没有这个函数是 DUI6018；实参对不上是 DUI6019。

DUI5010 是左边不是一个事件；DUI6004 和 DUI6005 是类上没有这个处理函数，或者它的参数对不上。

<a id="--mirrors-both-ways"></a>

### `<->` 双向镜像

```
Value <-> Volume
Value <-> Settings.MasterVolume
```

属性跟着变量走，控件自己产生的变化再写回变量。变量必须是这个类的
FieldNotify 变量，或者类持有的对象上的一个成员。写回一个成员时，对象的类上有
BlueprintCallable 的 `Set<Member>` 就调用它，否则直接写这个成员并在那个对象上宣布变化。
两种方式都写不了的成员是 DUI6020。

<a id="shown"></a>

### `Shown`

每个控件都有 `Shown`：`true` 是 Visible，`false` 是 Collapsed。它自己不存任何东西——读它就是读
Visibility，写它就是写 Visibility——条件绑定的就是它：

```
Widget Detail {
    Shown <- HasDetail()
}
```

一个 Hidden 或命中测试不可见、却仍然算显示的控件保持那个状态。

<a id="reflection-based-binding-at-run-time"></a>

### 运行时的反射绑定

上面的箭头是语言构造：编译器解析它们，并记录它们的依赖，包括
FieldNotify 成员和视图模型。`UDreamDataBinding` 提供了一条单独的、基于反射的单向绑定，在
运行时使用。它是核心 `DreamGUI` 模块里的一个 `UDreamUIBehaviour` 组件，按属性名连接两个 `UObject` 上
兼容的 `UPROPERTY` 值。它不要求 `.dui` 编译器知道它们的类型。

| 字段 | 默认 | 含义 |
|---|---|---|
| `SourceObject` / `SourceProperty` | 空 | 读哪个对象、哪个属性。 |
| `TargetObject` / `TargetProperty` | 空 | 写给哪个对象、哪个属性。`TargetObject` 不填时，默认是拥有这个组件的控件。 |
| `UpdateMode` | `OnAwake` | 一个 `EDreamBindingUpdateMode`：`OnAwake`、`EveryFrame` 或 `Manual`。 |

`ApplyBinding()` 是 BlueprintCallable。它通过反射解析命名的属性，并返回这个值
是否被应用成功。这条路径有字符串属性名和兼容反射类型的代价与约束；
它不是编译器的 FieldNotify 订阅机制。

<a id="responsive-appearance-with-udreamresponsivebehaviour"></a>

#### 用 `UDreamResponsiveBehaviour` 做响应式外观

`UDreamResponsiveBehaviour` 使用父级的尺寸（在根上则用控件自己的
尺寸）应用**第一个匹配的断点规则**。每个 `FDreamResponsiveRule` 包含：

| 字段 | 默认 | 含义 |
|---|---|---|
| `Name` | 空 | 规则的名字，生效时报告为 `ActiveRule`。 |
| `MinWidth` / `MinHeight` | `0` | 最小尺寸，含边界。 |
| `MaxWidth` / `MaxHeight` | `0` | 最大尺寸，含边界；零表示没有上限。 |
| `Visibility` | `Visible` | 规则匹配时应用的可见性。 |
| `RenderOpacity` | `1` | 规则匹配时应用的不透明度。 |

规则是对控件编写时外观的覆盖。如果一个规则都不匹配，组件会**恢复在第一次覆盖之前
它看到的外观**，而不是把上一条规则的可见性和不透明度留在原处。
`EvaluateResponsiveRules()` 是 BlueprintCallable；`GetActiveRule()` 报告当前生效的规则。

`ActiveRule` 是 `Transient`。私有的 `bHasBaselineAppearance`、`BaselineVisibility` 和 `BaselineRenderOpacity`
记录被规则临时覆盖的外观。它们故意不是 `UPROPERTY` 字段：这条记录
跨加载没有意义，而且在组件应用另一个覆盖之前，没有什么需要恢复。

这两个类都位于
[`Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h`](../../../Source/DreamGUI/Public/Interaction/DreamResponsiveBinding.h)。
文件名同时涵盖运行时绑定和响应式规则；没有单独的 `DreamDataBinding.h` 需要 include。
