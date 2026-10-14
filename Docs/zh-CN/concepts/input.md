---
title: "输入"
description: "输入系统 —— 事件系统与预设 Actor、射线检测器与输入模块、每个玩家各自的指针、焦点与文本目标、动作路由、导航（Tab、导航作用域、焦点外观、平台的确认与返回、肩键切换标签）、弹出层、模态、提示与拖放、文本输入与键盘布局，以及 Slate 输入源。"
---

输入位于核心之上的 `DreamGUIInput` 模块中。首先要知道的一点：**输入是按玩家独立的**。每个本地玩家都有自己的指针、焦点和文本目标，因此分屏中的第二个玩家会自行悬停、聚焦和输入。

<a id="the-pieces"></a>

## 组成部分

| 部分 | 类 | 作用 |
| --- | --- | --- |
| 输入子系统 | `UDreamUIInputSubsystem` | 每个世界一个。拥有每个本地玩家的输入（`UDreamUIInputUser`），并在 `TG_PostPhysics` 中、暂停时也按玩家顺序运行每个玩家的一帧 —— 在所有玩家控制器于 `TG_PrePhysics` 中运行完输入之后、UI 管理器布局与绘制之前 |
| 事件系统 | `UDreamEventSystem` | 关卡放置、蓝图可获取的"一个玩家的输入"：其细节面板即该玩家的设置，其函数读写该玩家的输入，其事件转发该玩家的事件 |
| 预设 Actor | `ADreamEventSystemActor`、`ADreamStandaloneInputEventSystemActor`、`ADreamEnhancedInputEventSystemActor` | 携带事件系统与输入绑定；未放置时由 `UDreamGUISettings::EventSystemActorClass` 生成 |
| 输入模块 | `UDreamPointerInputModule`、`UDreamStandaloneInputModule` | 把按键、指针和导航变成控件上的事件 |
| 射线检测器 | `UDreamScreenSpaceRaycaster`、`UDreamWorldSpaceRaycaster` | 屏幕空间与世界空间的命中测试，每个玩家一个（世界空间见[世界空间 UI](world-space.md)） |
| 动作路由 | `UDreamUIActionRouter` | 哪个界面听哪个键 |
| 导航栈 | `UDreamUINavigationStack` | 导航作用域的栈 |
| 弹出层、模态、提示、拖放、虚拟光标 | `UDreamUIPopupLayer`、`UDreamUIModalSubsystem`、`UDreamUITooltipSubsystem`、`UDreamUIDragDropSubsystem`、`UDreamUIVirtualCursorSubsystem` | 见下文 |

这些全都按需创建：屏幕根、射线检测器和事件系统在第一次需要时就会出现，无需在关卡中放置任何东西。

<a id="pointers-focus-and-events"></a>

## 指针、焦点与事件

- **指针 id 有分段。**鼠标（以及替代它的虚拟光标）是 `0`，手指是 `100` 加其序号，脚本自编的 id 从 `1000` 开始。以前第一根手指和鼠标是同一个指针，会争抢悬停与选中；要按手指序号读取触摸指针，用 `DreamUIPointerIds::ForTouch`。
- **一次点击**需要在按下的控件上抬起；抬起会发给接住按下的那个控件。
- **冒泡**：事件从命中的控件沿父链向上，直到某个处理者返回 false。`UDreamUserWidget` 上的 `bAllowEventBubbleUp` 默认开启。
- **焦点**：`SetFocus`、`SetKeyboardFocus`、`SetUserFocus`、`ClearKeyboardFocus`、`HasKeyboardFocus`、`HasUserFocus` 和 `HasFocusedDescendants` 都按控件**所属玩家**来询问（`GetOwningPlayer`、`GetOwningPlayerIndex`）—— 其上方最近的用户控件拥有它，树中没有用户控件时属于第一个本地玩家。关闭 `bIsFocusable` 会放弃控件持有的任何焦点。
- **暂停**：Enhanced Input 预设按原样绑定它被赋予的动作。游戏暂停且设置了 `UDreamUISettings::bScreenSpaceUIAffectByGamePause` 时，预设会丢弃到达的输入；要让暂停帧中的点击能够到达，其动作必须在暂停时也触发 —— 插件自带的 `IA_*` 动作会如此；你自己的动作要设置 `bTriggerWhenPaused`。
- **光标**：悬停的控件可以驱动硬件光标（`Cursor`；`Default` 表示"没意见"，让下面的控件决定）；`ResetCursor` 回到没意见。

