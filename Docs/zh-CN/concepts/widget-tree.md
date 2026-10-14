---
title: "控件树、停车与宿主"
description: "控件活在一条树上，树决定由谁照看它们 —— 还有树上最容易被误读的那个状态，即“停放”。此外还有宿主契约与呈现器基类。"
---

控件活在一条树上，树决定由谁照看它们。[控件模型](widgets.md)讲的是
一个控件由什么组成；本页讲的是**树本身**，以及树上最容易被误读的那个状态。

- 四个树动词、视觉与行为 → [控件模型](widgets.md)
- 用户控件中的 `WidgetTree` → [控件蓝图](widget-blueprints.md)
- “停放”**是什么** → [脚本](../tools/scripting.md)
- **树底下是什么、以及什么承载停放** → 本页

<a id="what-the-widget-tree-is-and-why-there-has-to-be-one"></a>

## 控件树是什么，为什么必须有一条

`UDreamWidgetTree` 是一个 `UObject`，一句注释交代了它存在的唯一理由：**它拥有一条控件层级，并且这条层级里的每个控件都被 outer 到它**。

要理解它，看看它取代了什么：早期 DreamGUI 把控件**扁平**地 outer 到 `UWorld`，层级只活在 prefab 的二进制块里，加载时再重建。那样引擎**没有对象图**可以序列化、复制或实例化 —— **这也正是一条树没法住在一个类模板上的原因**。树就是那个缺失的锚点。

在树里，控件同样**保持扁平**，而不是嵌套在父级之下 —— 与 UMG 一致。
`FObjectInstancingGraph` 跟随的是 **`Instanced` 属性，而不是 outer 链**，所以扁平 outer **不花任何代价**；而且
**重新挂父级永远不必移动 outer**，这就把 `UObject::Rename`（对象一旦开始销毁就不安全）**挡在了挂接路径之外**。

有两件事它刻意不做：

- **作为类模板被持有时，树不返回世界、从不注册、从不 tick。** 一个被类当作模板持有的树**故意**不触达关卡，所以 `GetWorld` 返回**空世界**，注册只通过显式的 `OnRegister` 调用发生（**从不**通过 `PostInitProperties`）。
- **父指针不序列化**；`RebuildParentLinks` 从持久的 `Children` 数组恢复它们 —— 在任何**不经过挂接函数就产出树**的路径（包加载、类模板实例化、子树复制）之后、注册之前，它**都必须跑**。

<a id="hosts-who-owns-a-tree"></a>

## 宿主：谁拥有一条树

真正拥有一条树的是一个**宿主**（`IDreamWidgetTreeHost`）：宿主是它每一棵树的 `Outer`，所以控件仍然能通过它找到自己的世界。

每一条树都由宿主用一个 **`Transient` / `DuplicateTransient` / `TextExportTransient`** 属性持有 —— 所以
**任何保存、复制或导出宿主的东西都不会顺手带走一条树**。

管理器只**观察**：一条**已注册但失去了宿主**的树是**要报告出来的 bug**，而不是留给 GC 去清的东西。

宿主接口只有三个方法：

| 方法 | 做什么 |
| --- | --- |
| `GetTreeOuter()` | 它每一棵树的 `Outer`；其 outer 链**必须触达**宿主所在的 `UWorld` |
| `ReleaseTree(Reason)` | **立刻**拆除，幂等；`Reason` 取 `Recompile` / `Undo` / `LevelRemoved` / `HostDestroyed` —— **前两者之后宿主会重建，后两者不** |
| `RebuildTree()` | 在一次**不是销毁**的释放之后，按类重建 |

<a id="queries-and-names-on-the-tree"></a>

## 树上的查询与命名

四个动词（`AddChild` / `RemoveChild` / `DestroyChild` / `FindChildByDisplayName`）在
[控件模型](widgets.md)里。树多出来的是一组**查询与命名**的 API：

| 成员 | 做什么 |
| --- | --- |
| `RootWidget`（`Instanced`） | 层级的根。`FObjectInstancingGraph` **从这里**进入树 |
| `ForEachWidget(Predicate)` | 访问树里**每一个**控件，**根在前、父先于子**，什么都不跳过 |
| `GetAllWidgets()` | 同一趟遍历的蓝图面 —— UMG ``UWidgetTree::GetAllWidgets`` 的对应物 |
| `CountWidgets()` | 总数，**含根**；⚠️ **遍历，不缓存** |
| `FindWidgetByVariableName(Name)` | 按**变量名**找 —— UMG 的 `GetWidgetFromName` |
| `MakeWidgetVariableName(Widget)` | 控件**绑定到**的成员变量名：把它的**显示名**净化成标识符 |
| `SanitizeIdentifier(Raw)` | 只保留字母、数字与下划线，**保留非 ASCII**（CJK 显示名很常见），且**从不以数字开头** |

⚠️ **命名为什么是运行时的事**：控件按**显示名**命名（**不是对象名** —— 对象名是生成的）。编译器**靠它声明变量**，运行时**靠它解析绑定** —— **两边一致就是全部契约**。编辑器侧再放一份**微妙地不同**的副本，正是“**绑定报告成功、却拿回空**”的出处。

<a id="what-is-under-the-parked-state"></a>

## 停放状态底下是什么

**“停放”是什么**（不画、行为不跑、被管理器持有、`IsWidgetParked` 查询）在
[脚本](../tools/scripting.md)里。实现把一个控件状态位与管理器带时间戳的登记项结合起来：

| 部件 | 在哪 | 作用 |
| --- | --- | --- |
| `bParked`（位域，默认 `false`） | `DreamWidget.h` | 控件自己的“**等一个父级**”位 |
| `FDreamParkedWidgetEntry` | `DreamUIManager.h` | 一条登记：控件指针 + `ParkedAtSeconds` |
| `ParkedWidgets`（`TArray`） | `DreamUIManager.h` | 管理器持有的列表 |

