---
title: "动"
description: "让物体运动的三种方式——DreamTween 模块提供的补间（针对面板内控件的 RenderOffsetTo、RenderScaleTo、RenderAngleTo 和 RenderTranslationTo，EDreamTweenEase，DreamTween.MaxStepSeconds），控件动画（UDreamWidgetAnimation 和 Sequencer），以及 .dui 时间轴。"
---

在 DreamGUI 中让物体运动有三种方式，每种适用于不同类型的任务：

| 方式 | 适用于 | 位置 |
| --- | --- | --- |
| **补间** | 在代码中决定的运动：入场、悬停、值变化 | `DreamTween` 模块，`UDreamTweener` |
| **控件动画** | 在 Sequencer 中编辑的设计动画 | `UDreamWidgetAnimation`，嵌入在类中 |
| **`.dui` 时间轴** | 由文本拥有、每次编译都会重建的控件动画 | `.dui` 中的 `timeline` 块 |

<a id="tweens"></a>

## 补间

`DreamTween` 是一个独立的底层模块。每个 `*To` 函数都返回一个 `UDreamTweener`，你可以链式调用：
`SetEase`、`SetDelay`、`SetLoop`（`Once`、`Restart`、`Yoyo`、`Incremental`；计数为 `-1` 表示无限）、`SetTimeScale`、
`SetAutoKill`，以及回调 `OnStart`、`OnUpdate`、`OnCycleStart`、`OnCycleComplete`、`OnComplete` 和 `OnKill`；
控制它则使用 `Pause`、`Resume`、`Restart`、`Goto`、`ForceComplete` 和 `Kill`。

控件、视觉元素和行为都带有自己的补间：

| 对象 | 补间 |
| --- | --- |
| `UDreamWidget` | `AnchoredPositionTo`、`SizeDeltaTo`、`WidthTo`、`HeightTo`、`RenderOpacityTo`、`LocalPositionTo`、`LocalScaleTo`、`LocalRotatorTo`、`WorldPositionTo`……以及下一节的渲染变换补间 |
| `UDreamVisual` | `ColorTo`、`AlphaTo` |
| `UDreamTextPaintLibrary` | `PaintPhaseTo`、`PaintAngleTo`、`PlayShimmer`（参见[文本](text.md)） |
| `UDreamTweenBPLibrary` | 通用的 `FloatTo`、`Vector3To`、`ColorTo`、`MaterialScalarParameterTo`、`DelayCall`、`SpringFloat`……以及用于 UMG 控件的 `UMG_*` 补间 |

缓动是 `EDreamTweenEase` 中的一个**名称**，整个插件共用一份词表：`Linear`；`In`、`Out` 和 `InOut`
搭配 `Quad`、`Cubic`、`Quart`、`Sine`、`Expo`、`Circ`、`Elastic`、`Back` 和 `Bounce`；以及 `CurveFloat`，它遵循一条
曲线（`SetCurveFloat`）。控件补间默认使用 `OutCubic`，时长为 0.5 秒。

有几条规则容易让人踩坑：

- **已完成和已终止的补间会退役。** 句柄会变为无效，持有它的 UPROPERTY 会在下一次垃圾回收时被置空；`Kill`、`ForceComplete` 和 `Goto` 对已退役的补间不起作用，而 `Restart` 只会发出警告。要在补间结束后再次播放，请在它开始前 `SetAutoKill(false)`，或者从它自己的 `OnComplete` 中 `Restart` 它，这样能让它继续运行。
- 弹簧（`SpringFloat`）默认保持在静止状态；`SetTarget` 可以唤醒它。
- 拥有者已不存在的补间会被丢弃，且不会触发回调。

<a id="pause-and-time-dilation"></a>

### 暂停与时间膨胀

