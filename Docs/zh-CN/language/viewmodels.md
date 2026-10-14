---
title: 视图模型
description: viewmodels 块声明类持有的视图模型和它们从哪来；绑定、路由和循环用成员路径读写它们；怎样写一个视图模型，以及经过它的绑定什么时候更新。
---

**提示：**

本页写的是 `main` 上、1.0.0 之后的功能，会随下一个版本发布（见[更新日志](../changelog/index.md)）。

视图模型是一个对象，持有界面的状态和命令，和显示它们的 widget 分开。widget 绑定到它；它从不引用 widget。
同一个视图模型可以服务几个界面，它的逻辑也可以脱离任何 UI 来测试。

```dui
class /Game/UI/WBP_Hud

viewmodels {
    PlayerVM    Player                   // 由宿主交给它
    SettingsVM  Settings = new           // 这个 widget 自己建一个
    InventoryVM Inventory = global       // 从游戏的注册表里按类取
    InventoryVM Stash = global "Stash"   // 按类和名字取
    PartyVM     Party = parent           // 最近一层外层 widget 的 PartyVM
}

VerticalBox Root {
    Text Name { Text <- Player.Name }
    Native.Slider Volume { Value <-> Settings.MasterVolume }
    Native.Button Apply { OnClicked += Settings.Apply() }
}
```

任何 `UObject` 类都可以当视图模型。`UDreamViewModel` 是方便用的基类；任何实现了 `INotifyFieldValueChanged` 的类——包括
UE 自己的 `UMVVMViewModelBase`——都会被同样地订阅，而 DreamGUI 不依赖 ModelViewViewModel 插件。

<a id="声明"></a>

## 声明

`viewmodels { … }` 写在文件顶层，和 `props`、`events` 一样。每一行是一个类型和一个名字，后面可以跟它的来源。

类型是一个类：它的反射名（C++ 的 `UPlayerVM` 写作 `PlayerVM`）、完整路径（`/Script/MyGame.PlayerVM`、`/Game/UI/BP_PlayerVM`），
或者 `use /Script/MyGame.PlayerVM as PlayerVM` 给的名字。

每个条目会成为类上的一个变量：那个类的对象引用，FieldNotify，Expose on Spawn，放在 *ViewModels* 分类下。所以凡是能设一个变量的
地方，都能把视图模型交给这个 widget。

