---
title: 动画
description: 三种动画方式：DreamTween 模块的补间（面板里的控件用 RenderOffsetTo、RenderScaleTo、RenderAngleTo、RenderTranslationTo，EDreamTweenEase，DreamTween.MaxStepSeconds），控件动画（UDreamWidgetAnimation 与 Sequencer），以及 .dui 里的 timeline。
---

DreamGUI 里让东西动起来有三种方式，各管一类事：

| 方式 | 适合 | 在哪 |
| --- | --- | --- |
| **补间** | 代码里临时起意的动：入场、悬停、数值变化 | `DreamTween` 模块，`UDreamTweener` |
| **控件动画** | 设计好的、可在 Sequencer 里编辑的动画 | `UDreamWidgetAnimation`，嵌在类里 |
| **`.dui` timeline** | 由文本拥有、每次编译重建的控件动画 | `.dui` 文件里的 `timeline` 块 |

<a id="补间"></a>

## 补间

`DreamTween` 是一个独立的底层模块。每个 `*To` 函数返回一个 `UDreamTweener`，可以链式设置：`SetEase`、`SetDelay`、
`SetLoop`（`Once`、`Restart`、`Yoyo`、`Incremental`，次数 `-1` 为无限）、`SetTimeScale`、`SetAutoKill`，以及回调 `OnStart`、`OnUpdate`、
`OnCycleStart`、`OnCycleComplete`、`OnComplete`、`OnKill`；控制用 `Pause`、`Resume`、`Restart`、`Goto`、`ForceComplete`、`Kill`。

控件、视觉和行为自带补间：

| 在哪 | 补间 |
| --- | --- |
| `UDreamWidget` | `AnchoredPositionTo`、`SizeDeltaTo`、`WidthTo`、`HeightTo`、`RenderOpacityTo`、`LocalPositionTo`、`LocalScaleTo`、`LocalRotatorTo`、`WorldPositionTo`……以及下一节的渲染变换补间 |
| `UDreamVisual` | `ColorTo`、`AlphaTo` |
| `UDreamTextPaintLibrary` | `PaintPhaseTo`、`PaintAngleTo`、`PlayShimmer`（见[文本](text.md)） |
| `UDreamTweenBPLibrary` | 通用的 `FloatTo`、`Vector3To`、`ColorTo`、`MaterialScalarParameterTo`、`DelayCall`、`SpringFloat`……也有给 UMG 控件用的 `UMG_*` 补间 |

缓动是 `EDreamTweenEase` 里的一个**名字**，整个插件共用这一张表：`Linear`，`In` / `Out` / `InOut` 加上 `Quad`、`Cubic`、`Quart`、`Sine`、
`Expo`、`Circ`、`Elastic`、`Back`、`Bounce`，以及用曲线的 `CurveFloat`（`SetCurveFloat`）。控件补间的默认缓动是 `OutCubic`，默认时长 0.5 秒。

几条容易踩到的规则：

- **完成和被杀掉的补间会退役**：句柄变为无效，持有它的 UPROPERTY 在下一次垃圾回收时被置空；对退役的补间 `Kill`、`ForceComplete`、`Goto`
  什么也不做，`Restart` 只警告。想在结束后重新播放，在开始前 `SetAutoKill(false)`，或者在它自己的 `OnComplete` 里 `Restart`（这会让它继续运行）。
- 弹簧（`SpringFloat`）默认在静止时保持；`SetTarget` 唤醒它。
- 主人已经不在的补间被丢弃，不调用回调。

<a id="暂停与时间膨胀"></a>

### 暂停与时间膨胀

控件的补间跟随项目设置：在屏幕上的 UI 看 `bScreenSpaceUIAffectByGamePause` 和 `bScreenSpaceUIAffectByTimeDilation`（默认都关），
其他的看 `bWorldSpaceUIAffectByGamePause` 和 `bWorldSpaceUIAffectByTimeDilation`（默认都开）。布局动画和嵌入的 UMG 控件遵守同一条规则。

`DreamTween.MaxStepSeconds`（控制台变量，`0` 为关，默认关）给一帧推进补间的步长设上限：比它长的一帧 —— 加载资源的界面、新文字的第一次绘制 ——
只把世界驱动的补间推进这么多。于是一个恰好在卡顿之前开始的入场动画会从原处接着播，而不是直接出现在终点。手动 tick 从不受限。

<a id="面板里的控件用渲染变换做动画"></a>

## 面板里的控件：用渲染变换做动画

面板会在下一轮把子控件的锚定位置和尺寸写回去，所以对一个面板放置的控件用 `AnchoredPositionTo` 或 `LocalPositionTo`，
动画和布局只会轮流改它。要动的应该是**画出来的样子**，而不是布局：