控件补间遵循项目设置：屏幕上的 UI 遵循 `bScreenSpaceUIAffectByGamePause` 和
`bScreenSpaceUIAffectByTimeDilation`（两者默认关闭），其他所有内容遵循 `bWorldSpaceUIAffectByGamePause` 和
`bWorldSpaceUIAffectByTimeDilation`（两者默认开启）。布局动画和嵌入的 UMG 控件遵循同样的规则。

`DreamTween.MaxStepSeconds`（一个控制台变量；`0` 为默认值，表示关闭）限制了一帧能推进补间的最大步长：
超过该上限的一帧——例如屏幕正在加载资源、新文本的首次绘制——只会按上限推进由世界 tick 驱动的补间。
在此类卡顿之前刚开始的入场动画，会从它原来的位置继续播放，而不是直接跳到结尾。手动 tick 永远不会被限制。

<a id="widgets-inside-panels-animate-the-render-transform"></a>

## 面板内的控件：为渲染变换做动画

面板会在它的下一轮中写回子控件的锚定位置和尺寸，因此对面板所放置的控件使用 `AnchoredPositionTo` 或
`LocalPositionTo` 只会与布局轮流生效。应该运动的是**被绘制的内容**，而不是布局：

| 补间 | 移动内容 |
| --- | --- |
| `RenderTranslationTo(FVector)` | 本地空间中的平移：X 为深度，Y 向右，Z 向上 |
| `RenderOffsetTo(FVector2D)` | 在画布平面上的同样移动（x 向右，y 向上），深度保持不变 |
| `RenderScaleTo(FVector)` | 围绕 `RenderTransformPivot` 缩放 |
| `RenderAngleTo(float)` | 平面内角度（`SetRenderTransformAngle`） |

它们移动、缩放和旋转的是被绘制的内容，而绝不会影响布局，因此相邻元素不会为其让出空间。渲染变换
持续变化的控件会被其画布设为渲染层，并在 GPU 上移动（参见[渲染](rendering.md)）。

```cpp
// Card sits in a VerticalBox: move what is drawn, not its slot
Card->SetRenderOpacity(0.0f);
Card->SetRenderTranslation(FVector(0.0, 0.0, -40.0));   // 40 below where layout put it
Card->RenderOpacityTo(1.0f, 0.25f);
Card->RenderOffsetTo(FVector2D::ZeroVector, 0.35f, 0.0f, EDreamTweenEase::OutCubic)
    ->OnComplete([]() { /* landed */ });

// An icon that keeps breathing
Icon->RenderScaleTo(FVector(1.15, 1.15, 1.0), 0.6f, 0.0f, EDreamTweenEase::InOutSine)
    ->SetLoop(EDreamTweenLoop::Yoyo, -1);
```

用 `.dui` 编写的组件同样如此：在面板内，通过渲染变换为实例做动画。

<a id="reusable-tweens-playtween"></a>

## 可复用补间：PlayTween

对于在代码中临时创建的补间，请使用 `*To`。对于**保存在资产中、在细节面板中调整、并由组件驱动**的补间，请使用 `UDreamUIPlayTween`。它是一个**播放器**，而不是补间本身：播放方式被写成可序列化字段，`Tweener` 只在 `Start()` 时才创建。

| 字段 | 默认值 | 备注 |
| --- | --- | --- |
| `LoopType` | `Once` | |
| `LoopCount` | **1** | 从 `-1` 改为该值；见下文 |
| `EaseType` / `EaseCurve` | `Linear` | 曲线仅在 `CurveFloat` 时生效 |
| `Duration` | 0.5 | |
| `StartDelay` | — | |
| 四个事件 | — | `OnStart` / `OnUpdateProgress` / `OnComplete` / `OnCycleComplete` |
| `bAffectByGamePause` / `bAffectByTimeDilation` | **false** | **与控件自身补间相反**（UI 通常不会被暂停拖住） |