| 代码 | 情况 |
| --- | --- |
| [DUI2021](../diagnostics/codes.md#dui2021) | 一行不是 `类型 名字` 加可选来源的形状，或者块写在了节点里 |
| [DUI3024](../diagnostics/codes.md#dui3024) | 两个同名条目 |
| [DUI6015](../diagnostics/codes.md#dui6015) | 类型找不到类，或者找到两个 |
| [DUI6016](../diagnostics/codes.md#dui6016) | 名字已经被类的另一个成员占用 |
| [DUI6017](../diagnostics/codes.md#dui6017) | 对抽象类写 `= new` |

父类是 C++ 类、并且已经有一个同名、正是这个类的对象引用属性时，这一行不另建变量，而是描述那个属性：它的对象从哪来。

<a id="对象从哪来"></a>

## 对象从哪来

| 写法 | 含义 |
| --- | --- |
| `PlayerVM Player` | 宿主交给它：创建这个 widget 的节点上的引脚、C++ 或 Blueprint 里的 `SetViewModel(Name, Object)`、Blueprint 里对这个变量的 Set，或者这个 widget 被当作组件使用时、宿主 `.dui` 里的一行（`Row Audio { Channel <- Settings.Audio }`）。 |
| `= new` | 没被交到对象时，widget 自己建一个，outer 是它自己。设计器的预览里也会建，于是预览显示的是视图模型的默认值；这样的实例 `IsDesignTimeInstance()` 返回 true。 |
| `= global`、`= global "Name"` | 从 `UDreamViewModelSubsystem`（每个 game instance 一个）里取：按类取，写了名字就按类和名字取。还没有合适的对象时，条目会等着，有合适的对象注册进来时再填上。设计器里是空的。 |
| `= parent`、`= parent "Name"` | 最近一层外层 user widget 上、类合适（写了名字就还要同名）的那个条目，并且跟着它变。组件不用被交到对象、也能共用宿主视图模型的办法。 |

对象在 On Initialized 运行之前、在任何绑定第一次取值之前就已经就位。宿主已经设过的条目，保留它被交到的那个对象：`= new`、`= global`、
`= parent` 只在没被交到对象时才起作用。

<a id="由宿主交给它"></a>

### 由宿主交给它

在 Blueprint 里，**Create Dream Widget** 节点为每个 Expose on Spawn 属性开一个引脚，每个 `viewmodels` 条目都是其中之一。
这些引脚在 widget 初始化**之前**赋值，所以 On Initialized 和每个绑定的第一次取值都已经能看到它们（见
[Widget Blueprint](../concepts/widget-blueprints.md#显示出来)）。

在 C++ 里，用 `SetViewModel` 按条目名交过去。在初始化之前交，用两段式的创建：

```cpp
#include "Core/DreamUserWidget.h"

UDreamUserWidget* Hud = BeginCreateDreamWidget(GetWorld(), HudClass);
Hud->SetViewModel(TEXT("Player"), PlayerVM);   // 写进条目的变量，并广播这个字段
FinishCreateDreamWidget(Hud);                   // Initialize、注册
Hud->AddToViewport();
```

之后再调 `SetViewModel` 也可以：对象写进变量、字段被广播，经过它的每个绑定都会重新读。传 null 清空条目。这个类没有叫这个名字的条目、
或者对象的类不对时，它返回 false 并在日志里打一条警告。`GetViewModel(Name)` 读出条目现在持有的对象。

组件的条目可以由宿主的 `.dui` 绑定，和一个 prop 一样：

```dui
// 组件：DUI/UI/Components/AudioRow.dui
class /Game/UI/Components/WBP_AudioRow

viewmodels {
    AudioChannelVM Channel
}

HorizontalBox Root {
    Text Label { Text <- Channel.DisplayName }
    Native.Slider Level { Value <-> Channel.Level }
}
```

```dui
// 宿主
class /Game/UI/WBP_Settings
use "UI/Components/AudioRow.dui" as AudioRow

viewmodels {
    SettingsVM Settings = new
}

VerticalBox Root {
    AudioRow Music { Channel <- Settings.Music }
    AudioRow Voice { Channel <- Settings.Voice }
}
```

<a id="-global游戏共享的视图模型"></a>

### `= global`：游戏共享的视图模型

`UDreamViewModelSubsystem` 是一个 game instance 子系统：跨关卡加载存在，PIE 的多个客户端之间不共享。它强引用交给它的对象，直到对象被注销或
game instance 关闭。

| 成员 | 作用 |
| --- | --- |
| `Register(ViewModel, Name = None)` | 共享一个对象。`None` 表示只按类。 |
| `Unregister(ViewModel)` | 把它拿出来，不管它是用什么名字注册的。 |
| `Find(Class, Name = None)` | 按下面的规则查找，找不到返回 null。返回值的类型跟着 `Class`。 |
| `FindOrCreate(Class, Name = None)` | 找，找不到就建一个（outer 是 game instance）、按 `Name` 注册并返回。抽象类返回 null。 |
| `OnRegistered` | 每次 `Register` 改变了注册表之后广播。 |
| `UDreamViewModelSubsystem::Get(WorldContext)` | C++：拿到这个世界的 game instance 的注册表。没有 game instance 的地方（设计器预览、编辑器世界）是 null。 |

查找规则：带名字时，找那个名字下、对象是这个类的条目；不带名字时，先找不带名字注册的、对象是这个类的条目，再找任何名字下对象是这个类的
第一个条目。同一个对象用同一个名字再注册一次什么都不替换；另一个对象用一个已被占用的名字注册，会替换掉前一个并广播——游戏就是这样整体换掉一个
全局视图模型的。不带名字时，条目按对象的确切类区分：两个不同类的对象可以都不带名字并存，同一个类的第二个不带名字的对象替换第一个。

一个 `= global` 条目在 widget 创建时还没找到对象，就会等着：之后注册进来的对象合适时再填上，经过它的绑定随之更新。

<a id="-parent沿用宿主的视图模型"></a>

### `= parent`：沿用宿主的视图模型

`= parent` 沿着父链往上找最近一层 user widget，取它上面类合适的条目（写了名字就还要同名），并在它变化时跟着变。Initialize 和
Construct 时都会重新找一次；C++ 里把 widget 挪到另一个宿主下之后，调 `RefreshParentViewModels()` 再找一次。

<a id="读和写"></a>

## 读和写

绑定、路由和循环都可以通过**成员路径**到达视图模型：

| 写法 | 详见 |
| --- | --- |
| `Text <- Player.Name`、`Text <- Player.Stats.Title`、`Text <- Player.FormatGold(Player.Gold)` | [绑定](bindings.md#--驱动属性) |
| `Value <-> Settings.MasterVolume` | [双向镜像](bindings.md#--双向镜像) |
| `OnClicked += Settings.Apply()`、`OnValueChanged += Settings.SetVolume(Value)` | [路由](bindings.md#--和--路由事件) |
| `for Item in Inventory.Items`、`Event -> Item.Use()` | [for 与 each](control-flow.md#for-和-each) |

路径上的对象没有都设好时，绑定不求值，属性保留它原来的值；路由什么都不调用。

<a id="写一个视图模型"></a>

## 写一个视图模型

<a id="c"></a>

### C++

```cpp
#include "ViewModel/DreamViewModel.h"

UCLASS()
class UPlayerVM : public UDreamViewModel
{
    GENERATED_BODY()
public:
    void SetHealth(float InHealth) { DREAM_VM_SET(Health, InHealth); }

    UFUNCTION(BlueprintCallable, Category = "Player")
    void Heal(float Amount) { SetHealth(Health + Amount); }

protected:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Player")
    float Health = 100.f;
};
```

- 绑定读的成员必须对 Blueprint 可见：`BlueprintReadOnly` / `BlueprintReadWrite` 的属性，`BlueprintCallable` / `BlueprintPure` 的函数
  （不可见的那一段报 [DUI5023](../diagnostics/codes.md#dui5023)）。
- `DREAM_VM_SET(Member, Value)` 在值变了的时候赋值并广播这个字段，没变时返回 false、不广播——每帧用同一个值调一次 setter，代价只是一次比较。
  `FText` 按显示出来的字符串比较。这个宏在任何有 FieldNotify 属性的 `INotifyFieldValueChanged` 类里都能用，不限于 `UDreamViewModel` 的子类。
- 由别的值算出来的值（`Gold` 变了之后的 `GoldText`），在它的来源变化时用 `BroadcastFieldValueChanged` 宣布。由视图模型提供这种直接能显示的成员，
  就是它转换值的方式；没有另外的转换器。
- 想在 UI 通过 `<->` 写回一个值时做出反应，就提供 `Set<Member>`（`BlueprintCallable`，一个参数）：`<->` 会调用它，而不是直接写属性。
  属性只读、又没有这样的 setter，是 [DUI6020](../diagnostics/codes.md#dui6020)。

<a id="blueprintunrealsharp-和-ue-的-mvvm"></a>

### Blueprint、UnrealSharp 和 UE 的 MVVM

- **Blueprint**：新建一个以 `DreamViewModel` 为父类的 Blueprint，变量勾上 FieldNotify。FieldNotify 变量的 Set 节点自己会广播；由别的值算出来的
  字段用 *Broadcast Field Value Changed* 节点宣布。`UDreamViewModel` 通过 outer 链拿到世界，所以 Blueprint 子类里能用要世界上下文的节点。
- **UnrealSharp**：`[FieldNotify]` 属性的 setter 自己会广播。属性要能被路径读到，`[UProperty]` 里要写 `BlueprintReadOnly` 或 `BlueprintReadWrite`。
- **`UMVVMViewModelBase`**：已经按 UE 的 MVVM 写好的视图模型直接能用，订阅方式完全一样。

不实现 `INotifyFieldValueChanged` 的类也能当视图模型，只是经过它的绑定每帧读一次。

<a id="设计时实例"></a>

### 设计时实例

设计器为 `= new` 条目建的实例 `IsDesignTimeInstance()` 返回 true：这时没有游戏在运行，游戏会建的子系统都不存在。一个在初始化时会去碰游戏的视图模型，
先查一下它。

<a id="绑定什么时候更新"></a>

## 绑定什么时候更新

编译器记下每个 `<-` 表达式读到的每个变量和成员。它们**全都**是 FieldNotify 的——类上的变量，以及路径上实现了 `INotifyFieldValueChanged` 的对象的每个成员——
绑定就订阅它们：只在其中一个宣布变化时更新，变化之间不花任何代价。否则绑定每帧读一次。带实参的调用（`Format(Gold)`）可能读任何东西，所以含有它的绑定
总是每帧读；让视图模型把格式化好的值作为一个成员提供，就是出路。

在 `for` 或 `each` 里，一项宣布它的某个被副本显示的成员变了时，只更新那一个副本，列表的其余部分不动。

控制台命令 `DreamUI.Binding.Dump` 列出每个活着的 widget 的绑定里多少是订阅的、多少是每帧读的，以及为什么；`DreamUI.Binding.ForcePoll 1`
让之后初始化的 widget 的绑定全部每帧读——用来区分"没人宣布这次变化"和"绑定写错了"。见[调试](../tools/debugging.md#诊断命令)。

**警告：**

在 C++ 里给 FieldNotify 成员赋值却不广播，FieldNotify 从来不允许这样做；以前每帧的轮询会把它掩盖过去，现在不会了。用 `DREAM_VM_SET` 或
  `BroadcastFieldValueChanged`，或者用 `DreamUI.Binding.ForcePoll 1` 找出是哪一个。

<a id="c-用户控件上的-fieldnotify-成员"></a>

## C++ 用户控件上的 FieldNotify 成员

`UDreamUserWidget` 自己实现了 `INotifyFieldValueChanged`，它的 C++ 子类可以声明 `UPROPERTY(FieldNotify)` 成员——和 UMG 的 `Widget.h` 一样，
`DreamUserWidget.h` 带上了 UHT 为它们生成的代码需要的头文件。`.dui` 里绑定到这样的成员会通过类的描述符订阅它，`DREAM_VM_SET` 就是它的 setter：

```cpp
UCLASS(Abstract)
class UMyListPage : public UDreamTextUserWidget
{
    GENERATED_BODY()
public:
    void SetHasSelection(bool bIn) { DREAM_VM_SET(bHasSelection, bIn); }

protected:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "List")
    bool bHasSelection = false;
};
```

```dui
Widget Details { Shown <- bHasSelection }
```