<a id="raycasters-and-hit-shapes"></a>

## 射线检测器与命中形状

命中测试由「射线检测器」完成：一个基类加四种命中，**命中形状由视觉的 `RaycastType` 决定**（`Rect` / `Mesh` / `VisiblePixel` / `Custom`）。

⚠️ **不要猜测它们各自的模块**：基类 `UDreamBaseRaycaster`、扇形 `UDreamRingSectorRaycast` 和自定义 `UDreamVisualCustomRaycast` 在**核心 `DreamGUI` 模块**；屏幕空间与世界空间的射线检测器在 `DreamGUIInput`；可见像素子类在 `DreamGUIExtensions`。

**基类**（一个 `UCLASS(Abstract)` `UActorComponent`）把命中测试拆成三个纯虚函数：`GenerateRay`（造射线）、`Raycast`（命中测试）和 `ShouldStartDrag`（这次按下是否成为拖拽）；外加 `IsWithinDoubleClickDistance`（默认 `true`；屏幕和世界射线检测器按拖拽阈值来衡量它）。它带有 `UserIndex`（哪个玩家）和 `PointerID`（`-1` 对每个指针生效）。基类分三个阶段运行命中测试：`GatherRaycastCandidates`（粗筛，**可在线程上运行**）→ `PrepareRaycastTree`（游戏线程；合并并求解这棵树）→ `RaycastCandidates`（精确测试，**按绘制顺序**排序）。

**屏幕空间射线检测器**的默认值：

| 属性 | 默认 |
| --- | --- |
| `RayLength` | 100000 |
| `DragThreshold` | 5（**以目标的局部空间衡量**） |
| `bHoldToDrag` / `HoldToDragTime` | false / 0.5 |

⭐ "移动够远算拖拽"与"离太远不算双击"**是同一个距离**。

**世界空间射线检测器是一个玩家一个，而不是一块面板一个** —— 射线从该玩家的视角瞄准，能命中哪些画布由管理器回答（``TraceChannel`` 匹配的世界空间根），所以**它从不需要被告知面板的增删或移动**。`PointerSource` 设定瞄准点（`Mouse` / `ScreenCenter`，后者是第一人称准星）；`bOccludeByWorld` 默认开启，**最近的阻挡命中既是遮挡物也是目标**。

**扇形射线检测器**（`UDreamRingSectorRaycast`）命中一个**环形扇区**，而不是它被画进去的矩形 —— 环形菜单的每个楔形都是整环大小的方块，在默认 `Rect` 命中下**最上面那个会替整个圆回答**。角度**从十二点顺时针**；`OuterRadius` 为 `0` 表示**无界**（武器轮盘正是如此）。

**可见像素**（`UDreamVisualCustomRaycast_VisiblePixel`）按命中点下的像素决定是否命中（`VisibilityThreshold` 默认 `0.1`，`PixelChannel` 默认 `3`）。⚠️ 它只支持**能从贴图读取像素**的视觉 —— `Texture2D` 上的 `UITexture` 完美；**打包进动态图集的 `UISprite` 不行**；**动态字体上的 `UIText` 不行**（插件内建字体都是动态的）。不支持时会退回几何命中。

<a id="event-data"></a>

## 事件数据

四类都是 `UObject`（蓝图类型），都派生自 `UDreamBaseEventData`：

| 类 | 回答 |
| --- | --- |
| `UDreamBaseEventData` | 基类（`SelectedComponent`、`EventType` —— 后者是一个 `EDreamUIPointerEventType`） |
| `UDreamPointerEventData` | **在哪** —— 指针与按下位置、进入与悬停栈、世界点与法线、滚轮轴、拖拽状态，以及三个时间戳和双击计数 |
| `UDreamKeyEventData` | **哪个键、什么和弦、谁的** —— `KeyEventType`、`Key`、四个和弦位、`AnalogValue`、`bHandled`、`FocusedWidget` |
| `UDreamGestureEventData` | **手势得出的结论** —— `GestureType`（`Pinch` / `Swipe`）及其分量 |

