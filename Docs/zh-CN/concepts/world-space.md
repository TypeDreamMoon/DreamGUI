---
title: 世界空间 UI
description: 把同一个 Widget Blueprint 放进关卡：ADreamWorldWidgetActor 与 UDreamWorldWidgetComponent 的各项属性、两种渲染后端、不用任何设置就能点击的交互、从代码里挂到场景组件上，以及 Level Sequence 能打关键帧的属性。
---

同一个类既可以是屏幕上的一层，也可以是关卡里的一个表面。在 DreamGUI 里世界空间是画布的一种一等渲染模式，
不是"渲染成纹理再贴到四边形上"，命中测试在任何角度都是准的。

<a id="放进关卡"></a>

## 放进关卡

两种做法，结果一样：

- 从内容浏览器把一个 Widget Blueprint 拖进关卡；
- 或者从 Place Actors 面板放一个 **DreamUI World Widget Actor**。

得到的都是一个 `ADreamWorldWidgetActor`，它的全部内容就是一个 `UDreamWorldWidgetComponent`（它的根组件）。
像别的场景组件一样移动、旋转、挂接它。这个组件本身也可以加到你自己的任何 Actor 上（添加组件时叫 **DreamUI World Widget**）。

组件**承载**这棵树，不重新定义它。树从 `WidgetClass` 原样实例化，Blueprint 自己的根画布被保留下来 —— 设计器里在那里设的，
就是关卡里得到的；一棵没有画布的树会被补上一个。组件只把渲染模式、排序和射线通道写到根画布上，别的不动。
属性都是活的：改尺寸、轴心、后端或排序会重新作用到已经加载的树上，只有换类才会重建。

<a id="组件的属性"></a>

## 组件的属性

| 属性 | 默认 | 作用 |
| --- | --- | --- |
| `WidgetClass` | — | 要加载的 Widget Blueprint 类。改它是唯一会重建树的编辑 |
| `Backend` | `DreamUIRenderer` | 谁来画，见下一节 |
| `bUseDesignSize` | 开 | 尺寸取自类：Blueprint 设计时的画布尺寸 |
| `DrawSize` | `(1920, 1080)` | `bUseDesignSize` 关掉时根控件的尺寸，单位是世界单位 |
| `Pivot` | `(0.5, 0.5)` | 组件的原点落在这个矩形的哪里；`(0.5, 0.5)` 让面板以 Actor 为中心 |
| `SortOrder` | `0` | 与其他世界空间画布之间的绘制顺序，写到根画布上 |
| `TraceChannel` | `TraceTypeQuery1`（Visibility） | 射线检测器要用哪个通道才能碰到这棵树，也是它的遮挡追踪所用的通道 |

运行时对应 `SetWidgetClass`、`SetBackend`、`SetUseDesignSize`、`SetDrawSize`（设了明确的尺寸，就等于关掉设计尺寸）、
`SetPivot`、`SetSortOrder`、`SetTraceChannel`。`GetDrawSize` 回答的是根控件**实际**拿到的尺寸。

为什么默认用设计尺寸：屏幕会拉伸到视口，世界里的面板没有视口可拉伸，设计时画的那个尺寸是唯一有人真正选过的尺寸 ——
按它落地，关卡里看起来就和设计器里一样。所以设计尺寸跟着类走，一直保留到烹饪后的版本里
（`UDreamWidgetGeneratedClass::GetDesignSize`）。

<a id="两种渲染后端"></a>

## 两种渲染后端

`Backend`（`EDreamWorldWidgetBackend`）对应根画布的渲染模式：

| 后端 | 渲染模式 | 特点 |
| --- | --- | --- |
| `DreamUIRenderer` | `WorldSpace_DreamUI` | DreamGUI 自己的渲染器，在场景之后由视图扩展画出，用 DreamUI 的排序：文字更锐利、绘制顺序精确；没有场景光照，不受后处理影响 |
| `UERenderer` | `WorldSpace` | 网格是场景里的普通图元：参与光照、投影、雾和遮挡，按半透明规则而不是 DreamUI 的规则排序，后处理照常作用 |

用 DreamUI 渲染器时，根画布上的 `BlendDepth` 决定场景深度遮挡它的程度：`0` 被场景深度遮挡，`1` 全部可见，`0.5` 半透明；
`DepthFade` 加一层深度渐隐。世界空间画布在共享的渲染图 pass 里绘制，每个面板的工作分给了工作线程；
`Tools/Bench` 里有一个 2688 个世界空间面板的基准关卡，用来量这部分开销（见[基准测试](../tools/bench.md)）。

<a id="交互不需要任何设置"></a>

## 交互：不需要任何设置

按下 Play 就能点到挂在世界里的按钮。`BeginPlay` 时组件为每个本地玩家要一个事件系统和一个 `UDreamWorldSpaceRaycaster`，
缺哪个补哪个（`UDreamUIInputSubsystem::EnsureInteractionForPlayer` 把射线检测器建在一个临时的
`DreamInteractionHost_P%d` Actor 上，事件系统从 `UDreamGUISettings::EventSystemActorClass` 生成）。

射线检测器是**每个玩家一个**，不是每个面板一个：射线由那个玩家的视角瞄准，能命中哪些画布是向管理器要的 ——
`TraceChannel` 与它相同的世界空间根。所以面板的增删移动它都不必知道，面板也不需要带自己的交互组件。

