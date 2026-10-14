---
title: "控件库"
description: "DreamGUIControls 中有什么——按类别划分的 Dream* 控件、它们所基于的 UI* 行为、操作栏、UMG 互操作，以及如何从 .dui 放置控件。"
---

控件库位于 `DreamGUIControls` 模块中，在输入系统之上（L3）。它提供两层：

- **`Dream*` 控件**：按钮、开关、滑条、列表、对话框……每一个都是
  `UDreamUserWidget` 的子类，其**层级由代码构建**，而非从资产实例化。
- **`UI*` 行为**：诸如 `UUIButton`、`UUIToggle` 和 `UUISlider` 这样的组件，你将它们挂到
  一个普通 widget 上，让它像按钮一样响应。控件就是由它们组装而成的。

使用这些类型的 C++ 会在其 `Build.cs` 中加入 `DreamGUIControls`。资产则无需任何操作。

<a id="why-a-controls-hierarchy-is-code"></a>

## 为什么控件的层级是代码

插件的 `Content/Controls/` 中仍然保留着一组预制蓝图（`BP_Button`、`BP_Toggle`、
`BP_Dropdown`……）——这是更早的做法，即控件是一棵可以打开并编辑的资产树。它有什么
问题，在 `UDreamButton` 的头文件里说得很直白：`BP_Button` 发布数月却没有
`UIButton`，所以点击它毫无反应，而且没有任何地方报告这一点。

由代码构建的控件没有半成品状态。`UDreamUIControl` 在 `NativeOnInitialized` 中按固定顺序依次运行
树、部件、行为和外观，而行为始终是控件自己添加的。代价是这棵树没人能打开，所以
**作者原本要伸手进层级里去改的一切都必须成为属性**——这正是每个控件的 `F…Style`
结构体；参见[样式与样式表](styles.md)。

<a id="the-controls"></a>

## 控件

`.dui` 标签一列是你在 `.dui` 中写的节点类型；类名链接到其生成的
参考页。

<a id="buttons-and-choices"></a>

### 按钮与选择

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamButton`](../Reference/DreamButton.md) | `Native.Button` | 一个面，面上有一个洞。**它自己不绘制任何文字**：按钮上显示什么，取决于宿主嵌进洞里的内容 |
| [`UDreamToggle`](../Reference/DreamToggle.md) | `Native.Toggle` | 复选框：一个框和一个勾。`CheckedState` 有 UMG 的第三个值；`bIsOn` 是它的布尔写法 |
| [`UDreamRadioButton`](../Reference/DreamRadioButton.md) | `Native.RadioButton` | 开关的结构，单选的行为 |
| [`UDreamInputKeySelector`](../Reference/DreamInputKeySelector.md) | `Native.InputKeySelector` | 一个标签为绑定按键的按钮，点击后会监听下一个按键 |

<a id="values-and-progress"></a>

### 数值与进度

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamSlider`](../Reference/DreamSlider.md) | `Native.Slider` | 一条轨道、一个填充和一个把手；横向或纵向只是一个属性 `Direction`，不是两个资产 |
| [`UDreamSpinBox`](../Reference/DreamSpinBox.md) | `Native.SpinBox` | `[-] value [+]`，一行三段：一个数值、一个范围、一个步长 |
| [`UDreamProgressBar`](../Reference/DreamProgressBar.md) | `Native.ProgressBar` | 一条轨道和一个填充，没有行为 |
| [`UDreamThrobber`](../Reference/DreamThrobber.md) | `Native.Throbber` | N 个片段脉动，只表达"有事在发生"。UMG 的 `Throbber` 和 `CircularThrobber` 在这里是一个类、调色板里的两行 |

<a id="text"></a>

### 文本

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamTextInput`](../Reference/DreamTextInput.md) | `Native.TextInput` | 带框的文本字段；`bMultiLine = true` 是多行版本。UMG 的 `EditableTextBox` / `MultiLineEditableTextBox` |
| [`UDreamEditableText`](../Reference/DreamEditableText.md) | `Native.EditableText` | 不带框的输入框，UMG 的 `EditableText` |
| [`UDreamMultiLineEditableText`](../Reference/DreamMultiLineEditableText.md) | `Native.MultiLineEditableText` | 不带框的输入框，跨多行 |
| [`UDreamRichTextBlock`](../Reference/DreamRichTextBlock.md) | `Native.RichText` | 读取标记的一段文字，构建于 `UDreamText::bRichText` 之上 |

<a id="lists-and-scrolling"></a>

### 列表与滚动

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamListView`](../Reference/DreamListView.md) | `Native.List` | 在滚动视口中从数据源构建的行——比 UMG 的 `ListView` 更小：没有条目 widget 协议要实现 |
| [`UDreamTileView`](../Reference/DreamTileView.md) | `Native.TileView` | 行既横向又纵向排列的列表 |
| [`UDreamTreeView`](../Reference/DreamTreeView.md) | `Native.TreeView` | 行带缩进和展开箭头的列表 |
| [`UDreamScrollBox`](../Reference/DreamScrollBox.md) | `Native.ScrollBox` | 一个面、一个裁剪视口、在其中滑动的内容栈，以及一个滚动条 |
| [`UDreamScrollBar`](../Reference/DreamScrollBar.md) | `Native.ScrollBar` | 一条轨道和一个把手；横竖同一个控件 |