入口点是 `Start` / `Stop` / `GetTweener`。共有 **10** 个类型化子类：`Float`、`Double`、`Color`、`Int`、
`LinearColor`、`Quaternion`、`Rotator`、`Vector2`、`Vector3`、`Vector4`——每个都增加了一对 `From` / `To` 以及一个
`OnUpdateValue(interpolated value)`。

`LoopCount` 的默认值从 `-1` 改为 **1** 是有意为之：如果默认无限，那么作者一旦选择循环类型，补间就**永不结束**——它永远不会离开管理器，会让播放器以及其背后的控件保持存活，等待它的序列组件也永远无法进入下一步。

有两个组件将它挂到树上（两者都是 `BlueprintSpawnableComponent`）：

| 组件 | 作用 |
| --- | --- |
| `UDreamUIPlayTweenComponent` | 如有要求，在 `Awake` 上播放一个 play tween 的 `Play()`（`bPlayOnStart` 默认为 true） |
| `UDreamUIPlayTweenSequenceComponent` | 依次**逐个**播放其数组中的 play tween（`bPlayNextWhenCycleComplete`、`OnComplete`） |

<a id="twenty-two-tweeners-nineteen-of-them-not-public-api"></a>

### 二十二个补间器，其中十九个不是公共 API

公开的门面是 `Public/DreamTweener.h` 中的**基类 `UDreamTweener`**。总共有 **22** 个：**1 个基类 + 19 个
具体实现 + 1 个 `Spring` + 1 个 `Sequence`**。按值划分，具体实现为 `Float`、`Double`、`Integer`、`Color`、
`LinearColor`、`Vector`、`Vector2D`、`Vector4`、`Quaternion`、`Rotator`、`RotationEuler`、`RotationQuat`、`Position`、
`MaterialScalar`、`MaterialVector`，外加无值的 `Frame`、`Callback`、`Update` 和 `Virtual`。

⚠️ **那 19 个具体补间的头文件在 `Private/Tweener/` 下** —— 它们**不是公共 API**：别把它们当作可以继承的类，也别直接 `#include`。公共门面只有基类 `UDreamTweener` 与 `UDreamTweenBPLibrary`。

`UDreamTweenerSequence` 把多个补间串成一条时轴：`Append` / `Insert` / `Prepend` / `Join`，加上 `AppendCallback` / `InsertCallback`。它**不接受 `Delay` / `DelayFrame` / `Virtual` 型补间**，并且**一旦开始就不能再改**。

<a id="springs-no-duration-and-a-retarget-keeps-the-velocity"></a>

### 弹簧：没有时长，重定目标保留速度

`UDreamTweenerSpring` 不按时钟推进，而是**按弹簧**推进：没有时长、没有缓动，值被拉向目标，**静止即完成**。它的参数是 `FDreamSpringParams` 的质量 / 刚度 / 阻尼，外加两个静止阈值（`RestDistance` / `RestVelocity`）。

`UDreamSpringLibrary` 还有两个**纯函数**，`StepSpring` 和 `IsSpringAtRest`，所以不架控件也能算出弹簧。

三点是它的全部价值：**重定目标保留速度**（`SetTarget` 可在飞行中随时调用，速度接着用 —— 一列歌词行滑向新位置而不是从头再来，靠的就是它）；**按阻尼收敛**（`DampingRatio()` < 1 回弹、= 1 临界、> 1 蠕动）；**轨迹与帧长无关**（`FDreamSpring::Step` 用阻尼谐振子的**闭式解**，任何步长都精确稳定，没有显式积分会在长帧上炸开）。

<a id="who-ticks-them"></a>

### 谁在 tick 它们

`DreamTween` 是一个独立的底层模块，不会被引擎自动 tick。驱动它的是 `UDreamTweenManager`，配合 `UDreamTweenTickHelperComponent`（有控件可挂时）或 `UDreamTweenTickHelperWorldSubsystem`（没有控件可挂时）。这也解释了上文那句「手动 tick 从不受限」——「手动」就是在这条链之外推进补间。

