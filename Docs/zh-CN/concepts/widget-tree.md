---
title: 控件树、挂起与停车
description: 控件活在一条树上，树决定它们归谁管 —— 以及它上面最容易被误读的那个状态："停放"。还有宿主的契约与呈现器基类。
---

控件活在一条树上，树决定它们归谁管。[控件模型](widgets.md)讲「一个控件由什么组成」，这一页讲**那条树本身**，以及它上面最容易被误读的那个状态。

- 四个树动词与「视觉 / 行为」→ [控件模型](widgets.md)
- 用户控件里的 `WidgetTree` → [Widget Blueprint](widget-blueprints.md)
- 「停放」**是什么** → [脚本](../tools/scripting.md)
- **树底下是什么、停放由什么承载** → 本页

<a id="控件树是什么为什么必须有"></a>

## 控件树是什么，为什么必须有

`UDreamWidgetTree` 是一个 `UObject`，一句注释交代了它存在的唯一理由：**它拥有一条控件层级，是这条层级里每个控件被 outer 到的那个对象**。

要理解它，先看它取代了什么：DreamGUI 早期把控件**扁平** outer 到 `UWorld`，层级只活在 prefab 的二进制块里，加载时再重建。那样引擎**没有一张对象图**可以序列化、复制或实例化 —— **这也正是一棵树没法住在一个类模板上的原因**。树就是补上的那个锚点。

控件在这棵树里同样**保持扁平**，不套在父级下 —— 和 UMG 一致。`FObjectInstancingGraph` 跟的是 **`Instanced` 属性、不是 outer 链**，所以扁平 outer **不花任何代价**；而且**重新挂父级永远不必移动 outer** —— 这就把 `UObject::Rename`（对象一旦开始销毁就不安全）**挡在了挂接路径之外**。

两处「刻意不做」：

- **作为类模板持有时，树不返回世界、从不注册、从不 tick。** 一个被类当模板持有的树**故意**不触达关卡，所以 `GetWorld` 返回**空世界**，注册只走显式的 `OnRegister` 调用（**从不靠** `PostInitProperties`）。
- **父指针不序列化**，靠 `RebuildParentLinks` 从持久的 `Children` 数组恢复 —— 任何「**不经过挂接函数就产出树**」的路径（包加载、类模板实例化、子树复制）之后、注册之前，**都得跑一次**。

<a id="宿主谁拥有一棵树"></a>

## 宿主：谁拥有一棵树

真正「拥有」一棵树的是一个**宿主**（`IDreamWidgetTreeHost`）：宿主是它每一棵树的 `Outer`，所以控件还能通过它找到世界。

每一棵树由宿主用一个 **`Transient` / `DuplicateTransient` / `TextExportTransient`** 的属性持有 —— 于是**任何保存、复制、导出宿主的东西都不会顺手带走一棵树**。

管理器只**观察**：一棵**已注册、宿主却没了**的树是**要报告出来的 bug**，不是交给 GC 去清的东西。

宿主接口只有三个方法：

| 方法 | 做什么 |
| --- | --- |
| `GetTreeOuter()` | 它每一棵树的 `Outer`，其 outer 链**必须触达**宿主所在的 `UWorld` |
| `ReleaseTree(Reason)` | **立刻**拆除，幂等；`Reason` 取 `Recompile` / `Undo` / `LevelRemoved` / `HostDestroyed` —— **前两者之后宿主会重建，后两者不** |
| `RebuildTree()` | 在一次「**不是销毁**」的释放之后，按类重建 |

<a id="树上的查询与命名"></a>

## 树上的查询与命名

四个动词（`AddChild` / `RemoveChild` / `DestroyChild` / `FindChildByDisplayName`）在[控件模型](widgets.md)里已写。树自己多出来的是一组**查询与命名**的 API：

| 成员 | 做什么 |
| --- | --- |
| `RootWidget`（`Instanced`） | 层级的根。`FObjectInstancingGraph` 从**这里**进入树 |
| `ForEachWidget(Predicate)` | 访问树里**每一个**控件，**根在前、父先于子**，什么都不跳过 |
| `GetAllWidgets()` | 同一趟遍历的蓝图面 —— UMG `UWidgetTree::GetAllWidgets` 的对应物 |
| `CountWidgets()` | 总数，**含根**；⚠️ **遍历，不缓存** |
| `FindWidgetByVariableName(Name)` | 按**变量名**找 —— UMG `GetWidgetFromName` |
| `MakeWidgetVariableName(Widget)` | 控件**绑定到**的成员变量名：把**显示名**净化成标识符 |
| `SanitizeIdentifier(Raw)` | 只留字母数字与下划线、**保留非 ASCII**（CJK 显示名很常见）、**不以数字开头** |

⚠️ **命名为什么在运行时**：控件按**显示名**命名（**不是对象名** —— 对象名是生成的）。编译器**靠它声明变量**、运行时**靠它解析绑定** —— **两边一致就是全部契约**。编辑器侧再放一份「**微妙地不同**」的副本，正是「**绑定报告成功、却拿回空**」的出处。

<a id="停放状态底下是什么"></a>

## 停放状态底下是什么

**「停放」是什么**（不画、行为不跑、被管理器持有、`IsWidgetParked` 查询）在[脚本](../tools/scripting.md)里写了。这里说**它底下是什么** —— 由**两个位和一个数组**共同承担：

| 部件 | 在哪 | 作用 |
| --- | --- | --- |
| `bParked`（位域，默认 `false`） | `DreamWidget.h` | 控件自己的「**等一个父级**」位 |
| `FDreamParkedWidgetEntry` | `DreamUIManager.h` | 一条登记：控件指针 + `ParkedAtSeconds` |
| `ParkedWidgets`（`TArray`） | `DreamUIManager.h` | 管理器持有的登记表 |