三种列表都派生自 [`UDreamListViewBase`](../Reference/DreamListViewBase.md)，它持有
视口、滚动条、行模板、行池和选中逻辑——"树就是会缩进的列表"
这句话在类结构上是字面成立的。

<a id="containers-popups-and-navigation"></a>

### 容器、弹层与导航

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamBorder`](../Reference/DreamBorder.md) | `Native.Border` | 一个面、一圈内边距，以及一个放东西的洞 |
| [`UDreamExpandableArea`](../Reference/DreamExpandableArea.md) | `Native.ExpandableArea` | 一个可点击的标题和它收起的内容 |
| [`UDreamTabView`](../Reference/DreamTabView.md) | `Native.TabView` | 一排标签页，加一个页面切换器 |
| [`UDreamDialog`](../Reference/DreamDialog.md) | `Native.Dialog` | 遮罩、居中面板、标题、消息和一排按钮。通过 `UDreamUIModalSubsystem::ShowModal` 打开，遮罩、输入拦截和焦点由子系统负责 |
| [`UDreamMenuAnchor`](../Reference/DreamMenuAnchor.md) | `Native.MenuAnchor` | 菜单从哪里弹出，以及谁负责把它收起来。菜单是每个玩家一层弹出层上的弹层 |
| [`UDreamDropdown`](../Reference/DreamDropdown.md) | `Native.Dropdown` | 一个带标题的面，加一个弹出的列表 |
| [`UDreamRingMenu`](../Reference/DreamRingMenu.md) | `Native.RingMenu` | 围着中心的扇区，按方向选择 |

<a id="interop"></a>

### 互操作

| 控件 | `.dui` 标签 | 是什么 |
| --- | --- | --- |
| [`UDreamNativeWidgetHost`](../Reference/DreamNativeWidgetHost.md) | `Native.NativeWidgetHost` | DreamGUI 层级里的一个洞，由一个 UMG widget 填上。见下文 [UMG 互操作](#umg-interop) |

每个控件的基类是 [`UDreamUIControl`](../Reference/DreamUIControl.md)，它是抽象的，
没有标签。

<a id="placing-a-control-from-dui"></a>

## 在 .dui 里放控件

在 `.dui` 里，控件是由其**注册表标签**作类型的节点：`Scope.Name`，插件的控件
都在 `Native` 下。

```dui
class /Game/UI/WBP_AudioSettings

VerticalBox Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    Spacing = 12
    Padding = (48, 32, 48, 32)

    HorizontalBox {
        Spacing = 8
        Native.Toggle Mute { OnToggleChanged -> HandleMuteChanged }
        Text { Text = "Mute" }
    }
    Native.Slider Volume {
        MinValue = 0
        MaxValue = 1
        Value = 0.8
        OnValueChanged -> HandleVolumeChanged
    }
    HorizontalBox {
        Spacing = 16
        Native.Button Apply {
            Text { Text = "Apply" }
            OnClicked -> HandleApply
        }
        Native.Button Back {
            Text { Text = "Back" }
            OnClicked -> HandleBack
        }
    }
}
```

要点：

- **节点的行设置控件的属性**：`MinValue` 和 `Value` 是 `UDreamSlider` 的，
  命名与 UMG 一致（见[从 UMG 过来](umg-parity.md)）。样式结构体用点路径写：`Style.TickChecked = #FF3355`。
- **嵌套内容进入控件的默认洞。** `Native.Button` 里的 `Text { }` 就是
  按钮的标签——按钮自己不绘制任何文字。开关没有洞；框旁边的文字是你
  自己的布局，所以上面它和一个 `Text` 同处一个 `HorizontalBox` 里。
- **事件用路由接**：`OnClicked -> HandleApply` 把控件的事件接到用户 widget 的同名函数上，
  和接任何其他事件一样。
- 打不出标签的类，可以用类路径作类型：`/Script/DreamGUIControls.DreamDialog Confirm { … }`。
  但写回（designer 编辑文件）总是写标签：类路径带着类所在的模块名，
  用类路径写出的文件，在类换模块的那天就读不回来了。

