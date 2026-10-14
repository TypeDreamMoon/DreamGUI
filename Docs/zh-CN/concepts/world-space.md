---
title: "世界空间 UI"
description: "把同一个控件蓝图放进关卡：ADreamWorldWidgetActor 与 UDreamWorldWidgetComponent 的各项属性、两种渲染后端、无需任何设置即可交互、从代码挂接到场景组件，以及关卡序列能够打关键帧的属性。"
---

同一个类既可以是关卡里的一个表面，也可以是屏幕上的一层。在 DreamGUI 中，世界空间是画布的一种一等渲染模式，
而不是“渲染成纹理再贴到四边形上”，命中测试在任何角度都是准确的。

<a id="putting-it-in-a-level"></a>

## 放进关卡

两种做法，结果一样：

- 从内容浏览器把一个控件蓝图拖进关卡；
- 或者从 Place Actors 面板放一个 **DreamUI World Widget Actor**。

两种情况都会得到一个 `ADreamWorldWidgetActor`，它的全部内容就是一个 `UDreamWorldWidgetComponent`（它的根组件）。
像其他场景组件一样移动、旋转和挂接它。该组件也可以添加到你自己的任意 Actor 上（在添加组件列表中叫 **DreamUI World Widget**）。

组件**承载**这棵树，但不会重新定义它。树从 `WidgetClass` 原样实例化，且蓝图自身的根画布会被保留 —— 设计器在那里设置的，
就是关卡里得到的；一棵没有画布的树会被补上一个。组件只把渲染模式、排序顺序和射线通道写到那个画布上，其他一律不动。
它的属性都是实时的：改动尺寸、轴心、后端或排序顺序会重新应用到已加载的树上；只有更换类才会重建它。

<a id="the-components-properties"></a>

## 组件的属性

| 属性 | 默认 | 作用 |
| --- | --- | --- |
| `WidgetClass` | — | 要加载的控件蓝图类。改它是唯一会重建树的编辑 |
| `Backend` | `DreamUIRenderer` | 由谁来绘制，见下一节 |
| `bUseDesignSize` | 开 | 尺寸取自类：该蓝图设计时所用的画布尺寸 |
| `DrawSize` | `(1920, 1080)` | `bUseDesignSize` 关闭时根组件的尺寸，单位是世界单位 |
| `Pivot` | `(0.5, 0.5)` | 组件的原点落在这个矩形上的哪个位置；`(0.5, 0.5)` 让面板以 Actor 为中心 |
| `SortOrder` | `0` | 世界空间画布之间的绘制顺序，会写到根画布上 |
| `TraceChannel` | `TraceTypeQuery1`（Visibility） | 射线检测器必须设置成哪个通道，其射线才能触及这棵树；也是它的遮挡追踪所使用的通道 |

运行时：`SetWidgetClass`、`SetBackend`、`SetUseDesignSize`、`SetDrawSize`（一个显式尺寸，会关闭设计尺寸）、
`SetPivot`、`SetSortOrder`、`SetTraceChannel`。`GetDrawSize` 返回根组件**实际**得到的尺寸。

为什么默认用设计尺寸：屏幕会拉伸到视口，而世界里的面板没有视口可以拉伸。设计时所用的尺寸是唯一有人真正为它选择过的尺寸，
按这个尺寸落地的面板，在关卡里看起来就和设计器里一样。所以设计尺寸跟着类走，并保留到烹饪后的版本中
（`UDreamWidgetGeneratedClass::GetDesignSize`）。

<a id="the-two-backends"></a>

## 两种渲染后端

`Backend`（`EDreamWorldWidgetBackend`）对应根画布的渲染模式：

| 后端 | 渲染模式 | 特点 |
| --- | --- | --- |
| `DreamUIRenderer` | `WorldSpace_DreamUI` | DreamGUI 自己的渲染器：在场景之后由视图扩展绘制，使用 DreamUI 的排序。文字更锐利、绘制顺序精确；没有场景光照，不受后处理影响 |
| `UERenderer` | `WorldSpace` | 网格是场景里的普通图元：像其他物体一样参与光照、投影、雾和遮挡，按半透明规则而非 DreamUI 的规则排序；后处理会生效 |

使用 DreamUI 渲染器时，根画布上的 `BlendDepth` 决定场景深度遮挡它的程度：`0` 被场景深度遮挡，
`1` 全部可见，`0.5` 半透明；`DepthFade` 会增加一层深度渐隐。世界空间画布在共享的
渲染图 pass 中绘制，每个面板的工作分派给工作线程；`Tools/Bench` 有一个包含 2688 个世界空间面板的关卡用于
衡量这部分开销（见[基准测试](../tools/bench.md)）。

<a id="interaction-no-setup"></a>

## 交互：无需任何设置

按下 Play 就足以点击挂在世界里的按钮。在 `BeginPlay` 时，组件会为每个本地玩家请求一个事件系统和
一个 `UDreamWorldSpaceRaycaster`，并补齐缺失的那一个
（`UDreamUIInputSubsystem::EnsureInteractionForPlayer` 把射线检测器放在一个临时的 `DreamInteractionHost_P%d` Actor 上；
事件系统从 `UDreamGUISettings::EventSystemActorClass` 生成）。

射线检测器是**每个玩家一个**，不是每个面板一个：射线由该玩家的视角瞄准，它能命中哪些画布，是通过向管理器询问
`TraceChannel` 与其自身一致的世界空间根来回答的。添加、移动或销毁面板都不需要通知它，面板也不需要自己的交互组件。