| 补间 | 动什么 |
| --- | --- |
| `RenderTranslationTo(FVector)` | 本地空间的平移：X 是深度、Y 向右、Z 向上 |
| `RenderOffsetTo(FVector2D)` | 同样的移动，但在画布平面上（x 向右、y 向上），深度不变 |
| `RenderScaleTo(FVector)` | 绕 `RenderTransformPivot` 缩放 |
| `RenderAngleTo(float)` | 画布平面内的角度（`SetRenderTransformAngle`） |

它们只动画法、从不动布局，兄弟也不会让位。渲染变换持续变化的控件会被画布做成渲染层，在 GPU 上移动（见[渲染](rendering.md)）。

```cpp
// Card 在一个 VerticalBox 里：动的是画出来的样子，不是它的槽
Card->SetRenderOpacity(0.0f);
Card->SetRenderTranslation(FVector(0.0, 0.0, -40.0));   // 比布局放的位置低 40
Card->RenderOpacityTo(1.0f, 0.25f);
Card->RenderOffsetTo(FVector2D::ZeroVector, 0.35f, 0.0f, EDreamTweenEase::OutCubic)
    ->OnComplete([]() { /* 落定 */ });

// 一直呼吸的图标
Icon->RenderScaleTo(FVector(1.15, 1.15, 1.0), 0.6f, 0.0f, EDreamTweenEase::InOutSine)
    ->SetLoop(EDreamTweenLoop::Yoyo, -1);
```

在 `.dui` 写的组件里也是一样：面板里的组件实例通过渲染变换来动。

<a id="可复用的补间playtween"></a>

## 可复用的补间：PlayTween

代码里临时起意的补间用 `*To`；要**存进资产、在细节面板里调、被组件驱动**的，用 `UDreamUIPlayTween`。
它是一条**播放器**而不是补间本体：把「怎么演」写成可序列化的字段，`Start()` 时才把 `Tweener` 造出来。

| 字段 | 默认 | 说明 |
| --- | --- | --- |
| `LoopType` | `Once` | |
| `LoopCount` | **1** | 从 `-1` 改过来的，见下 |
| `EaseType` / `EaseCurve` | `Linear` | 曲线仅 `CurveFloat` 时有效 |
| `Duration` | 0.5 | |
| `StartDelay` | — | |
| 四个事件 | — | `OnStart` / `OnUpdateProgress` / `OnComplete` / `OnCycleComplete` |
| `bAffectByGamePause` / `bAffectByTimeDilation` | **false** | **与控件自带补间相反**（UI 通常不被暂停拖住） |

入口是 `Start` / `Stop` / `GetTweener`。类型化子类共 **10 个**：`Float`、`Double`、`Color`、`Int`、`LinearColor`、
`Quaternion`、`Rotator`、`Vector2`、`Vector3`、`Vector4` —— 每个多一对 `From` / `To` 和一个 `OnUpdateValue(插值值)`。

`LoopCount` 默认从 `-1` 改成 **1** 是有意的：如果默认无限，一旦作者选了某个循环类型，补间就**永不结束** ——
从不离开管理器、拖住这个播放器和它背后的控件、等它的序列组件永远走不到下一步。

两个组件把它挂到树上（都是 `BlueprintSpawnableComponent`）：

| 组件 | 干什么 |
| --- | --- |
| `UDreamUIPlayTweenComponent` | `Awake` 时按需 `Play()` 一个 play tween（`bPlayOnStart` 默认 true） |
| `UDreamUIPlayTweenSequenceComponent` | 把数组里的 play tween **一个接一个**播（`bPlayNextWhenCycleComplete`、`OnComplete`） |

<a id="二十二个-tweener其中十九个不是公共-api"></a>

### 二十二个 Tweener，其中十九个不是公共 API

公共门面是 `Public/DreamTweener.h` 里的**基类 `UDreamTweener`**。它一共有 **22 个**：**基类 1 + 具体实现 19 + `Spring` 1 + `Sequence` 1**。
具体实现按值分是 `Float`、`Double`、`Integer`、`Color`、`LinearColor`、`Vector`、`Vector2D`、`Vector4`、`Quaternion`、`Rotator`、`RotationEuler`、`RotationQuat`、`Position`、`MaterialScalar`、`MaterialVector`，加上无值的 `Frame`、`Callback`、`Update`、`Virtual`。

⚠️ **那 19 个具体实现的头在 `Private/Tweener/` 下** —— 它们**不是公共 API**：别当成可继承或可直接 `#include` 的类。公共门面只有基类 `UDreamTweener` 与 `UDreamTweenBPLibrary`。

`UDreamTweenerSequence` 把多个补间串成一条时轴：`Append` / `Insert` / `Prepend` / `Join` 加 `AppendCallback` / `InsertCallback`；**不支持 `Delay` / `DelayFrame` / `Virtual` 型**，且**开始后不能再改**。

<a id="弹簧没有时长重定目标保留速度"></a>

