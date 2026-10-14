---
title: "控件蓝图"
description: "一棵 UI 树是一个类——UDreamWidgetBlueprint 编译为 UDreamUserWidget——支持子类化和嵌套、命名槽、按名称绑定到子项、无需应用步骤即可编辑类的设计器、它所取代的预制体模型，以及作为蓝图源文件的 .dui。"
---

DreamGUI 中的 UI 树是一个**类**，而不是你实例化的资产。编写一棵树会给你一个 `UDreamWidgetBlueprint`，它
生成的类是 `UDreamUserWidget`；你可以子类化它、将它放入另一棵树中，并按名称绑定到它命名
的子项——与 UMG 的控件蓝图形状相同，由 DreamGUI 自己的 `FDreamWidgetBlueprintCompilerContext` 编译。

<a id="asset-class-instance"></a>

## 资产、类、实例

| | 它是什么 |
| --- | --- |
| `UDreamWidgetBlueprint` | 编写用资产（其类型在内容浏览器中显示为 **DreamUI Widget Blueprint**，*添加*菜单将其列为 **DreamGUI Widget**），持有设计器编辑的树（`WidgetTree`）和图表 |
| `UDreamWidgetGeneratedClass` | 它编译成的产物。编译会将树复制到类上作为模板（原型），实例由它构建 |
| `UDreamUserWidget` | 一个实例。它本身就是一个 `UDreamWidget`，正如 `UUserWidget` 是一个 `UWidget` |

这两份副本是有意为之，并且与 UMG 一致：原地编辑模板会在会话中途改变每个活动实例的模板。

因为 `UDreamUserWidget` 是一个控件，嵌套自然就有了：一个类的实例放入另一棵树中
就只是那棵树中的一个控件，而它自己的内容来自它自己的类。这些内容位于 `WidgetTree`
（`UDreamWidgetTree`）之后，而不是直接在其子项之中，这种间接并非装饰：没有它，
“一个控件的类”和“一棵树的类”就会是同一回事。`WidgetTree` 是瞬态的——类模板绝不能
携带展开的子树，否则嵌套类会被烘焙进其父级的模板中，并停止跟踪它自己的类——
并且在 Initialize 时、每次根据类构建。

和 `UWidgetBlueprint` 一样，该资产永远不会被烘焙：烘焙后的构建拥有生成的类（包含字节码），不需要
其他任何东西。未烘焙内容则是另一回事：编辑器构建在加载时会丢弃蓝图保存的字节码，并从
蓝图重新构建它，因此任何运行未烘焙内容的进程——包括使用 `-game` 启动的游戏或以独立游戏方式启动的游戏
——都需要这个类及其编译器。这就是为什么 `DreamGUIEditor` 是一个 `UncookedOnly` 模块，而不是 `Editor` 模块；
否则控件蓝图的图表在 `-game` 中将不起作用。

<a id="an-instances-life-cycle"></a>

## 实例的生命周期

`UDreamUserWidget` 可以重写的时刻，与 UMG 中一样：

| 事件 | 时机 |
| --- | --- |
| `PreConstruct(bIsDesignTime)` | 在 Initialize 的开头，设计器预览和运行时都是如此 |
| `OnInitialized` | 内容构建完成后（C++ 中为 `NativeOnInitialized`）；在预览中也是如此 |
| `OnConstruct` | 开始播放：树已注册并显示在屏幕上；仅限游戏世界 |
| `OnDestruct` | 在拆除时 |
| `OnTick` | 仅在开启 `bWantsTick` 时每帧执行，而默认情况下并未开启——从未使用的 tick 仍需付出管理器列表条目、虚函数调用和 ProcessEvent 的代价 |

指针、拖拽、按键、焦点和导航事件（`OnPointerClick`、`OnDrop`、`OnKeyDown`、`OnFocusReceived`、
`OnNavigate`……）也在这里。`bAllowEventBubbleUp` 默认开启：用户控件是一个容器，而一个
静默吞掉内部每次点击的容器会破坏它所放置的每个屏幕；将其关闭以在此边界处消费事件。
`bCanNavigateHere` 默认关闭，因此导航搜索会像以往一样越过该控件；
当你想要 `OnNavigate` 时将其开启。

