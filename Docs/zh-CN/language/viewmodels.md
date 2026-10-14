---
title: "视图模型"
description: "viewmodels 块声明类持有的视图模型以及每个视图模型的来源；绑定、路由和循环通过成员路径到达它们；如何编写一个视图模型，以及通过它的绑定何时更新。"
---

<a id="view-models"></a>

## 视图模型

视图模型是一个对象，持有界面的状态和命令，与显示它们的控件分开。控件绑定到它；它从不引用控件。一个视图模型可以服务多个界面，它的逻辑可以脱离任何
UI 进行测试。

```
viewmodels {
    PlayerVM    Player                   // the host gives it
    SettingsVM  Settings = new           // this widget makes one
    InventoryVM Inventory = global       // from the game's registry, by class
    InventoryVM Stash = global "Stash"   // by class and name
    PartyVM     Party = parent           // the nearest enclosing widget's PartyVM
}

VerticalBox Root {
    Text Name { Text <- Player.Name }
    Native.Slider Volume { Value <-> Settings.MasterVolume }
    Native.Button Apply { OnClicked += Settings.Apply() }
}
```

每个条目是一个类型和一个名称，还可以选择性地指定对象的来源。类型是一个类：它的反射名
（C++ `UPlayerVM` 的 `PlayerVM`）、完整路径（`/Script/MyGame.PlayerVM`、`/Game/UI/BP_PlayerVM`），或由
`use /Script/MyGame.PlayerVM as PlayerVM` 给出的名称。不指向任何类、或指向两个类的类型是 DUI6015；两个同名条目
是 DUI3024；类已经使用的名称是 DUI6016。

每个条目会成为类的一个变量——那个类的对象引用，FieldNotify，Expose on Spawn，位于
*ViewModels* 分类下——所以凡是能设置变量的地方，都能把视图模型交给控件。

<a id="where-the-object-comes-from"></a>

### 对象从哪来

| 写法 | 含义 |
|---|---|
| `PlayerVM Player` | 由宿主交给它：创建控件的节点上的引脚、C++ 或 Blueprint 中的 `SetViewModel(Name, Object)`、对该变量的 Blueprint Set，或者当这个控件被当作组件使用时、宿主的 `.dui` 行（`Row Audio { Channel <- Settings.Audio }`）。 |
| `= new` | 在没被交到对象时，控件自己建一个，outer 是它自己。设计器的预览里也会建，于是预览显示视图模型的默认值；这样的实例 `IsDesignTimeInstance()` 返回 `IsDesignTimeInstance()` true。对抽象类写 `new` 是 DUI6017。 |
| `= global`、`= global "Name"` | `UDreamViewModelSubsystem`（每个 game instance 一个）：`Register(Object, Name)` 共享一个对象，条目按类填充，写了名字就按类和名字填充。还没有合适的对象时，条目会等待，有合适的对象注册进来时再填上。设计器里是空的。 |
| `= parent`、`= parent "Name"` | 最近一层外层 user widget 上、类合适（写了名字就还要同名）的那个条目，并且跟着它变化。组件不用被交到对象、也能共用宿主视图模型的办法。 |

对象在 On Initialized 运行之前、在任何绑定第一次取值之前就已经就位。宿主已经设过的条目，保留它被交到的对象。

<a id="writing-a-view-model"></a>

### 写一个视图模型

任何 UObject 类都可以。实现了 `INotifyFieldValueChanged` 的类——`UDreamViewModel` 是方便用的基类，
UE 自己的 `UMVVMViewModelBase` 用法相同——会在字段变化时通知绑定，经过它的绑定就只在那时更新。未实现的类则每帧读取。

```cpp
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

- 绑定读取的成员必须对 Blueprint 可见：`BlueprintReadOnly` / `BlueprintReadWrite` 属性，
  `BlueprintCallable` / `BlueprintPure` 函数（DUI5023 指出不可见的那一个）。
- `DREAM_VM_SET(Member, Value)` 在值变了的时候赋值并广播这个字段。Blueprint 视图模型的 FieldNotify 变量的 Set 节点
  自己会广播。
- 由别的值算出来的值（`Gold` 算出的 `GoldText`）在它的来源变化时用 `BroadcastFieldValueChanged` 宣布。由视图模型提供这种直接能显示的成员，
  就是它转换值的方式；没有另外的转换器。
- 想在 UI 通过 `<->` 写回一个值时做出反应的视图模型，提供 `Set<Member>`（BlueprintCallable，
  一个参数）：`<->` 会调用它，而不是直接写属性。
