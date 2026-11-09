---
title: Niagara 粒子
description: NiagaraSystem 视觉——由一个不进场景的组件模拟、由画布绘制的 Niagara 系统，每个 Sprite / Ribbon 渲染器一个绘制调用，像其它元素一样裁剪、遮罩、混合、编组；它的空间、时间、着色、注入的参数、爆发与飞行、触发器，以及它不画什么。
---

`UDreamNiagaraSystem`（`NiagaraSystem` 视觉，位于 `DreamGUINiagara` 模块）把一个 Niagara 系统放到控件上。元素持有一个
Niagara 组件，注册到所在世界、但不挂到任何东西上：组件只**模拟**，场景里不给它建代理（它的
`CreateSceneProxy` 不返回任何东西），所以它的任何部分都不会画进世界，连激活的那一帧也不会；也不受 Niagara
可伸缩性管理（`SetAllowScalability(false)`），所以特效不会因为“没被看见”而被剔除。画布每帧发送网格段之前，元素等模拟的
tick 结束，按渲染器的属性绑定读取每个渲染器的粒子，写进自己的网格段。从这里开始粒子就是 DreamGUI 的几何：像任何元素一样
被裁剪、遮罩、混合、放进图层组。

```text
NiagaraSystem Sparks {
    System        = /DreamGUI/Samples/Niagara/NS_Sample_ClickSparks
    bAutoActivate = false
}
```

`Content/Samples/NiagaraEffects.dui` 演示五个效果：点击火花、指针拖尾、悬停按钮边框冒火星、进度条满格爆发、金币飞向计数器。

## 画什么

| Niagara | DreamGUI |
| --- | --- |
| CPU 模拟的发射器 | 读取 |
| GPU 模拟的发射器、轻量（无状态）发射器 | 不画，报告 |
| Sprite 渲染器（粒子精灵） | 一个四边形网格段 |
| Ribbon 渲染器 | 一个条带网格段 |
| Mesh、Light、Component、Decal 渲染器 | 不画，报告 |

每个被画的渲染器是一个**网格段**：独立的段和绘制调用，按 Niagara 的渲染器顺序（排序提示，然后是发射器和渲染器的位置）。
细节面板列出每个渲染器——画成什么、贴图和混合方式，或为什么不画；同一份报告也可以用 `GetRendererInfo()` 取得。

**Sprite** 按 Niagara 自己的 sprite 顶点工厂的放法写顶点，平铺在控件上：尺寸围绕轴心（`PivotInUVSpace`，或粒子的轴心偏移），
按旋转顺时针转，按速度或自定义对齐方向对齐，UV 缩放，以及图集（`SubImageSize`）的帧——渲染器开启子图混合时还有向下一帧的
混合。排序：不排，或按自定义排序属性升序/降序；按视图排序在平面 UI 上没有深度可排，按自定义属性升序处理。

**Ribbon** 按 ribbon id 分条、按 link order 排列（没有时按年龄，最新的在前），沿 Catmull-Rom 曲线（渲染器曲线张力的
基数样条）按渲染器的细分要求切段（常数因子，或按弯曲程度需要的段数），两套 UV（按点拉伸、按长度拉伸或平铺）以及逐粒子的
U 覆盖值。非平面形状的 ribbon 画成面向观看者的平面；UV 边缘的平滑过渡画成锁定边缘。

顶点写进段持有的数组，分级扩容（64，然后 2 的幂）、从不缩小；粒子够多时在工作线程上并行写。段的粒子数留在当前级别时，
GPU 上的缓冲原地更新，不重建。一个绘制调用最多索引 65,535 个顶点（16,383 个 sprite），超出的部分不画，只警告一次。

## 空间

模拟的单位就是画布的单位：一个单位 = `PixelsPerUnit` 个画布单位（默认 1），在 `Plane` 指定的平面上。**XZ**（默认）：
模拟的 X 是控件的右、Z 是上，所以按 -Z 重力做的特效会往屏幕下方落；**XY**：Y 是上，Z 垂直屏幕向外。`bFlattenDepth`
（默认开）把所有粒子画在控件平面上，不管模拟把它们推离平面多远。

