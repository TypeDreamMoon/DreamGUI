---
title: Widget Blueprint
description: 一棵 UI 树是一个类：UDreamWidgetBlueprint 编译成 UDreamUserWidget；子类化与嵌套、命名槽、按名字绑定孩子、设计器直接改类而没有 Apply，被取代的 prefab 模型，以及 .dui 作为 Blueprint 的源文件。
---

在 DreamGUI 里，一棵 UI 树是一个**类**，不是一个拿来实例化的资产。编写一棵树得到的是一个 `UDreamWidgetBlueprint`，
它生成的类是 `UDreamUserWidget` 的子类；你可以子类化它、把它放进另一棵树、按名字绑定它的孩子 —— 和 UMG 的
Widget Blueprint 是同一个形状，只是由 DreamGUI 自己的 `FDreamWidgetBlueprintCompilerContext` 编译。

<a id="资产类实例"></a>

## 资产、类、实例

| | 是什么 |
| --- | --- |
| `UDreamWidgetBlueprint` | 编写用的资产（内容浏览器里的类型名是 **DreamUI Widget Blueprint**，*添加* 菜单里叫 **DreamGUI Widget**），持有设计器编辑的那棵树（`WidgetTree`）和图表 |
| `UDreamWidgetGeneratedClass` | 编译产物。编译把树复制到类上作为模板（archetype），实例由它构建 |
| `UDreamUserWidget` | 实例。它本身就是一个 `UDreamWidget`，正如 `UUserWidget` 是一个 `UWidget` |

两份拷贝是有意的，和 UMG 一样：直接改模板会在会话中途改掉每个活实例的模板。

`UDreamUserWidget` 本身是控件，所以嵌套是白来的：一个类的实例放进另一棵树，就只是那棵树里的一个控件，而它自己的内容来自它自己的类。
它的内容放在 `WidgetTree`（`UDreamWidgetTree`）后面而不是直接放在孩子里，这层间接不是装饰：没有它，"一个控件的类"和
"一棵树的类"就是同一回事。`WidgetTree` 是 Transient 的 —— 类模板绝不能带着展开的子树，否则嵌套进来的类会被烤进父级的模板、
再也不跟随它自己的类 —— 每次都在 Initialize 时从类重新构建。

资产本身和 `UWidgetBlueprint` 一样从不烹饪：烹饪后的版本有生成的类和字节码，别的都不需要。未烹饪的内容则不同：编辑器构建会在加载时
丢掉 Blueprint 保存的字节码、从 Blueprint 重建，所以任何运行未烹饪内容的进程 —— 包括 `-game` 和 Standalone Game —— 都需要这个类和它的编译器。
这就是 `DreamGUIEditor` 模块是 `UncookedOnly` 而不是 `Editor` 的原因：否则在 `-game` 里控件 Blueprint 的图表什么也不做。

<a id="实例的生命周期"></a>

## 实例的生命周期

`UDreamUserWidget` 上能覆写的时机，和 UMG 的对应：

| 事件 | 时机 |
| --- | --- |
| `PreConstruct(bIsDesignTime)` | Initialize 最开始，设计器预览和运行时都会调用 |
| `OnInitialized` | 内容构建完成之后（C++ 是 `NativeOnInitialized`），预览里也会调用 |
| `OnConstruct` | 开始游戏：树已注册、在屏幕上；只在游戏世界里 |
| `OnDestruct` | 拆除时 |
| `OnTick` | 只有 `bWantsTick` 打开时才每帧调用，默认关 —— 一个从不用的 tick 也要付管理器列表、虚调用和 ProcessEvent 的钱 |

指针、拖拽、按键、焦点和导航事件（`OnPointerClick`、`OnDrop`、`OnKeyDown`、`OnFocusReceived`、`OnNavigate`……）也都在这里。
`bAllowEventBubbleUp` 默认开：用户控件是一个容器，一个悄悄吞掉内部所有点击的容器会搞坏它所在的每个界面；设成关，事件就在这一层被消费。
`bCanNavigateHere` 默认关，导航搜索照常越过它，只在你需要 `OnNavigate` 时打开。

<a id="子类化与嵌套"></a>

## 子类化与嵌套