<a id="widget-animations"></a>

## 控件动画

`UDreamWidgetAnimation` 是嵌在类里的一个 `UMovieSceneSequence`，放在根控件的动画组件里（`UDreamWidgetAnimationComponent`），像其他动画一样有自己的类成员变量，在 Sequencer 里编辑。播放走 `UDreamUserWidget` 的入口，这些入口沿用 UMG 的名字：`PlayAnimation`、`PlayAnimationByName`、`PlayAnimationForward`、`PlayAnimationReverse`、`PlayAnimationTimeRange`、`QueuePlayAnimation`、`PauseAnimation`、`GetAnimationByName`、`BindToAnimationStarted` / `BindToAnimationFinished`、`FlushAnimations`。播放返回一个 `FDreamUIAnimationHandle`，可以安全地保留：实例结束后它就不再有效（`UDreamUIAnimationLibrary::IsAnimationHandleValid`）。

- **轨道能驱动什么**：标了 `Interp` 的属性。在控件上有 `RenderTranslation`、`RenderRotation`、`RenderScale`、`RelativeLocation`、`RelativeRotationEuler`（四元数没有属性轨道，所以旋转用这个欧拉镜像）、`RelativeScale`、`PerspectiveFieldOfView` 和 `PerspectiveOrigin`；视觉的 `Color`；文本的 paint 相位和 `PaintAngleOffset`。锚点那一块是结构体，结构体没有属性轨道，所以控件有一组可动画的镜像，在轨道上显示为 Width、Height、Anchor Left 等（`AnimatableWidth`、`AnimatableHeight`、`AnimatableAnchorLeft`……）。
- **事件轨道**：DreamUI Event 轨道上的关键帧在播放经过时通过 `OnAnimationEvent` 广播。
- **C++ 绑定**：`meta = (BindDreamWidgetAnim)` 声明类的代码要播一个同名动画，层级里没有就是编译错误；`BindDreamWidgetAnimOptional` 做同样的声明但不报错。
- **播放器**：只由普通属性轨道组成的动画由 DreamGUI 自己的播放器直接从通道求值，而不是走 Sequencer 的实体系统（`DreamUI.Animation.DirectEvaluation`、`DreamUI.Animation.LitePlayer`，默认都开）。

放进关卡的 `ADreamWorldWidgetActor` 还可以直接被 Level Sequence 占有，组件上的 `WidgetOpacity`、`WidgetOffset` 和 `bWidgetVisible` 都能打关键帧（见[世界空间 UI](world-space.md)）。

<a id="dui-timelines"></a>

## `.dui` timeline

`.dui` 里的 `timeline` 块是一个由文件拥有的控件动画。每行一条轨道：节点 id 组成的路径、它驱动的属性、关键帧（`time = value`，每个可带 `ease` 和缓动名）。`@time -> Name` 是事件轨道上的一个关键帧。

```dui
timeline Pulse {
    duration = 0.6
    loop     = PingPong

    Icon.RenderScale            : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1) ease InOutQuad, 0.6 = (1, 1, 1)
    Row/Title.RenderTranslation : 0.0 = (-40, 0, 0), 0.2 = (0, 0, 0) ease OutCubic
    @0.3 -> Landed
}

timeline Celebrate external

Widget Root {
    Image Icon { }
    Widget Row {
        Text Title { Text = "Ready" }
    }
}
```

块编译成根控件的动画组件里的一个 `UDreamWidgetAnimation`，和别的动画一样有成员变量、走同样的入口播放。因为文件拥有它，每次编译都会重建，**动画编辑器以只读方式打开它** —— 要改就改 `.dui`，或者写成 `timeline Name external` 把动画永久交给 Sequencer。轨道能驱动的正是动画编辑器提供的那些属性，缓动是 `EDreamTweenEase` 里的名字而不是切线。完整规则见 [`.dui` timeline](../language/timelines.md)。