### 弹簧：没有时长，重定目标保留速度

`UDreamTweenerSpring` 不按时钟推进，而是**按弹簧**推进：没有时长、没有缓动，值被拉向目标，**静止即完成**。参数是 `FDreamSpringParams` 的质量 / 刚度 / 阻尼加两个静止阈值（`RestDistance` / `RestVelocity`）。

另有 `UDreamSpringLibrary` 的 `StepSpring` / `IsSpringAtRest` 两个**纯函数**，不架控件也能算。

三点是它的全部价值：**重定目标保留速度**（`SetTarget` 可在飞行中随时调、速度接着用 —— 一列歌词行滑向新位置而不是从头再来，靠的就是它）；**按阻尼收敛**（`DampingRatio()` < 1 回弹、= 1 临界、> 1 蠕动）；**轨迹与帧长无关**（`FDreamSpring::Step` 用阻尼谐振子的**闭式解**，任何步长都精确稳定，没有显式积分会在长帧上炸开）。

<a id="谁在-tick-它们"></a>

### 谁在 tick 它们

`DreamTween` 是一个独立的底层模块，不会被引擎自动 tick。驱动它的是 `UDreamTweenManager`，配合 `UDreamTweenTickHelperComponent`（挂在控件上时）或 `UDreamTweenTickHelperWorldSubsystem`（没有控件可挂时）。这也解释了上文那句「手动 tick 从不受限」——「手动」就是这条链之外的推进方式。

<a id="控件动画"></a>

## 控件动画

`UDreamWidgetAnimation` 是嵌在类里的一个 `UMovieSceneSequence`，放在根控件的动画组件里（`UDreamWidgetAnimationComponent`），
像其他动画一样有自己的类成员变量，在 Sequencer 里编辑。播放走 `UDreamUserWidget` 上和 UMG 同名的入口：`PlayAnimation`、`PlayAnimationByName`、
`PlayAnimationForward`、`PlayAnimationReverse`、`PlayAnimationTimeRange`、`QueuePlayAnimation`、`PauseAnimation`、`GetAnimationByName`、
`BindToAnimationStarted` / `BindToAnimationFinished`、`FlushAnimations`。播放返回一个 `FDreamUIAnimationHandle`，可以安全地保留：
实例结束后它就不再有效（`UDreamUIAnimationLibrary::IsAnimationHandleValid`）。

- **轨道能驱动什么**：标了 `Interp` 的属性。在控件上有 `RenderTranslation`、`RenderRotation`、`RenderScale`、`RelativeLocation`、
  `RelativeRotationEuler`（四元数没有属性轨道，所以旋转用这个欧拉镜像）、`RelativeScale`、`PerspectiveFieldOfView`、`PerspectiveOrigin`；
  视觉的 `Color`；文本的 paint 相位和 `PaintAngleOffset`。锚点那一块是结构体，结构体没有属性轨道，所以控件有一组可动画的镜像，
  在轨道上显示为 Width、Height、Anchor Left 等（`AnimatableWidth`、`AnimatableHeight`、`AnimatableAnchorLeft`……）。
- **事件轨道**：DreamUI Event 轨道上的关键帧在播放经过时通过 `OnAnimationEvent` 广播。
- **C++ 绑定**：`meta = (BindDreamWidgetAnim)` 声明类的代码要播一个同名动画，层级里没有就是编译错误；`BindDreamWidgetAnimOptional` 不报错。
- **播放器**：只由普通属性轨道组成的动画由 DreamGUI 自己的播放器直接从通道求值，而不是走 Sequencer 的实体系统
  （`DreamUI.Animation.DirectEvaluation`、`DreamUI.Animation.LitePlayer`，默认都开）。

放进关卡的 `ADreamWorldWidgetActor` 还可以直接被 Level Sequence 占有，组件上的 `WidgetOpacity`、`WidgetOffset`、`bWidgetVisible`
都能打关键帧（见[世界空间 UI](world-space.md)）。

<a id="dui-里的-timeline"></a>

## `.dui` 里的 timeline

`.dui` 文件里的 `timeline` 块是一个由文件拥有的控件动画。每行一条轨道：节点 id 组成的路径、它驱动的属性、关键帧（`时间 = 值`，可带
`ease` 和缓动名）。`@时间 -> 名字` 是事件轨道上的一个关键帧。

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

块编译成根的动画组件里的一个 `UDreamWidgetAnimation`，和别的动画一样有成员变量、走同样的入口播放。因为文件拥有它，每次编译都会重建，
**动画编辑器以只读方式打开它** —— 要改就改 `.dui`，或者写成 `timeline Name external` 把它永久交给 Sequencer。轨道能驱动的正是动画编辑器提供的那些
属性，缓动是 `EDreamTweenEase` 里的名字而不是切线。完整规则见 [`.dui` timeline](../language/timelines.md)。