- **子类化**：一个只加逻辑的子类不声明自己的树，实例化的是父类的树（`FindWidgetTreeArchetype` 找最近一个有树的类）——
  否则为了改一个函数而子类化一个界面，会悄无声息地得到一个空界面。属性绑定和事件路由沿继承链**累加**：
  子类没加任何绑定也要遵守父类的，加了的也不会顶替父类的。
- **嵌套**：把一个类拖进另一棵树（或在 `.dui` 里写成节点类型），设计器把这个实例折叠成一行 —— 对按钮、滑条这种成品来说是对的。
  要让宿主往里放东西，类必须明确开一个洞，也就是命名槽。

<a id="命名槽"></a>

## 命名槽

`UDreamNamedSlot` 是一个行为，它把所在的控件标成一个**洞**，由放置这个类的宿主来填：类说"在哪"，宿主说"放什么"。

- 槽名就是这个控件的显示名，也就是作者已经在层级里看到、敲过的那个名字 —— 和 UMG 的 `UNamedSlot` 同样的选择。
- 默认只收一个控件（和 `UDreamContentWidget` 一样，收多个的洞就是面板了）。洞本身已经是面板时，打开 `bAcceptsSeveral`。
- `bIsDefaultSlot` 标出默认槽：宿主不指名时，嵌套进来的内容就去那里。每个类只能有一个（DUI3022）。

宿主填好的内容是**宿主自己树里的控件**，不是对另一个资产记录的差异；运行时在 Initialize 末尾把它们挂到对应的槽下。
相关的调用在 `UDreamUserWidget` 上：`GetContentForNamedSlot`、`SetContentForNamedSlot`、`FindSlotWidget`、
`GetDefaultSlotName`（C++ 类可以覆写它）、`GetNativeSlotNames`（内容由代码而不是模板构建的类用它声明自己的洞）。

另有一个名字很像的 `UDreamNamedSlotHost`，它是 UMG `INamedSlotInterface` 的对应物：**一棵树内部**的运行时"名字 → 孩子"映射
（`SetContentForSlot`、`GetContentForSlot`、`ClearSlot`、`GetSlotNames`），不涉及别的资产。不要和类之间的命名槽混为一谈。

在 `.dui` 里，组件用 `slot` 开洞、用 `props` 声明宿主能设的属性；宿主直接在实例里嵌套内容，就进了默认槽：

```dui
class /Game/UI/Components/WBP_Card

props {
    Text Title
}

VerticalBox Root {
    Spacing = 8
    Padding = (16, 16, 16, 16)

    Text Heading {
        Text <- Title
        FontSize = 22
    }
    slot Body default
}
```

```dui
use "UI/Components/Card.dui" as Card

Widget Root {
    + Overlay {}

    Card Audio {
        Title = "Audio"
        Text Hint { Text = "Master volume and effects" }
    }
}
```

完整写法见 [`.dui` 组件](../language/components.md)。

<a id="按名字绑定孩子"></a>

## 按名字绑定孩子

树里每个有 id 的控件都会成为类的一个成员变量，图表直接读它。C++ 父类里声明的、类型是控件的属性，也按**同名**绑定到树里的控件：
`InitializeWidgetStatic` 绑定 `UDreamUserWidget` 之下声明的每一个控件类型的属性，不管有没有标记。标记决定的是另一件事 ——
`meta = (BindDreamWidget)` 让"树里没有这个控件"成为编译错误：

```cpp
UCLASS()
class UMySettingsScreen : public UDreamUserWidget
{
    GENERATED_BODY()

protected:
    // 树里必须有一个叫 Confirm 的控件，否则编译失败
    UPROPERTY(meta = (BindDreamWidget))
    TObjectPtr<UDreamWidget> Confirm;
};
```

这个名字故意不叫 UMG 的 `BindWidget`：两者在运行时不会冲突，但头文件是给人读的，在 Dream 控件上写一个裸的 `meta=(BindWidget)`
等于说它属于一个它并不属于的框架。动画的对应物是 `BindDreamWidgetAnim`，以及不报错的 `BindDreamWidgetAnimOptional`。
运行时也可以用 `GetWidgetFromName` 按变量名找。

事件处理用**路由**：`OnClick -> HandleConfirm` 把一个事件接到用户控件上的一个函数（UMG 也把事件处理放在那里）。编译器把每条路由解析进
`UDreamWidgetBlueprint::EventBindings`，`UDreamUserWidget::BindEventBindings` 在 Initialize 时接上。设计器里同一件事是细节面板的
*Events* 分区：`+` 建一个签名正确的自定义事件和指向它的路由。