<a id="subclassing-and-nesting"></a>

## 子类化与嵌套

- **子类化。** 只添加逻辑的子类不声明自己的树，而是实例化其父级的树
  （`FindWidgetTreeArchetype` 查找最近的拥有树的类）——否则为了更改一个函数而对屏幕进行子类化
  会静默地产生一个空屏幕。属性绑定和事件路由沿链**累积**：未添加任何绑定的子类
  仍然遵循其父级的绑定，而添加了一些绑定的子类也不会替换它们。
- **嵌套。** 将一个类拖入另一棵树（或在 `.dui` 中将其写为节点类型），设计器会将那个
  实例折叠为一行——这适用于按钮或滑块这类成品控件。要让宿主能在其中放入内容，
  该类必须有意识地开一个洞：命名槽。

<a id="named-slots"></a>

## 命名槽

`UDreamNamedSlot` 是一种行为，它将其控件标记为一个**洞**，由放置该类的宿主来填充：类
说明位置，宿主说明内容。

- 槽的名称就是控件的显示名称——作者已经输入并在层级中看到的内容，与 UMG 为
  `UNamedSlot` 所做的选择相同。
- 默认情况下接收一个控件（类似 `UDreamContentWidget`：接收多个的洞会变成面板）。当该洞
  本身已经是面板时，开启 `bAcceptsSeveral`。
- `bIsDefaultSlot` 标记默认槽，未指定槽名称的嵌套内容会去那里。每个类一个（DUI3022）。

宿主放入槽中的是**宿主自己树中的控件**，而不是针对另一个资产记录的差异；在
Initialize 结束时，它们会被挂到匹配的槽下。相关调用位于 `UDreamUserWidget` 上：`GetContentForNamedSlot`、
`SetContentForNamedSlot`、`FindSlotWidget`、`GetDefaultSlotName`（C++ 类可以重写它）和 `GetNativeSlotNames`
（内容由代码而非模板构建的类如何声明其洞）。

还有 `UDreamNamedSlotHost`，名称相似但作用不同：它是 UMG 的
`INamedSlotInterface` 的对应物，是**一棵树内部**的运行时名称到子项映射（`SetContentForSlot`、`GetContentForSlot`、
`ClearSlot`、`GetSlotNames`），不涉及其他资产。不要将其与类之间的命名槽混淆。

在 `.dui` 中，组件用 `slot` 开一个洞，并声明宿主可以设置什么，用 `props`；宿主在实例中嵌套内容
以填充默认槽：

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

完整语法见 [`.dui` 组件](../language/components.md)。

<a id="binding-to-children-by-name"></a>

## 按名称绑定到子项

树中每个带 id 的控件都会成为该类的一个成员变量，图表可以直接读取它。在 C++ 父类中声明的控件类型
属性会绑定到树中**同名**的控件：`InitializeWidgetStatic` 绑定
在 `UDreamUserWidget` 之下声明的每个控件类型属性，无论是否标记。该标记决定的是另一件事——
`meta = (BindDreamWidget)` 使缺失的控件成为编译错误：

```cpp
UCLASS()
class UMySettingsScreen : public UDreamUserWidget
{
    GENERATED_BODY()

protected:
    // The tree must contain a widget named Confirm, or the compile fails
    UPROPERTY(meta = (BindDreamWidget))
    TObjectPtr<UDreamWidget> Confirm;
};
```

它有意不采用 UMG 的 `BindWidget`。两者在运行时从不冲突，但头文件是给人读的，一个裸的
`meta=(BindWidget)` 加在 Dream 控件上会表明它属于一个它并不属于的框架。动画方面的对应物是
`BindDreamWidgetAnim`，以及不带错误的 `BindDreamWidgetAnimOptional`。在运行时，`GetWidgetFromName` 按其变量名查找
控件。

事件通过**路由**处理：`OnClick -> HandleConfirm` 将一个事件连接到用户控件上的一个函数，这也正是 UMG 放置事件处理的地方。编译器将每条路由解析为 `UDreamWidgetBlueprint::EventBindings`，而
`UDreamUserWidget::BindEventBindings` 在 Initialize 时附加它们。在设计器中，同样的事情是细节面板的 *Events* 部分：
`+` 会创建一个具有正确签名的自定义事件以及命名它的路由。