标签不是内建在语言里的。一个类在自己的 `.cpp` 里用一行把自己登记到注册表：

```cpp
DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
```

项目插件用同一个宏声明自己的作用域（比如 `Game.HealthBar`）；编译器、符号导出、"未知标签"诊断和写回都从同一张表读。节点类型完整的解析顺序见[节点与值](../language/nodes-and-values.md)。

<a id="how-a-control-assembles-itself"></a>

## 一个控件是怎么搭起来的

`UDreamUIControl` 在 `NativeOnInitialized` 里按这个顺序跑四步；子类只通过钩子说"我是什么"：

<a id="the-tree"></a>

### 树
有 `Template` 时实例化它的树（`RealizeTemplate`），否则由代码搭建（`RealizeBuiltIn`）。

<a id="the-parts"></a>

### 部件
按**名字**在树里找部件（`BindParts`），名单来自 `CollectParts` 这一份声明。

<a id="the-behaviours"></a>

### 行为
在找到的部件上挂行为（`WireParts`）。它用"缺了就加"的方式拿组件，所以模板树上作者没挂的 `UUIButton` 也会被补上。

<a id="the-look"></a>

### 外观
解析样式并推到部件上（`ApplyStyle`），之后任何属性变化都会再推一次。

把第 1 步和第 2 步分开，就是 `Template` 能成立的原因。它相当于 WPF 的 ControlTemplate：控件保留行为、状态机和样式约定；模板决定长什么样、有哪些节点。部件按名字匹配，所以一个模板就是"一个里面有 `Face` 和 `Label` 的 widget Blueprint"，不需要继承什么、实现什么接口。模板缺了必需部件时会按名字报一次；`GetUnboundRequiredParts` 返回所有没找到的必需部件，空数组才是健康的。

Blueprint 子类想碰控件的部件，要等 **On Control Ready**（`OnControlReady`），不是 On Initialized：后者在四步之前就触发了，那时部件还不存在。`FindPart` 按显示名在控件自己的内容里找部件。

<a id="the-ui-behaviours"></a>

## UI* 行为

行为是挂在 widget 上的 `UDreamUIBehaviour` 组件，在 `.dui` 里写成 `+ ClassName { }` 块。控件由它们拼成，你也可以直接用它们让一个自己画的 widget 获得交互——`+ UIButton { TransitionType = None }` 在组件文件里很常见。

| 行为 | 模块 | 做什么 |
| --- | --- | --- |
| `UUISelectable` | `DreamGUIInput` | 所有可交互行为的基类：普通、悬停、按下、禁用、聚焦五种状态和它们之间的过渡，导航，声音 |
| `UUIButton` | `DreamGUIControls` | 点击 |
| `UUIToggle`、`UUIToggleGroup` | `DreamGUIControls` | 开关；一组开关互斥 |
| `UUISlider`、`UUIScrollbar` | `DreamGUIControls` | 拖动取值 |
| `UUIScrollView`、`UUIScrollViewWithScrollbar`、`UUIRecyclableScrollView` | `DreamGUIControls` | 滚动视图；带滚动条的；回收复用格子的 |
| `UUIListView`、`UUITileView`、`UUITreeView`、`UUIListEntry` | `DreamGUIControls` | 回收式列表，以及每一行身上的条目行为 |
| `UUIDropdown` | `DreamGUIControls` | 下拉框 |
| `UUITextInput` | `DreamGUIControls` | 文本编辑：光标、选区、IME、虚拟键盘 |
| `UUITextHyperlink` | `DreamGUIControls` | 文字里的超链接 |
| `UUIProgressBar` | `DreamGUIControls` | 进度条 |

`UUISelectable` 在输入层而不是控件层，因为导航和焦点需要它；控件层的行为都派生自它或 `UDreamUIBehaviour`。设计器的调色板把这些行为放在一个 legacy 分类里，正常情况下应该用对应的控件。

<a id="the-action-bar"></a>

## 操作提示栏

`UDreamUIActionBar` 是屏幕底部那一排 "A：确认　B：返回"。它**读动作路由器（action router）**，而不是由谁告诉它显示什么，所以不会和按键的真实作用脱节——这是手写提示栏的通病：屏幕改了绑定，下面的提示还在宣传旧的。

| 属性 | 含义 |
| --- | --- |
| `EntryClass` | 每条提示实例化一次的 widget 类，作为提示栏的子节点。没有它什么都不显示 |
| `UserIndex` | 谁的提示。`-1`（默认）是拥有这个 widget 的玩家 |
| `MaxEntries` | 最多几条（默认 8），防止失控的表格铺满屏幕 |