⭐ **三个时间戳都在"指针时钟"上** —— 世界的真实时间，**不因暂停而停止、不随时间膨胀而拉伸**。UI 在游戏暂停时仍须工作：暂停菜单里的按住仍是长按，一秒内的两次点击仍是双击。要对比 `GetPointerClockSeconds`，**绝不要用游戏时钟**。

**`UDreamKeyEventData` 的冒泡开关叫 `bHandled`，而不是 `EventType`**：基类已经有一个 `EventType`
装着指针词汇，UHT 不允许遮蔽；而且形状也不对 —— 指针事件问的是「能不能继续
冒泡」，按键问的是「有没有人接住」。

**手势不是指针**：它没有按下控件、没有拖拽、没有悬停 —— 它是从**已经派发过自己的事件的指针**中得出的结论，但仍走同一条派发路径。

<a id="the-action-router"></a>

## 动作路由

以前「哪个界面听哪个键」是输入 Actor 的 cpp 里一个静态的 `FKey` 数组：项目没法加动作、改绑定、为它画提示，除非改插件。
现在绑定随注册它的界面生灭，**只有最上面的界面**拿到按键 —— 这样对话框打开时，Delete 指的是对话框的删除而不是列表的。

- `UDreamUIActionRouter::RegisterAction(Scope, Action, Callback, UserIndex, bDisplayInActionBar)` 注册一个，动作是一个数据表行；`UnregisterAction` 注销它；`GetDisplayBindings` 为提示栏列出它们，已经
  按当前设备解析好；`GetHoldProgress` 读取按住进度。
- 路由器自己 tick，游戏暂停时也 tick：按住确认（「按住退出」「按住重开」）就在暂停菜单上，按住要在时间到时触发，而不是玩家松手时。
- `UDreamUIActionTrigger` 行为放在按钮旁边，表示「这个按钮也就是 Confirm 做的事」（CommonUI 的
  `TriggeringInputAction`），所以界面的确认键和确认按钮成了一回事，不会各走各的。它是
  行为而不是按钮上的字段，所以对任何控件都有效。
- 控件库里的 `UDreamUIActionBar` 是屏幕底部那一排「A：确认  B：返回」提示。它
  读路由器而不是被告知显示什么，所以不会和按键实际做的事脱节；它
  只在绑定增减、玩家换设备、或换了手柄型号时重建。
- 绑定先于一切拿到每个按键。

<a id="navigation"></a>

## 导航

<a id="tab-and-shifttab"></a>

### Tab 和 Shift+Tab

不按 Ctrl、Alt、Cmd 时，Tab 和 Shift+Tab 按**层级顺序**走（深度优先，兄弟按 `TabIndex`）。每个控件上的三个属性塑造这条路：

| 属性 | 作用 |
| --- | --- |
| `bIsTabStop` | Tab 是否停在这里（前提是能聚焦、能导航到）。关掉就只留给方向键、手柄、指针和代码 —— 浏览器的 `tabindex="-1"` |
| `TabIndex` | 在兄弟之间的顺序：小的在前，相同的按层级顺序；只和兄弟比较 |
| `TabNavigation` | 里面的停靠点怎么参与：`Continue`（就地走完、继续往后）、`Cycle`（在里面转圈）、`Contained`（到头停住）、`Once`（整体是一个停靠点）、`None`（里面都不是停靠点） |

列表、平铺和树视图是 `Once`：Tab 从选中的那一行进去（自己写的容器覆写
`ResolveTabEntry` 决定从哪进），下一次 Tab 离开。滚出视野的停靠点会被滚进来；
提示框、弹出面板和滚动条从来不是停靠点。

Tab 被关在里面的情况：模态、调暗了背景的对话框、以及循环的弹出层；另一个玩家的弹出层永远不是停靠点。
Tab 提交文本框的编辑并移到下一个，通过 Tab 到达的文本框开始编辑（`bTabStartsTextEdit`）；下拉框在 Tab 时提交
高亮的那一行（`bTabCommitsHighlightedRow`），菜单整条链关闭。