<a id="the-designer-edits-the-class"></a>

## 设计器编辑类

设计器编辑类的预览实例并将更改写回**类**，因此没有应用步骤，也没有
需要协调的每实例覆盖列表。预览宿主是瞬态且非事务性的；编写树是
唯一可撤销的部分。设计器本身在[设计器](../tools/designer.md)中描述。

**注意：**

此插件所衍生的预制体资产模型已经消失，连同 `SavePrefab`、`Apply`、`ClearLoadedPrefab` 和
  *Save on Apply* 一起。插件不附带重定向：针对旧类名保存的资产需用 1.0.0 重新保存一次，
  借用 2.1.0 的重定向块；请参阅[迁移](../guides/migration.md)。

<a id="dui-a-blueprints-source-file"></a>

## `.dui`：蓝图的源文件

一棵树也可以写成文本。`UDreamTextUserWidget` 是一个用户控件，其层级来自 `.dui` 文件：它的
`SourceFile` 指定文件名，编译时 `FDreamWidgetBlueprintCompilerContext` 读取该文件、构建树，
并将其安装为蓝图的编写层级 —— 在类声明其变量之前。从那时起，它在每个方面都是一个
普通的 `UDreamUserWidget`：相同的生成类、每个控件相同的成员变量、相同的
属性绑定、相同的设计器。

- 文本替代的是**编写**步骤，而非运行时间。
- `SourceFile` 只能在类默认值上编辑（`EditDefaultsOnly`）：一个类，一棵树。文件在
  编译时读取，这是唯一能为每个控件声明成员变量、解析绑定、检查绑定并将
  层级交给设计器绘制的时刻。
- 它是仅编辑器数据，在烘焙时剥离；相对路径以 `DUI/` 源目录为根。
- 设计器工具栏中的 *Set Source File...* 可将类指向某个文件，在设计器中所做的编辑会写回
  文件中保存该值的那一行。

如何编写：[`.dui` 概览](../language/overview.md)。

<a id="showing-it"></a>

## 显示它

| 调用 | 作用 |
| --- | --- |
| **Create Dream Widget** 节点 | UMG 的 Create Widget 节点：选择一个类会为每个 Expose on Spawn 属性生成一个引脚（`.dui` 的每个 `props` 和 `viewmodels` 条目各一个），以及一个 Owning Player 引脚，并将结果类型设为此类。这些引脚在控件初始化**之前**赋值，因此 Pre Construct、On Initialized、On Construct 以及每个绑定的初始值都能看到它们；UMG 在 Create 之后赋值，对 On Initialized 来说太晚了 |
| `UDreamUserWidget::AddToViewport` | UMG 的 Add to Viewport：显示在拥有者玩家的屏幕上（未指定时为第一个本地玩家），并开启显示。与 UMG 一样，已有父级的控件会被拒绝。`RemoveFromParent` 会将其再次移除 |
| `UDreamUserWidget::AddToPlayerScreen` | 同上，但没有拥有者玩家时拒绝，而不是回退到屏幕 0 |
| `UDreamUIBPLibrary::AddWidgetOfClassToViewport` | 一次调用即可创建并放入视口；返回引脚采用该类的形状 |
| `UDreamUIBPLibrary::CreateDreamWidgetOfClass` | 实例化它但不放置在任何地方（与 `ConstructWidget` 相同的未上屏状态）。与 UMG 的 `Create` 一样，不在蓝图菜单中 —— Create Dream Widget 节点代表它；已经调用它的图表仍可编译 |
| `BeginCreateDreamWidget` / `FinishCreateDreamWidget` | C++：该节点的两部分。在初始化之前，于两者之间写入新控件自身的属性（例如 `SetViewModel`） |
| `UDreamScreenUISubsystem::CreateWidgetOnScreen` / `AddToPlayerScreen` | 指定玩家的屏幕 |
| `UDreamWorldWidgetComponent::SetWidgetClass` | 放入关卡；请参阅[世界空间 UI](world-space.md) |

该类的成员与 UMG 的 `UUserWidget` 逐项对比，见
[`UDreamUserWidget` 参考](../Reference/DreamUserWidget.md)。