⚠️ **`bParked` 故意是一个独立的位，不是 `bWidgetActive` 的一个状态** —— 借 `bWidgetActive` 会让 `SetWidgetActive` 在「调用方正在配置控件」的那段窗口里**静默失效**。所以这个位的语义是「**等一个父级**」，而**不是**「走到人生哪一步」：**停在那里的控件是已注册的，世界一开始跑就已经 `BeginPlay`**。

**停车键在两个地方按下**：`ConstructWidget` 建完控件时，和 `RemoveChild` 把子级摘下来时。⚠️ 源码注释解释了**为什么不在通用 detach 路径**：**两个父级之间的移动也会中途 detach**，在那里停车会把行为**在一个「并非移除」的操作里关掉一瞬**。

移出停车位的是 `UnparkWidget`，它**每次挂接都会跑**，对一个从没停过的控件**返回 `false`** —— 这是**常态**。

**「不跑行为」落在一行判据上**：`CalculateWidgetActive` 里 `if (!bWidgetActive || bParked) bResultActive = false;` —— **只要 `bParked` 为真，这棵子树一律判为不激活**，于是不画、不排布局、行为收到 `OnDisable`。

⚠️ **两张表别混**：

| 表 | 装什么 | 谁清 |
| --- | --- | --- |
| `ParkedWidgets` | **刚创建、从未挂上**的控件（停车位） | 挂接时 `UnparkWidget`；世界拆除；或下面的清扫 |
| `FreeRoots` | **注册过的树根、没有任何宿主持有** | 管理器自己的池；里面**没有一条算泄漏** |

<a id="清扫与泄漏探测"></a>

## 清扫与泄漏探测

停车位有个可选的**泄漏探针**，**默认关着**：`UDreamUISettings::ParkedWidgetLifetimeSeconds`，默认 **`0.0f`**（**`0` 关掉这项检查，也是默认**）。

⚠️ 它是一台**开发期泄漏探测器，不是寿命策略** —— 一个**正当的慢路径**（比如在等异步加载）从这里看**和泄漏一模一样**，把那类调用方的控件销毁掉**等于自己制造故障**。

扫描**每帧**跑（`TickDreamUI` 里第一件事就是它）。关键在那句注释 **「Destroying is the point」**：一个控件是**已注册**的，单纯放手会让 GC 去收一个**仍处于注册态**的控件，而那条路径会从**一个和本源无关的调用栈**打出「not destroyed by its owner」错误。所以清扫**主动销毁**并警告，走 `DestroyWidget()` —— 它**按正确顺序**注销并结束 play，于是控件**永远不会在仍注册的状态下**走到 `BeginDestroy`。

<a id="呈现器基类"></a>

## 呈现器基类

`UDreamWidgetPresenterComponentBase` 是 **`USceneComponent` + `IDreamWidgetTreeHost`**，`UCLASS(Abstract, ClassGroup=DreamGUI, Blueprintable, BlueprintSpawnableComponent)`。

⚠️ **抽象是故意的**：`LoadWidget()` 是 `PURE_VIRTUAL`，所以这个类的实例**一注册就断言** —— 没有 `Abstract` 的话「添加组件」菜单会把它列出来，**在那里挑中它是一次崩溃而不是一个错误**。要加的是落地子类 [`UDreamWorldWidgetComponent`](world-space.md)。

它把宿主契约落实为 `OwnedTree`（`Transient, NonTransactional, DuplicateTransient, TextExportTransient`）。**`NonTransactional` 也有理由**：事务连 `transient` 属性都记，一次把组件带回来的 undo/redo 会写回**它当时持有的树**（一棵已销毁的，或本来有活树却写成没有）—— **哪棵树归组件自己管，不归 undo 管**。

两条判据：

| 判据 | 回答 | 为什么需要 |
| --- | --- | --- |
| `IsLoadedWidgetCurrent()` | 已经装着的树**是不是**此刻会重建的那一棵 | 编辑器里改宿主 actor 的**任何属性**都会触发一次 reregister，而 **reregister 不是拆除** |
| `IsInAWorldThatRunsTrees()` | 此刻建的树会不会落在一个**跑树的世界**里 | `Inactive` 或无类型的世界是「正在被制造 / 预加载」的包，在那建的树**没有管理器** |

**呈现器级动画前向**：`WidgetOpacity` / `WidgetOffset` / `bWidgetVisible`（都是 `Interp`，**可由 Level Sequence 打点**）。每个 setter **写穿到根控件**（透明度走 `RenderOpacity`、偏移走**渲染-only 的平移**，**都不碰布局**）。`NotifyWidgetLoaded` 则通知世界里每个 Level Sequence 播放器**重新解析绑定** —— 一个在树存在之前求值的自定义控件绑定否则会一直失败。

<a id="什么时候它不好用"></a>

## 什么时候它不好用

- **停车探测默认关，别当寿命策略** —— 发布时开着会**误杀正常等待异步加载的调用方**。
- **`ParkedWidgets` 与 `FreeRoots` 不是一回事** —— 查错先分清在哪张表。
- **`CountWidgets` 不缓存、每次都遍历** —— 高频调用要自己缓存结果。
- **呈现器基类不能被添加** —— `LoadWidget` 是 `PURE_VIRTUAL`，实例一注册就断言。
- **父指针不序列化** —— 任何绕过挂接函数造树的路径，注册前必须 `RebuildParentLinks`。