项目设置（*Project Settings > Plugins > Dream GUI*）：`bTabNavigation`、`TabOrder`（`Hierarchy`；
`LegacyGeometric` 在这个版本里放回 2.0 的几何顺序）、`bTabWrapsAtScreenEnd`、`bTabStartsTextEdit`。当
玩家有 DreamGUI 焦点或 Tab 停靠点时，Slate 从裸的游戏视口发起的导航（Tab、方向键）由
DreamGUI 接管，所以不会把焦点移出视口、移进 UMG。

<a id="arrows-the-pad-and-navigation-areas"></a>

### 方向键、手柄与导航区域

方向导航按 `DirectionKeys` 表移动焦点。每个控件可以有自己的方向规则
（`UDreamWidgetNavigation`，用 `GetOrCreateNavigation` 拿到：`SetRule`、`SetExplicitTarget`、`SetCustomDelegate`、
`SetAllNavigationRules`），对应 UMG 的 Navigation。`FDreamUINavigationScroll` 管另一件事：方向键把焦点移到
**滚出视野**的控件时，**把它滚进来**。

`bRestrictNavigationArea` 把导航限制在一个控件的孩子之内，
`NavigationBoundaryRule` 决定走到边界时怎么办：`Stop`、`Wrap`（从另一边接上）、或者 `Escape`（交给
外面那一层回答，像列表把移动交还给页面）。

<a id="navigation-scopes"></a>

### 导航作用域

`UDreamUINavigationScope` 是导航意义上的一个界面、面板或对话框：被推到
前面、拿到焦点、退出时还回去的那个单位。它记住焦点该从哪开始、上次交出控制时
焦点在哪 —— 打开子菜单再回来，焦点回到原处，而不是落到恰好第一个注册的那个可选控件上。栈的顺序是推入顺序，不是控件层级，
所以从页面打开的对话框无论挂在哪里都在页面前面。

作用域有输入模式 `InputMode`（`EDreamUIScopeInputMode`）：

| 值 | 含义 |
| --- | --- |
| `All` | DreamGUI 的内置按键有效（导航、确认、返回、翻页、切标签），游戏听到 DreamGUI 没留下的 |
| `Menu` | 和 `All` 一样，但游戏应当让开 —— 项目的玩法输入问 `UDreamUINavigationStack::GetEffectiveInputMode`，它说 `Menu` 时忽略该玩家 |
| `Game` | 对该玩家关掉内置的导航、确认和返回：HUD 上的按钮不会被十字键走到，也不会被跳跃键按下。控件注册的绑定、指针和打字仍然有效 |

没有作用域时，用项目设置 `InputModeWithoutScope`（默认 `All`）。没有作用域也没有焦点时，
导航键只看该玩家自己的屏幕空间画布。

<a id="keys-act-on-the-focus-the-focus-look-is-for-keys"></a>

### 按键作用于焦点；焦点外观只给按键

- Enter、Space 和手柄的确认键按下的是**有焦点的**控件，而不是鼠标悬停的那个；没有焦点时什么也不按。
- 焦点外观 —— Focused 状态和焦点环 —— 只在焦点是按键或手柄移过来的时候显示，像 CSS 的 `:focus-visible`（`bFocusVisibleOnlyFromKeys`）。鼠标点击不再显示它，鼠标悬停也不再把焦点环拿走。
- 焦点环由项目设置 `NavigationSelectionClass` 指定的控件类画，并贴合它标记的控件；不设就没有焦点环。外观自己就能表示焦点的控件用
  `UUISelectable::bUseFocusRing` 拒绝它（运行时 `SetUseFocusRing`）。
- 确认键和返回键用**平台的**，在运行时读取（`bUsePlatformAcceptBack`；Switch 上两者对调）。
  按键表都是项目设置，改了下一个按键就生效：`ConfirmKeys`、`BackKeys`、`DirectionKeys`、
  `PageKeys`、`ExtentKeys`、`PreviousTabKeys`、`NextTabKeys`。