组件在模拟世界里站在控件在画布上的位置。**本地空间**发射器的粒子跟着控件走；**世界空间**发射器的粒子留在画布上生成的
位置，控件移动时拖出尾迹。`SimToCanvas` 和 `CanvasToSim` 在两者之间换算。

## 时间

| 属性 | |
| --- | --- |
| `bAutoActivate` | 控件注册时自动开始 |
| `bPaused` / `SetPaused` | 停在当前时刻 |
| `TimeDilation` | 模拟时间相对世界时间的快慢 |
| `bTickWhenGamePaused` | 游戏暂停时照跑，UI 一般需要（默认开） |
| `FixedTimeStep` | 用这么多秒的固定步长代替帧间隔，一帧最多 8 步：任何帧率下粒子都一样 |
| `WarmupTime` | 开始时先跑这么久 |
| `Advance(Seconds, Step)` | 立刻往前跑，不管是否暂停：预热、拖动时间、测试 |

在游戏世界里、没有固定步长时，组件自己 tick（solo 模式，带自己的时间缩放和暂停）。有固定步长时，以及在不 tick 组件的世界里
——设计器预览、关卡编辑器——元素在画布绘制时自己推进模拟，所以设计器能预览特效。模拟只在应用能渲染的地方运行：
`-nullrhi` 下 Niagara 不运行任何系统。

## 着色

每个渲染器按两种方式之一着色（`DefaultShading`，或渲染器自己的覆盖）：

- **内置着色**（默认）。DreamGUI 自己的着色器，不用材质。贴图取粒子材质的 `Texture` 参数，否则第一个有值的贴图参数，否则
  材质图里第一个采样的贴图；一个都不采样的材质——引擎默认的 sprite、ribbon 材质是程序化的——用一张柔和圆点（ribbon 用柔和
  色带）。混合方式：加法材质用 Additive，调制材质用 Multiply，其它用 Alpha。都可以改：`bOverrideBlendMode` 和 `BlendMode`
  作用于所有渲染器（蓝图里用 `SetBlendModeOverride`），或单个渲染器的覆盖（`RendererOverrides`：发射器名、渲染器序号、着色、贴图、混合模式、材质）。粒子颜色
  可以亮于 1：顶点里存的是除以一个系数后的颜色，着色器再乘回去。所有混合模式都可用，包括读背景的模式（见
  [混合模式与图层组](blend-modes-and-groups.md)）。加法绘制和任何元素一样不动身后的 alpha：隔离图层组里只有加色内容时，用
  按 alpha 加权的方式（比如 Multiply）合成会什么都看不到——这种粒子请用 Alpha 混合。
- **材质**。一个 UI 材质：覆盖里的，否则 `MaterialMap` 为该粒子材质给出的那个。细节面板的 **Convert Particle Materials**
  会生成它们：每个被画渲染器的粒子材质复制到它旁边，命名 `<名字>_DreamUI`（引擎或插件里的材质放进
  `/Game/DreamUI/ConvertedParticleMaterials`），保存，填进映射表，并把着色设为 Material。复制出的是表面域、无光照、双面的
  材质，保留原混合方式（不透明的改为半透明），粒子输入改读 DreamGUI 写的数据，不透明度经过 DreamGUI 的裁剪：

  | 粒子材质里 | 转换后 |
  | --- | --- |
  | Particle Color | 顶点色转线性，乘以它的系数 |
  | Particle SubUV | 当前帧和下一帧，混合 |
  | Dynamic Parameter | 顶点里存放它的空余纹理坐标 |
  | 纹理坐标 1 | 纹理坐标 2（1 是控件记录用的） |
  | Opacity | 乘以 `MF_DreamWidgetProperty` 接 `MF_DreamUI_Clip`；加法材质的自发光也乘 |

  转换不了的会列出来并说明原因：其它粒子属性（相对时间、随机数、尺寸、速度等）和读场景的节点（深度渐隐、场景深度、
  场景纹理）原样保留，按默认值画。材质实例的转换方式是先转换它的父材质，再复制实例、把父材质换成转换结果。读 DreamGUI
  控件数据的材质需要由 DreamGUI 的渲染器来画。