它只在答案变了时重建：绑定增减、玩家换了设备、换了另一种手柄（图标按手柄型号区分）。屏幕自己的动作之后，如果玩家有一个肩键能切换的标签页视图，还会追加"上一页 / 下一页"两条提示，按键取自项目设置的 `PreviousTabKeys` 和 `NextTabKeys`。`GetPrompts` 返回上次重建放上去的提示列表，不需要条目 widget 就能读。

每条提示的根上挂 `UDreamUIActionBarEntry`，指向画提示的两三个 widget：`LabelText`（动作说明）、`IconImage`（按键图标，当前设备没有图标时隐藏）、`KeyText`（没有图标时显示按键名）。更复杂的画法覆盖 Blueprint 事件 `OnBindingChanged`；长按动作的进度通过 `OnHoldProgressChanged` 推送，用来画填充的圆环。

<a id="umg-interop"></a>

## UMG 互操作

<a id="umg-inside-dreamgui"></a>

### UMG 放进 DreamGUI

`Native.NativeWidgetHost`（`UDreamNativeWidgetHost`）是 DreamGUI 层级里的一个洞，由一个 UMG widget 填上。它建立在早就存在的两个组件上：`UDreamUMGWidget`（`.dui` 里的视觉标签 `UMGWidget`）把一个 `UUserWidget` 渲染到渲染目标，再当作 DreamGUI 自己的几何体画出来；`UDreamUMGWidgetInteraction` 把指针事件转发进去。控件把这两件作者原本要自己加、自己接、自己定尺寸的事包成一个标签。

| 属性 | 含义 |
| --- | --- |
| `WidgetClass` | 要实例化并绘制的 UMG widget 类；为空就是一个空洞 |
| `ResolutionScale` | 一个本地单位值多少渲染目标像素，1 是像素对像素。这是保真度和内存的取舍，所以不在样式里 |
| `BackgroundColor` | 被托管的 widget 没画的地方透出什么，默认透明 |

它**不是**把 UMG 布局和 DreamGUI 布局混在一起的办法：被托管的 widget 按这个控件的尺寸画进一张纹理，对周围一无所知，既推不动 DreamGUI 的布局，DreamGUI 也伸不进它里面。这和 UMG 自己的世界空间 `WidgetComponent` 是同一笔交易。它还**需要一个世界**：没有世界时（无头测试、设计器里不建世界的部分），节点存在、有尺寸、有位置，只是什么都不画。

交互方面，表面上的每个 DreamGUI 指针都作为一个独立指针转发：鼠标作为 Slate 的光标，每根手指作为一个触点（UMG 的 `ScrollBox` 就是靠触点平移的）。多个 `UDreamUMGWidgetInteraction` 可以共用一个 `VirtualUserIndex`；同一时间只有一个能占用那个模拟光标，由每个世界一个的 `UDreamUMGWidgetInteractionManager` 仲裁。

<a id="umg-over-dreamgui"></a>

### UMG 叠在 DreamGUI 上

另一个方向是两套 UI 共用同一个视口。打开 Slate 输入源（*Use Slate Input
Source*）后，指针落在绘制于视口上层的 UMG 控件上时，以及某个 UMG 控件持有键盘焦点时的按键输入，都不归 DreamGUI 管。当玩家拥有 DreamGUI 焦点或 Tab 停靠点时，
`UDreamGameViewportClient` 会从光秃秃的视口接管 Slate 自身的导航（Tab、方向键），这样 Slate
就不会把焦点移进 UMG 层。输入的完整说明见[输入](../concepts/input.md)。

<a id="samples"></a>

## 示例

插件自带一个能直接运行的最小屏幕：`Content/Samples/HelloDreamGUI.dui`；用法见[你的第一个屏幕](../getting-started/first-screen.md)。

`DreamGUISamples` 模块（L4，位于控件之上）放着两块展示屏幕的**原生基类**：

| 类 | 用途 |
| --- | --- |
| `UDreamUIShowcasePanel` | 媒体控制台展示屏幕的基类：其 `.dui` 绑定到的所有变量和函数——用于 `<->` 绑定的 `bMuted`，用于表达式的 `IsMuted()` / `GetFade()`，用于 `each` 的 `Tracks` 和 `GetHistory()` |
| `UDreamUIControlsGalleryPanel` | 控件画廊的基类：每类输入控件一个处理函数，每次交互都会向绑定实时显示的事件日志追加一行——覆盖点击、开关、拖动、滚动、文本和选择 |
| `UDreamUIShowcaseDialog` | 展示屏幕的对话框按钮所打开的 `Native.Dialog` 子类 |
| `UDreamUIShowcaseTrack` | `each` 迭代的数据行 |

这两块屏幕的 `.dui` 文件和控件蓝图属于使用该插件的项目，不随插件发布；`DreamGUISamples` 只提供它们背后的 C++。