- 扳机键翻页；**肩键切换**当前标签视图的标签，跳过禁用的、两端循环，提示栏显示“上一个标签”“下一个标签”。自己写的控件实现
  `IDreamUITabSwitchTarget` 就能响应肩键。
- 在 DreamGUI 的 UI-only 输入模式（`UDreamUIInputModeLibrary::SetInputModeUIOnly`）下，玩家用手柄时光标隐藏，回到鼠标或键盘时出现
  （`bHideCursorOnGamepad`）；别的模式下 DreamGUI 不碰光标。
  `bAutoVirtualCursorOnGamepad`（默认关）在物理设备是手柄时显示一个虚拟光标。

<a id="popups-modals-tooltips-drag-and-drop"></a>

## 弹出层、模态、提示和拖放

**弹出层**（`UDreamUIPopupLayer`）每个玩家一层。弹出的东西 —— 下拉列表、菜单 —— 还是它原来那个控件；
弹出层把它抬到玩家的屏幕根下、保持世界位置不变，这样它不会被祖先的边界裁掉，也不算进祖先的布局，就像 UMG 的下拉列表住在 Slate 的菜单栈里。
弹出层负责外部点击、焦点、菜单嵌套，以及打开期间跟随移动的锚点。Back 只关最上面那个，关闭后焦点还给打开它的人。弹出层说明 Tab 在它里面做什么（`TabBehavior`）：
`Cycle` 在里面转圈；`CloseAndContinue` 关掉它并从打开者那里继续走（下拉列表和菜单）。在一个打开的弹出层**前面**画着的层上按一下，
会关掉弹出层，而且按下仍然到达那一层。

⚠️ 「被抬到屏幕根下」背后是**两套 API**，别混：

| | 做什么 |
| --- | --- |
| **`Elevate` / `Restore`** | **只管搬**：把控件从原父级挪到屏幕根（世界位置逐像素保住），再搬回原处 |
| **`Push` / `Pop`** | 在搬的基础上**再加四样**：焦点归还（`FDreamFocusReturn`）、点外部关闭、Back、菜单嵌套，以及**每帧布局后跟随开者** |

`FDreamPopupParams` 是 `Push` 的参数：

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `Popup` / `Opener` | 空 | 弹出的控件，与开它的那个（焦点回哪、跟随谁、谁是父） |
| `UserIndex` | `0` | 属于哪个玩家 |
| `OutsideClick` | `Consume` | 见下 |
| `bFocusOnOpen` / `InitialFocus` | true / 空 | 一开就把焦点迁进去（迁到 `InitialFocus` 或第一个可导航控件） |
| `bRestoreOnDismiss` | true | 关时搬回原父级原位；false 让它留在上面演关闭动画 |
| `TabBehavior` | `Cycle` | 上面说的那两个设置 |
| `Place` | 空 | 每帧布局后摆它的委托；不绑就保持相对开者的偏移 |
| `OnDismissed` / `OnClosing` | 空 | 关完之后 / 关之前（**焦点还在里面时**）的回调 |

⚠️ **`Push` 的返回值不能当「确实打开了」用**：`true` 只代表**调用返回时**它是打开的 ——
焦点迁进去时若跑了某个处理器、把弹层又关了，`Push` 也返回 `false`。要记「我开着」，
看弹层自己的 `OnDismissed`。

`EDreamPopupOutsideClick` 有**三档**：`Consume`（关掉它；这按到此为止）、`PassThrough`（关掉它；这按
**继续落到下面** —— Slate 菜单栈的做法）和 `Ignore`（什么都不做；等拥有者 /
Back / 父级来关）。

**关闭原因有八个**（`EDreamPopupDismissReason`，写给拥有者的回调）：`Explicit`、
`OutsideClick`、`Back`、`ParentClosed`、`Replaced`（被同玩家的新顶层弹层 /
遮在它们前面的模态顶掉）、`OpenerLost`（开者被销毁 / 出局 / 失效 / 隐藏 / 不可交互）、
`WorldTeardown`、`Tab`。