手写材质时的顶点布局：UV0 当前帧（ribbon 的第一套 UV），UV1.x 控件记录、UV1.y 动态参数 x，UV2 下一帧（ribbon 的第二套 UV），
UV3.x 颜色系数和帧混合打包（`floor` 是系数×16，小数部分是混合×255/256），UV3.y 动态参数 y，UV4 动态参数 z、w。
`Shaders/Private/DreamUIParticle.ush` 和 `DreamNiagaraVertexLayout.h` 定义同一组常量。

## 系统被告知什么

系统里声明了下列用户参数的，每帧在它 tick 之前写入（`bWriteUIParameters`）：

| 参数 | 类型 | |
| --- | --- | --- |
| `User.DreamUI_WidgetSize` | Vector2（或 Vector） | 控件尺寸，模拟单位 |
| `User.DreamUI_PointerPosition` | Position（或 Vector） | 指针位置，模拟世界坐标 |
| `User.DreamUI_PointerInside` | bool（或 float） | 指针是否在控件上 |
| `User.DreamUI_TargetPosition` | Position（或 Vector） | 飞行的目标（见下） |

`SetPointerPositionOverride` 可以代替真实指针。其它参数用 `SetVariableFloat`、`SetVariableInt`、`SetVariableBool`、
`SetVariableVec2`、`SetVariableVec3`、`SetVariableLinearColor`、`SetVariableObject` 设置，`GetNiagaraComponent()` 返回组件本身。

## 爆发与飞行

- `BurstAt(本地坐标)` 在控件上的一点再生成一次系统，和正在运行的那个并存；它的粒子画在系统的网格段里，直到消失。
- `BurstTo(目标, 本地坐标)` 生成**飞行系统**——`FlightSystem`，否则用插件自带的模板 `/DreamGUI/Niagara/NS_DreamUI_FlyTo`——
  粒子飞向目标控件的中心，每帧写入 `User.DreamUI_TargetPosition`，目标移动也能追上。它的渲染器是独立的网格段，排在系统
  的后面。只做飞行的控件不需要 `System`。
- `OnBurstFinished(BurstId)` 在一次爆发的粒子全部消失时通知。

模板的源文件是 `DFX/Niagara/NS_DreamUI_FlyTo.dfs`：几个光点先散开，再沿弧线扫向目标并缩小消失。复制它做自己的飞行效果；
保留参数名和世界空间。

## 触发器

`UDreamNiagaraTrigger`（**Niagara Trigger** 组件）在控件的指针事件上播放系统，不需要蓝图。挂在接收指针的控件上；系统是
`SystemWidget` 的视觉，否则是本控件或它下面第一个有 Niagara 视觉的控件。

| 属性 | |
| --- | --- |
| `OnClick`、`OnEnter`、`OnExit` | 在指针处爆发、在中心爆发、飞向目标、激活、停止、重新开始，或什么也不做 |
| `FlightTarget` / `FlightTargetName` | 飞行目标，直接引用或按同一棵树里的显示名查找 |
| `ProgressWidget` / `ProgressWidgetName` | 一个有 float `Percent` 的控件（进度条）：到 1 时执行 `OnProgressFull` |
| `ClickAddsProgress` | 每次点击给进度加多少：蓄力条 |

代码里用 `Fire(Action)` 执行一个动作。

## 编辑器

调色板在 Effects 下列出 **Niagara System**，在 Components 下列出 **Niagara Trigger**。细节面板显示渲染器报告和转换按钮。
设计器能预览系统：预览绘制时推进模拟。

## 暂不支持

GPU 模拟、Mesh 渲染器和轻量发射器会被报告并跳过。走材质的网格段保留材质自己的混合方式，只有内置着色器使用 `BlendMode`。
读场景的发射器（碰撞、深度缓冲）读的是模拟世界，也就是画布，不是关卡。

*参考：设计时阅读了 SourySK/NiagaraUIRenderer 和 liufei2008/LGUI_ParticleSystem（均为 MIT）；代码是 DreamGUI 自己写的。*