| 射线检测器属性 | 默认 | 作用 |
| --- | --- | --- |
| `PointerSource` | `Mouse` | `Mouse` 从指针自身的屏幕位置（鼠标、触摸、虚拟光标）发射；`ScreenCenter` 从视图正中央发射，适合第一人称准星 —— 分屏时是该玩家自己那块视口的正中央 |
| `bOccludeByWorld` | 开 | 实体几何会像挡住线追踪一样挡住点击 |
| `TraceChannel` | Visibility | 只对相同通道上的根作出响应；开启遮挡时，也是遮挡追踪的碰撞通道 |
| `RayLength` | `100000` | 射线长度 |
| `DragThreshold` / `bHoldToDrag` / `HoldToDragTime` | `5` / 关 / `0.5` | 拖拽阈值（在目标的本地空间中计算），以及按住以开始拖拽 |

在任意 Actor 上放一个你自己的、带有相同 `UserIndex` 的射线检测器，系统就不会再往上叠加 —— 检查的是“是否存在一个”，
而不是“是不是本插件创建的”。这样你就能使用自己的指针来源、射线长度、遮挡或拖拽行为。

<a id="occlusion-by-the-world"></a>

### 被世界遮挡

开启 `bOccludeByWorld` 后，每个指针每帧沿着用于命中测试面板的那条射线，在 `TraceChannel` 所代表的碰撞通道上
（除非改动过，否则是引擎的 Visibility）做**一次**单次线追踪。命中的东西不带控件，但它在距离上胜出，因此它后面的面板不再被指向；
被它命中的 Actor 则会收到指针事件 —— 进入、离开、按下、抬起、点击、双击、长按、滚动 —— 事件发送给它自身以及它身上实现了指针接口的任意组件
（见 `FDreamUIPointerWorldTarget`）。渲染目标表面的 `UDreamUIRenderTargetInteraction` 就是这样拿到指针的：该表面是世界里的一个网格，
而这次追踪是唯一能触及它的东西。一堵墙不实现任何接口，只是一个遮挡物。

在 Visibility 上挡住指针的是实体几何：静态网格组件的默认 `BlockAllDynamic`、放置的静态网格 Actor 的 `BlockAll`。
**不**会挡的有：Pawn 的胶囊体和角色的网格（引擎的 Pawn、CharacterMesh 和 Spectator 配置都忽略 Visibility）、触发器，
以及裸的盒、球或胶囊组件（它们的默认 `OverlapAllDynamic` 只重叠，而追踪是单次的，因此重叠永远不构成遮挡）。

**注意：**

  射线检测器所在的 Actor 会被跳过，其他都不会。如果摄像机周围有一个挡住该通道的图元 —— 比如一个被赋予阻挡 Visibility 配置的 Pawn，
  从它内部往外看 —— 它会在距离为零、射线起点处被命中，并让所有世界空间面板都点不到。把射线检测器放在那个 Pawn 上，
  或者给该 Pawn 一个忽略该通道的配置。

要点击穿过墙体，可自行在射线检测器上取消勾选它，或对玩家那个调用 `SetOccludeByWorld(false)`。它默认开启是因为：关闭时，世界中的指针会直接穿墙点到玩家看不到的面板上 —— 一个在隔壁房间，一个在柱子后面 —— 并且完全不会触碰到渲染目标表面；而为玩家创建的射线检测器是默认的，因此依赖它的项目会同时遇到这两个问题，却又找不到任何设置来解释它们。代价是每个指针每帧一次追踪。

<a id="from-code"></a>

## 从代码

还是原本就有的两个调用：先用 `UDreamUIBPLibrary::ConstructWidget` 创建根节点，再对应当承载这棵树的任意场景组件调用 `AttachWidgetToSceneComponent`。根节点必须带画布（画布决定渲染模式并拥有网格），且不能有父级；否则调用返回 `false` 且不改变任何东西。挂接也是让根节点脱离尚未添加状态、从而被绘制和命中的关键一步。

```cpp
#include "DreamUIBPLibrary.h"
#include "Core/Components/DreamCanvas.h"

UDreamWidget* Panel = UDreamUIBPLibrary::ConstructWidget(this, TEXT("Panel"), nullptr);
Panel->SetWidth(800.0f);
Panel->SetHeight(450.0f);

UDreamCanvas* Canvas = Panel->AddComponent<UDreamCanvas>();
Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);

// ...build the children with ConstructWidget + AddChild...

UDreamUIBPLibrary::AttachWidgetToSceneComponent(Panel, GetRootComponent());
```

对于控件蓝图类，给 Actor 添加一个 `UDreamWorldWidgetComponent` 并调用 `SetWidgetClass` 更省事：尺寸、轴心、后端和通道都由该组件负责。

<a id="level-sequence"></a>

## Level Sequence

`ADreamWorldWidgetActor` 可以被 Level Sequence 直接占有，并且组件上的 `WidgetOpacity`、`WidgetOffset` 和 `bWidgetVisible` 都能在其中打关键帧。树内部的动画见[动画](animation.md)。

<a id="what-no-longer-exists"></a>

## 已不存在的内容

2.0 之前的世界空间根蓝图（`WorldSpaceRoot_DreamRenderer`、`WorldSpaceRoot_UERenderer`、`DreamWorldSpaceRaycasterSource_Mouse`）、`UDreamWidgetPresenterComponent`（抽象基类 `UDreamWidgetPresenterComponentBase` 仍在；要放置的是 `UDreamWorldWidgetComponent`），以及 `UDreamWorldSpaceRaycasterBase` / `ForWorldTrigger` / `Source` 一族（被 `UDreamWorldSpaceRaycaster` 吸收）都已删除，且没有重定向。仍放置了这些蓝图的关卡会在加载时丢弃该 Actor 并记录一条点名它的警告；把控件蓝图重新拖进关卡即可。见[迁移](../guides/migration.md)。