另有 `DismissAll(UserIndex, Reason)`（把某玩家所有弹层**自新到旧**全关 —— 模态为它自己的玩家做的正是这件事）与 `GetHomeSlot(Widget)`（给出被抬起的控件
**在家时**那份面板槽的**副本**，**只读** —— 往里写不会传到控件）。

**模态**（`UDreamUIModalSubsystem`）：`ShowModal(DialogClass, OnResult, UserIndex)` 显示一个对话框并等待结果；
`CloseTopModal(Result)`、`CloseAllModals`、`IsModalActive`、`GetModalDepth`。遮罩吃掉所有指针事件（颜色
是 `ModalScrimColor`），作用域把手柄焦点关在里面、事后恢复，Back 意味着“以 Back 结果关闭”。模态**可以嵌套**：一个模态还开着时再 `ShowModal`，
新对话框带着自己的遮罩和作用域盖在上面，关掉它就露出下面那个，仍在等自己的结果。显示模态会先关掉该玩家打开着的弹出层。

⚠️ **`UDreamDialogScope` 与 `ShowModal` 的分工，别叠**：`UDreamDialogScope` 只服务于**另一种
摆法（standalone）** —— 对话框作为普通控件丢进 `.dui`、上方没有子系统层时，它才是让 Back 关对话框的那一层。
**通过 `ShowModal` 打开时，遮罩与 Back 由模态层的作用域负责**；此时再叠一个 `UDreamDialogScope` 会**悄悄改变「Back 用哪个结果回答」**。

三个没写在别处的承诺：**`OnResult` 恰好触发一次**（连“没屏幕根”或“类建不出来”这样的失败也算）；**模态按玩家** —— 分屏里玩家一的“确定吗？”**不遮**玩家二、**不抢**它的焦点、**不被**它的 Back 关掉；以及**为什么要先关弹层** —— 否则留给遮罩后面的弹层会接走模态的**第一下点击**和**第一次 Back**。

**提示**：任何控件都有 `ToolTipText` 和 `ToolTipWidgetClass`（后者优先，是一个**类**：气泡出现时实例化、消失时销毁）。`UDreamUITooltipSubsystem` 找指针下离得最近、提供了提示的祖先；行为可以实现 `IDreamUITooltipSourceInterface` 来提供一个控件。项目设置：
`TooltipDelaySeconds`（0.5）、`TooltipOffset`、`TooltipMaxWidth`、`TooltipFontSize`。

⚠️ **手指永远不唤醒提示**：点一下、按住过了延迟、手指拖到控件上 —— 都**不会**浮出气泡；但手指的**按下仍会收掉已经浮着的气泡**。触屏游戏想在长按时显示提示，得在自己的长按处理器里调 `ShowTooltipFor(Source)`。

⚠️ **它只读不抢**：气泡排在自己的画布上、**整程射线关闭** —— 一个能抢到指针的提示会把自己永久藏起来。

**拖放**：`UDreamUIDragSource` 在拖拽开始时创建一个 `UDreamDragDropOperation`，并把它写到指针的事件数据上；`UDreamUIDropTarget` 从一次放下中读取该操作，按 `RequiredTag` 和载荷类过滤，接受时标记为已处理并广播。被目标拒绝的放下会继续冒泡，所以嵌套目标的行为就像其他任何嵌套的东西一样。没有拖拽源时，拖拽就只是纯几何上的 —— 滚动视图要的正是这个，而背包里的物品则不是。

拖放按**三层**来读就很清晰：

| 层 | 谁 | 管什么 |
| --- | --- | --- |
| **含义** | `UDreamDragDropOperation`（核心 `DreamGUI`） | 这次拖动**是什么**：载荷、标签、拖拽视觉 |
| **管线** | `UDreamUIDragDropSubsystem`（`DreamGUIInput`） | 跟随每个指针的拖动、点亮落点、移动拖拽视觉 |
| **两端** | `UDreamUIDragSource` / `UDreamUIDropTarget` | 谁被拖、哪里能落 |

管线是**按玩家和指针逐个**跟随拖动的 —— **触屏上两根手指拖两个物品是常态**，早期「全场一份」的写法会让第二根手指把第一根的视觉**瞬移**过去。