<a id="设计器直接改类"></a>

## 设计器直接改类

设计器编辑的是这个类的一个预览实例，改动写回**类**。所以没有 Apply 这一步，也没有要对账的每实例覆盖列表。
预览宿主是 Transient、不可事务的，编写用的那棵树才是唯一可撤销的一半。设计器本身见[设计器](../tools/designer.md)。

**提示：**

这个插件 fork 自的 prefab 资产模型已经没有了，连同 `SavePrefab`、`Apply`、`ClearLoadedPrefab` 和 *Save on Apply*。
  插件不带重定向：旧类名保存的资产，要借来 2.1.0 的重定向段、用 1.0.0 重存一次，见[迁移](../guides/migration.md)。

<a id="duiblueprint-的源文件"></a>

## `.dui`：Blueprint 的源文件

一棵树也可以写成文本。`UDreamTextUserWidget` 是一个层级来自 `.dui` 文件的用户控件：它的 `SourceFile` 指向文件，
编译时 `FDreamWidgetBlueprintCompilerContext` 读文件、建树、把它装成 Blueprint 的编写层级 —— 在类声明变量之前。
从那一刻起它在每个方面都是一个普通的 `UDreamUserWidget`：同样的生成类、每个控件一个成员变量、同样的属性绑定、同样的设计器。

- 文本替代的是**编写**这一步，不是运行时。
- `SourceFile` 只能在类默认值里改（`EditDefaultsOnly`）：一个类，一棵树。文件只在编译时读，这是唯一能为每个控件声明成员变量、
  解析绑定、做检查、把层级交给设计器的时刻。
- 它是仅编辑器数据，烹饪时去掉；相对路径以 `DUI/` 源目录为根。
- 设计器工具栏里的 *Set Source File...* 把一个类指向一个文件；设计器里的改动写回文件里对应的那一行。

怎么写见 [`.dui` 概览](../language/overview.md)。

<a id="显示出来"></a>

## 显示出来

| 调用 | 做什么 |
| --- | --- |
| **Create Dream Widget** 节点 | UMG 的 Create Widget 节点：选了类之后为它的每个 Expose on Spawn 属性开一个引脚（`.dui` 的每个 `props` 和 `viewmodels` 条目都是一个），另有 Owning Player 引脚，返回值的类型就是这个类。引脚在 widget 初始化**之前**赋值，所以 Pre Construct、On Initialized、On Construct 和每个绑定的第一次取值都已经看得到它们；UMG 在 Create 之后才赋值，对 On Initialized 来说太晚了 |
| `UDreamUserWidget::AddToViewport` | UMG 的 Add to Viewport：放上拥有者玩家的屏幕并打开它（没指定玩家时是第一个本地玩家的）。和 UMG 一样，已经有父节点的 widget 会被拒绝。拿下来用 `RemoveFromParent` |
| `UDreamUserWidget::AddToPlayerScreen` | 同上，但没有拥有者玩家时拒绝，而不是退回到 0 号屏幕 |
| `UDreamUIBPLibrary::AddWidgetOfClassToViewport` | 创建并放上视口，一个调用；返回值的类型跟着类走 |
| `UDreamUIBPLibrary::CreateDreamWidgetOfClass` | 只实例化，不放到任何地方（不在屏幕上的状态，和 `ConstructWidget` 一样）。和 UMG 的 `Create` 一样不出现在蓝图菜单里——Create Dream Widget 节点代表它；已经调用它的图照常编译 |
| `BeginCreateDreamWidget` / `FinishCreateDreamWidget` | C++：节点的两半。中间写新 widget 自己的属性（比如 `SetViewModel`），赶在它初始化之前 |
| `UDreamScreenUISubsystem::CreateWidgetOnScreen` / `AddToPlayerScreen` | 指定玩家的屏幕 |
| `UDreamWorldWidgetComponent::SetWidgetClass` | 放进关卡，见[世界空间 UI](world-space.md) |

类的成员与 UMG `UUserWidget` 的逐项对照见 [`UDreamUserWidget` 参考](../Reference/DreamUserWidget.md)。