| 射线检测器属性 | 默认 | 作用 |
| --- | --- | --- |
| `PointerSource` | `Mouse` | `Mouse` 从指针自己的屏幕位置（鼠标、触摸、虚拟光标）发射；`ScreenCenter` 从视图正中发射，适合第一人称准星。分屏时是该玩家自己那块视口的正中 |
| `bOccludeByWorld` | 开 | 实体几何像挡住线追踪一样挡住点击 |
| `TraceChannel` | Visibility | 只回答 `TraceChannel` 相同的根；开着遮挡时也是遮挡追踪的碰撞通道 |
| `RayLength` | `100000` | 射线长度 |
| `DragThreshold` / `bHoldToDrag` / `HoldToDragTime` | `5` / 关 / `0.5` | 拖拽阈值（在目标的本地空间里算）、按住才开始拖 |

在任何 Actor 上放一个你自己的射线检测器，`UserIndex` 相同，系统就不会再叠一个上去 —— 检查的是"有没有"，
不是"是不是插件建的"。所以自定义指针来源、射线长度、遮挡或拖拽行为，放一个你自己的就行。

<a id="世界遮挡"></a>

### 世界遮挡

`bOccludeByWorld` 打开时，每个指针每帧沿着测试面板的那条射线做**一次**单次线追踪，碰撞通道就是 `TraceChannel` 代表的那个
（除非改过，就是引擎的 Visibility）。命中的东西不带控件，但按距离取胜：它后面的面板不再被指向；而被它挡住的那个 Actor
会收到指针的事件 —— 进入、离开、按下、抬起、点击、双击、长按、滚动 —— 发给它自己和它身上实现了指针接口的组件
（见 `FDreamUIPointerWorldTarget`）。网格上的渲染目标表面（`UDreamUIRenderTargetInteraction`）就是这样拿到指针的：
那个表面是世界里的网格，这次追踪是唯一能碰到它的东西。一堵墙什么接口都不实现，只是一个遮挡物。

在 Visibility 上挡住指针的是实体几何：静态网格组件默认的 `BlockAllDynamic`、放置的静态网格 Actor 的 `BlockAll`。
**不**挡的有：Pawn 的胶囊体和角色网格（引擎的 Pawn、CharacterMesh、Spectator 配置都忽略 Visibility）、触发器、
裸的盒 / 球 / 胶囊组件（默认 `OverlapAllDynamic` 只重叠，而追踪是单次的，重叠永远不算遮挡）。

**警告：**

射线检测器所在的 Actor 会被跳过，其他的不会。如果摄像机周围有一个挡住该通道的图元 —— 比如一个改成了挡 Visibility 的 Pawn，
  从它内部往外看 —— 它会在距离 0、射线起点处被命中，所有世界空间面板都点不到了。把射线检测器放到那个 Pawn 上，
  或者给 Pawn 一个忽略该通道的碰撞配置。

要穿墙点击，在你自己的射线检测器上取消勾选它，或者对玩家现有的那个调用 `SetOccludeByWorld(false)`。默认开着的理由是：
关着时，世界里的指针会穿过墙点到玩家看不见的面板（隔壁房间的、柱子后面的），而且永远碰不到渲染目标表面；
而系统为玩家自动建的就是一个默认的射线检测器，依赖它的项目会同时碰到这两个问题，却找不到能解释它们的设置。代价是每个指针每帧一次追踪。

<a id="从代码里放"></a>

## 从代码里放

还是原来那两个调用：`UDreamUIBPLibrary::ConstructWidget` 建出根控件，然后 `AttachWidgetToSceneComponent` 把它挂到
应当承载它的那个场景组件上。根控件必须带一个画布（画布决定渲染模式、拥有网格），而且不能已有父级；
否则返回 `false`，什么也不改。挂接也是让根控件离开"已建未加"状态、开始绘制和接受命中的那一步。

```cpp
#include "DreamUIBPLibrary.h"
#include "Core/Components/DreamCanvas.h"

UDreamWidget* Panel = UDreamUIBPLibrary::ConstructWidget(this, TEXT("Panel"), nullptr);
Panel->SetWidth(800.0f);
Panel->SetHeight(450.0f);

UDreamCanvas* Canvas = Panel->AddComponent<UDreamCanvas>();
Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);

// ……用 ConstructWidget + AddChild 搭好孩子……

UDreamUIBPLibrary::AttachWidgetToSceneComponent(Panel, GetRootComponent());
```

要放一个 Widget Blueprint 类，直接给 Actor 加一个 `UDreamWorldWidgetComponent` 再 `SetWidgetClass` 更省事：
尺寸、轴心、后端、通道都由组件处理。

<a id="level-sequence"></a>

## Level Sequence

`ADreamWorldWidgetActor` 可以直接被 Level Sequence 占有（possess），组件上的 `WidgetOpacity`、`WidgetOffset` 和
`bWidgetVisible` 都能在里面打关键帧。树内部的动画见[动画](animation.md)。

<a id="已经不存在的东西"></a>

## 已经不存在的东西

2.0 之前的世界空间根 Blueprint（`WorldSpaceRoot_DreamRenderer`、`WorldSpaceRoot_UERenderer`、
`DreamWorldSpaceRaycasterSource_Mouse`）、`UDreamWidgetPresenterComponent`（抽象基类 `UDreamWidgetPresenterComponentBase`
还在，要放的是 `UDreamWorldWidgetComponent`），以及 `UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source`
这一族（被 `UDreamWorldSpaceRaycaster` 吸收了）都已删除，没有重定向。关卡里还放着那些 Blueprint 的 Actor 会在加载时被丢弃，
日志里有一条警告说出它的名字；把 Widget Blueprint 重新拖进关卡即可。详见[迁移](../guides/migration.md)。