**列表行版**（`UDreamListRowDragSource` / `UDreamListRowDropTarget` / `UDreamListRowButton`，都在 **`DreamGUIControls`**）是上面两端的**子类，没有自己的检测路径** —— 它们只补上「这是谁的列表的哪一行」；落点还会给出一个**落区**，`EDreamItemDropZone`（`AboveItem` / `OntoItem` / `BelowItem`）。列表有三个开关：`bAllowDragging`（默认 **false**）、`bAllowDragDrop`（默认 **false**，**故意与前一个分开** —— 只出不进的调色板要前者，只进不出的装备栏要后者），以及 `FingerDrag`（`EDreamListFingerDrag`：手指拖动一行是**滚动列表**还是**拾起这一行**）。

<a id="virtual-cursor"></a>

### 虚拟光标

`UDreamUIVirtualCursorSubsystem`（`DreamGUIInput`）为**没有指针的玩家**造一个指针 —— 它解决的是「**四向导航开不了地图、技能树或合成网格**」这件事。

它把玩家的**左摇杆**积分成屏幕位置，通过该玩家输入模块的**覆盖缝**顶替操作系统鼠标，并把**确认键当作左键**。下游的一切 —— 射线、悬停、点击、**拖拽**、提示 —— **都分不出区别**，这正是整个设计：按住确认推摇杆，也走通了鼠标的拖拽路径（比如滑条的把手）。每个玩家一个光标，各由自己的摇杆驱动、画在自己的屏上。

从需要的屏幕调 `ActivateVirtualCursor` / `DeactivateVirtualCursor`（以及它们的 `ForUser` 版本），或打开项目设置 `bAutoVirtualCursorOnGamepad`（默认关）让它跟随物理设备。

<a id="text-input-and-keyboard-layouts"></a>

## 文本输入与键盘布局

DreamGUI 的文本框不是 Slate 控件，所以从来不在键盘焦点路径上；而引擎在游戏里交出平台**字符**的唯一地方是虚函数 `UGameViewportClient::InputChar`，它没有可以订阅的委托。没有人接管这个函数时，文本框就退回自己的 `FKey` 到字符的表，而那张表只在**美式 QWERTY** 上正确：AZERTY、QWERTZ、Dvorak、西里尔、死键和 AltGr 都会打错字符（IME 用户不受影响；组字文本走 TSF）。文本框第一次被编辑时，如果两种办法都没用上，会记一条警告。

最简单的办法是用插件的视口客户端：

```ini
[/Script/Engine.Engine]
GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
```

要保留自己的视口客户端，就让它派生自 `UDreamGameViewportClient`，或者在它的 `InputChar` 里、控制台之后、基类之前调用 `DreamUITextInputRouter::RouteViewportCharacter`。代码见 [安装](../getting-started/installation.md)。

<a id="the-slate-input-source"></a>

## Slate 输入源

默认情况下，DreamGUI 通过预设事件系统 Actor 在玩家控制器上的绑定听输入，所以在引擎自己的 UI-only 输入模式下什么也听不到：`SetInputMode(FInputModeUIOnly())` 让游戏视口忽略输入，控制器根本收不到。

打开 *Project Settings > Plugins > Dream GUI > Input > Use Slate Input Source*（`bUseSlateInputSource`），DreamGUI 就直接从 Slate 本身听鼠标、触摸、按键和摇杆 —— 一个排在游戏视口之前的输入预处理器 —— 在任何输入模式下都有效。打开期间预设 Actor 会退下，所以不会有东西到达两次。`SlateInputConsumePolicy` 决定 UI 从游戏那里留下什么：

| 值 | UI 留下的 |
| --- | --- |
| `Never`（默认） | 什么都不留；游戏照常听到一切 |
| `WhenOverUI` | 在 DreamGUI UI 上的一次按下、抬起或滚轮，以及 UI 接住的任何按键 |
| `WhenHandled` | 只有落在处理按下的控件上的一次按下，以及 UI 接住的一次按键 |

正在编辑的文本框里敲的键总是会被留下。该输入源目前默认关闭，会在之后的版本里成为默认。