⚠️ **`bParked` 故意是一个独立的位，不是 `bWidgetActive` 的一个状态** —— 借用 `bWidgetActive` 会让
`SetWidgetActive` 在“调用方正在配置控件”的那段窗口里**静默地什么都不做**。所以这个位的含义是
“**等一个父级**”，而**不是**“走到了人生的哪个阶段”：**一个停放的控件是已注册的，而且世界一开始跑，它就已经
``BeginPlay`` 过了**。

**停车发生在两个地方**：`ConstructWidget` 建完一个控件时，以及 `RemoveChild` 把一个子级摘下来时。⚠️ 一段源码注释解释了**为什么不在通用的 detach 路径里**：**在两个父级之间移动也会中途 detach**，在那里停车会把行为**在一个并非移除的操作里关掉一瞬**。

把控件移出停车位的是 `UnparkWidget`，它**每次挂接都会跑**，对一个从没停放过的控件**返回 `false`** —— **这是常态**。

**“行为不跑”归结于一个判断**：在 `CalculateWidgetActive` 里，
`if (!bWidgetActive || bParked) bResultActive = false;` —— **只要 `bParked` 为真，整棵子树都算作不激活**，于是它不被绘制、不参与布局，它的行为收到 `OnDisable`。

⚠️ **别把两张表混为一谈**：

| 表 | 装什么 | 谁清 |
| --- | --- | --- |
| `ParkedWidgets` | 创建之后、**或从父级摘下来之后**等待父级的控件 | 挂接时的 `UnparkWidget`；世界拆除；或下面的清扫 |
| `FreeRoots` | **已注册、但没有任何宿主持有的树根** | 管理器自己的池；**其中没有一条算泄漏** |

<a id="the-sweep-and-leak-detection"></a>

## 清扫与泄漏探测

停车位有一个可选的**泄漏探针**，**默认关闭**：`UDreamUISettings::ParkedWidgetLifetimeSeconds`，
默认 **`0.0f`**（**`0` 会关掉这项检查，也是默认值**）。

⚠️ 它是一台**开发期泄漏探测器，不是寿命策略** —— 一条**正当的慢路径**（比如在等异步加载）从这里看
**和泄漏一模一样**，而销毁这类调用方的控件**等于自己制造 bug**。

扫描**每帧**都跑（它是 `TickDreamUI` 里的第一件事）。关键在于那句注释 **“Destroying is the point”**：控件是**已注册**的，单纯放手会让 GC 去收一个**仍处于注册态**的控件，而那条路径会从**一个和本源无关的调用栈**打出“not destroyed by its owner”错误。所以扫描**主动销毁**并警告，走 `DestroyWidget()` —— 它**按正确顺序**注销并结束 play，于是控件**永远不会在仍注册的状态下**走到 `BeginDestroy`。

<a id="the-presenter-base-class"></a>

## 呈现器基类

`UDreamWidgetPresenterComponentBase` 是 **`USceneComponent` + `IDreamWidgetTreeHost`**，`UCLASS(Abstract, ClassGroup=DreamGUI, Blueprintable, BlueprintSpawnableComponent)`。

⚠️ **抽象是故意的**：`LoadWidget()` 是 `PURE_VIRTUAL`，所以这个类的实例**一注册就断言** —— 没有 `Abstract` 的话，“添加组件”菜单会把它列出来，**在那里挑中它会是一次崩溃而不是一个错误**。要加的是落地子类 [`UDreamWorldWidgetComponent`](world-space.md)。

它把宿主契约落实为 `OwnedTree`（`Transient, NonTransactional, DuplicateTransient, TextExportTransient`）。**`NonTransactional` 也有理由**：事务连 `transient` 属性都记，一次把组件带回来的 undo/redo 会写回**它当时持有的树**（一棵已销毁的，或本来有活树却写成没有）—— **哪棵树归组件自己管，不归 undo 管**。

两条判据：

| 判据 | 回答 | 为什么需要 |
| --- | --- | --- |
| `IsLoadedWidgetCurrent()` | 已经装着的树**是不是**此刻会重建的那一棵 | 编辑器里改宿主 actor 的**任何属性**都会触发一次 reregister，而 **reregister 不是拆除** |
| `IsInAWorldThatRunsTrees()` | 此刻建的树会不会落在一个**跑树的世界**里 | `Inactive` 或无类型的世界是“正在被制造 / 预加载”的包，在那建的树**没有管理器** |

**呈现器级动画前向**：`WidgetOpacity` / `WidgetOffset` / `bWidgetVisible`（都是 `Interp`，**可由 Level Sequence 打点**）。每个 setter **写穿到根控件**（透明度走 `RenderOpacity`、偏移走**渲染-only 的平移**，**都不碰布局**）。`NotifyWidgetLoaded` 则通知世界里每个 Level Sequence 播放器**重新解析绑定** —— 一个在树存在之前求值的自定义控件绑定否则会一直失败。

<a id="when-it-does-not-serve"></a>

## 什么时候它不好用

- **停放探测默认关，别当寿命策略** —— 发布时开着会**误杀正常等待异步加载的调用方**。
- **`ParkedWidgets` 与 `FreeRoots` 不是一回事** —— 查错先分清在哪张表。
- **`CountWidgets` 不缓存、每次都遍历** —— 高频调用要自己缓存结果。
- **呈现器基类不能被添加** —— `LoadWidget` 是 `PURE_VIRTUAL`，实例一注册就断言。
- **父指针不序列化** —— 任何绕过挂接函数造树的路径，注册前必须 `RebuildParentLinks`。
