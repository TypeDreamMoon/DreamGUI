---
title: "画布"
description: "UDreamCanvas 做什么——把子树合批为绘制调用——包括根画布与嵌套画布、四种渲染模式、缩放与投影、排序顺序、逐控件的透视需要什么、按需创建的屏幕根，以及 DreamUI.Stats 测量什么。"
---

画布（`UDreamCanvas`）是把控件树变成画面的东西。它是一种**行为**——某个控件上的 `UDreamUIBehaviour`
——它为其下方的整棵子树负责：它收集每个视觉元素的几何，按层级顺序将其合批为尽可能少的绘制调用，并把这些交给渲染器。没有画布在其上方的控件不会被绘制。

画布不会每帧更新，只在需要时更新。三类变化可以重建其绘制调用：材质或纹理变化，或控件的激活状态变化；变换或顶点位置变化，导致绘制调用可能重叠；以及层级顺序变化，也就是绘制顺序。即便如此，它也会检查之前的绘制调用是否可以复用。更新只遍历那些请求变化的控件，对几何发生变化的区段进行原地修补而非重建，并在控件只是移动时保留绘制调用。

<a id="root-and-nested-canvases"></a>

## 根画布与嵌套画布

一棵树中最顶层的画布是它的**根画布**；DreamGUI 的更新从那里开始，一路向下。树中更深的画布是嵌套画布：

- 嵌套画布继承根画布的渲染模式（`GetActualRenderMode`）和渲染目标（`GetActualRenderTarget`）。
  `SetRenderMode` 只在根画布上生效。
- `OverrideParameters` 是一个位掩码（`DefaultMaterial`、`RequireNormalAndTangent`、`BlendDepth`、`DepthFade`），表示嵌套画布从中哪些从其父级取得，哪些自行设置。
- 开启 `bOverrideSorting` 时，它使用自己的 `SortOrder`；否则按层级顺序排序。
- `bForceRenderToTarget` 使得该画布无论根的模式是什么都渲染到纹理——这会断开它与上方画布的链接，并使其成为自己的根。

嵌套画布绘制到其父级的表面上，而画布不是裁剪器，因此它自己的矩形不能说明什么可见。`bCullElementsOutsideCanvas`（完全省略画布矩形之外的元素；默认开启）因此只作用于根画布或被强制渲染到渲染目标的画布。

<a id="render-modes"></a>

## 渲染模式

`RenderMode`（`EDreamRenderMode`）决定根画布绘制到哪里以及由谁绘制：

| 模式 | 编辑器中的名称 | 由谁绘制 | 说明 |
| --- | --- | --- | --- |
| `ScreenSpaceOverlay` | Screen Space Overlay | DreamGUI 的渲染器 | 在屏幕上；同一世界中的多个屏幕空间根按 `SortOrder` 排序 |
| `WorldSpace_DreamUI` | World Space - DreamUI Renderer | DreamGUI 的渲染器 | 在关卡中，不受后处理影响 |
| `WorldSpace` | World Space - UE Renderer | 引擎的管线 | 在关卡中，作为接受光照、雾和后处理的普通图元 |
| `RenderTarget` | Render Target | DreamGUI 的渲染器 | 渲染到 `UTextureRenderTarget2D` |

在两种世界空间模式之间选择，以及放置和点击此类画布，请参阅
[世界空间 UI](world-space.md)；两种渲染器的比较在
[渲染](rendering.md)中。

<a id="screen-space-scaling-and-projection"></a>

## 屏幕空间：缩放与投影

屏幕空间根从本地玩家的视口获取尺寸，画布缩放器属性（类别 `DreamGUI-CanvasScaler`）将其转换为根控件的尺寸：

| 属性 | 作用 |
| --- | --- |
| `ScaleMode` | `ConstantPixelSize`（1 单位是 1 屏幕像素；默认）、`ScaleWithScreenSize`（按参考分辨率缩放）、`ScaleWithEngineDPI`（UMG 的规则：引擎的 UI Scale Rule 和 DPI 曲线）、`Custom`（询问 `CustomScale`——见下文） |
| `ReferenceResolution` | 参考分辨率，默认 `(1280, 720)` |
| `MatchFromWidthToHeight`（显示为 Match） | 在 `MatchWidthOrHeight` 下，匹配宽度还是高度 |
| `ScreenMatchMode` | `MatchWidthOrHeight`、`Expand`、`Shrink` |
| `ScreenSpaceRenderScale` | 以视口分辨率的这一比例绘制 UI 并放大；低于 1 用清晰度换取填充率 |
| `bFixedSizeInEditMode` / `SizeInEditMode` | 在编辑模式下使用固定尺寸，而不是编辑器视口的尺寸 |

`Custom` 将这项工作交给一个**自定义缩放对象**（`UDreamCanvasCustomScale`）：两个虚函数 `Init` 和
`CalculateSizeAndScale` 计算画布尺寸和缩放，而 `ConvertPositionFromViewportToCanvas` /
`ConvertPositionFromCanvasToViewport` 双向转换位置。

它转换的位置是**画布所填充的视口部分**上的像素位置（整个视口，或分屏中某个玩家的区域，左上角为零）。**画布已经考虑了该区域的角**，因此该对象从**其自己区域的角**开始度量。这些函数**只在根画布上成立**；非根画布返回失败。

在 `ScaleWithEngineDPI` 下，布局矩形在任何分辨率下都保持曲线的设计尺寸，因此为其编写的布局永远不会因空间不足而被裁剪——它只会渲染得更小——而调整 DPI 曲线的项目会让 UMG 和 DreamUI 一起移动。

画布有一个虚拟相机。`ProjectionType`（默认 `Perspective`）和 `FieldOfView`（水平，默认 60 度）定义它：眼睛位于 `Width * 0.5 / tan(FOV / 2)`，即画布矩形恰好填满画面的距离。更宽的视场会让眼睛更近，并加深其下方每个透视作用域。`NearClipPlane` 和
`FarClipPlane` 是高级属性；`SetProjectionParameters` 在运行时设置全部四项。

<a id="perspective-per-widget"></a>

## 逐控件的透视

控件上的 `bPerspective` 会像 CSS 的 `perspective` 那样，使其后代朝向一只眼睛产生透视缩短（这些属性在
[控件模型](widgets.md)上）。它需要的是**具有透视投影的屏幕空间画布**，
否则不起作用：正交画布的眼睛在无穷远处，任何仿射映射都无法到达那里。这就是为什么
`ProjectionType` 和 `FieldOfView` 特意不是高级属性——找不到它们的作者就无法
校准该功能。

它开销低，是因为画布**已经**通过透视投影绘制。子树不需要自己的投影或通道；其几何被重新瞄准，使画布的投影产生子树自己的眼睛本会看到的画面。重新瞄准是一个普通的仿射映射：不改变顶点格式、不改变着色器、也不增加绘制调用。嵌套作用域从最内层向外层依次交接，只有最外层映射到画布的眼睛。

<a id="sort-order"></a>

## 排序顺序

- `SortOrder` 存储为 `int16`，从 -32768 到 32767；较大的值绘制在较小的值之上。嵌套画布只有在 `bOverrideSorting` 开启时才使用自己的值。
- `SetSortOrder(Value, PropagateToChildrenCanvas)` 在传播时保持子画布相对于它的顺序；
  `SetSortOrderToHighestOfHierarchy` 和 `SetSortOrderToLowestOfHierarchy` 将其置于同一层级的所有画布之上或之下。
- 屏幕页面：`AddToViewport` 的 `InSortOrder` 对页面之间进行排序。1000（`StackBaseSortOrder`）及以上
  保留给页面栈，页面栈在每次刷新时重写其页面的排序顺序；放在该区间的独立页面相对栈的绘制顺序未定义，因此请求这样做会记录一条警告。
- 在世界空间画布之间，`UDreamWorldWidgetComponent::SortOrder` 被写入到根画布。

<a id="render-target-canvases"></a>

## 渲染目标画布

在 `RenderTarget` 模式下，画布绘制到 `RenderTarget` 中，或在未分配时绘制到它为自己创建的目标中
（`AutoRenderTarget`，从不保存或复制）。

| 属性 | 作用 |
| --- | --- |
| `RenderTargetClearColor` | 清屏颜色，默认透明 |
| `RenderTargetUpdateMode` | `Automatic`（有变化时才画；默认）、`Always`（每帧）、`WhenRequest`（只在 `RequestUpdateForRenderTarget` 时） |
| `RenderTargetSizeMode` | `RenderTargetFitToCanvas`（目标跟着画布；默认）、`CanvasFitToRenderTarget`（反过来）、`None` |
| `RenderTargetResolutionScale` | 目标跟着画布时，目标的分辨率缩放倍数 |

渲染目标画布由**它自己的一个渲染命令**来画，在它的网格段变化之后执行，而不是在它所在世界的某个视图里——
所以不管有没有东西在渲染它的世界，它都会更新。要显示画出来的内容，可以在另一棵树里放一个
`CanvasRenderTargetPreviewer` 视觉（`SetPreviewCanvas` 指定画布），或者用
`UDreamUIRenderTargetGeometrySource` 在关卡里为它生成几何——一个 `Plane`、一个 `Cylinder`，或它
映射到的 `StaticMesh`——再配一个 `UDreamUIRenderTargetInteraction` 让它能被点击。

<a id="the-screen-root-made-on-demand"></a>

## 屏幕根：按需创建

不需要在关卡里放任何东西。屏幕上的 UI 挂在一个屏幕空间根画布下，由 `UDreamScreenUISubsystem` **每个本地玩家一个**，
并在第一次需要时创建：

- `UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, WidgetClass)` 创建一个 Widget Blueprint 实例并把它
  放到屏幕上；它的返回值形状跟着类走，所以不需要 Cast；
- `UDreamScreenUISubsystem::AddToViewport`、`AddToPlayerScreen`、`CreateWidgetOnScreen` 和 `RemoveFromViewport` 做
  同样的事，只是指定了玩家；
- `GetOrCreateScreenRoot` 和 `UDreamUIBPLibrary::GetOrCreateScreenSpaceUIRoot` 直接把根交给你。

分屏是常态而不是一种模式：玩家 1 和玩家 2 各有自己的根画布、自己带着 `UserIndex` 的屏幕空间射线检测器
和自己的页面栈，所以一个人推上来的全屏页面盖不住另一个人。和 UMG 不同的是这里没有共享层可退：`AddToViewport` 对控件所属的
玩家来说就是 `AddToPlayerScreen`。射线检测器和事件系统同样按需创建（见[输入](input.md)）。

2.0 之前放进关卡的根 Blueprint（`ScreenSpaceRoot`、`WorldSpaceRoot_DreamRenderer`、
`WorldSpaceRoot_UERenderer`）已经删除，没有重定向；见[迁移](../guides/migration.md)。

<a id="other-canvas-properties"></a>

## 其他画布属性

| 属性 | 作用 |
| --- | --- |
| `DefaultMaterial` | 画默认 UI 元素用的材质 |
| `bRequireNormalAndTangent` | 顶点数据里带法线和切线 |
| `bEnableDepthTest` | 建一张深度纹理做深度测试，给用不透明材质的 `StaticMesh` 用；只对屏幕空间和渲染目标模式有效 |
| `BlendDepth` / `DepthFade` | 只用于 World Space - DreamUI Renderer：被场景深度遮挡的程度，以及深度渐隐 |
| `TraceChannel` | 交互射线命中世界空间 UI 所用的通道 |
| `bAllowDropFrame` | draw call 太耗时时允许画布跳掉一帧（高级选项） |
| `DefaultMeshType` | 用自定义的 `UDreamUIMeshComponent` 子类来画（高级选项） |

`SetDrawCallRebuildSuspended(true)` 原样保留画布的 draw call 列表。顶点刷新照常运行——移动或变色的控件继续更新——期间请求的重建
会在解除的那一刻发生。这就是 Slate 的 `UInvalidationBox` 对缓存 draw 元素做的事，如今为 DreamGUI 的 draw call
而做，由 `UDreamInvalidationBox` 行为驱动它。

<a id="what-dreamuistats-measures"></a>

## DreamUI.Stats 量的是什么

控制台命令 `DreamUI.Stats` 打印自上一次 `DreamUI.Stats` 以来每帧的平均开销，然后重新计数。计数一直开着——
每个阶段两端各读一次时钟、一次原子加，每个根画布每帧几次——所以基准测试读到的、这个命令打印的、
Unreal Insights 里看到的是同一批帧。总数是**整个进程**的，不是某个画布或某个世界的：一帧的开销就是这么多。

| 阶段 | 线程 | 是什么 |
| --- | --- | --- |
| `ManagerTick` | 游戏线程 | UI 管理器的 tick：行为、布局、剪裁，以及下面每个根画布的更新 |
| `CanvasUpdate` | 游戏线程（在 `ManagerTick` 之内） | 根画布及其子画布把控件、剪裁和几何更新到最新 |
| `Batching` | 工作线程 | 用游戏线程备好的几何拼画布的 draw call |
| `DrawCallSubmit` | 游戏线程 | 把拼好的 draw call 放进网格段和材质，发给渲染线程 |
| `RenderRecord` | 渲染线程 | 把 UI 的 pass 录进这一帧的渲染图 |

`CanvasUpdate` 在 `ManagerTick` 之内，两者不能相加；`Batching` 和 `RenderRecord` 跑在游戏线程
旁边，而不是之内。每个阶段同时也是一个 Insights 里的 CPU 追踪作用域，名字是 `DreamUI_` 加阶段名。

计数器则说明做了多少事：录制的批次数和顶点数（`BatchesRecorded`、`VerticesRecorded`）、上传的网格段和
字节数（`SectionUploads`、`UploadedBytes`）、重建了 draw call 的画布数（`DrawCallRebuilds`）、
只移动而原地刷新的画布数（`InPlaceRefreshes`）、只打补丁的网格段（`SectionPatches`）、原样沿用的网格段
（`SectionReuses`）、画布更新看过的控件数（`WidgetsUpdated`），以及渲染层的移动、提升和撤销
（`RenderLayerMoves`、`RenderLayerPromotions`、`RenderLayerDemotions`）和文本重绘（`TextPaints`）。

`stat DreamGUI` 仍然显示它的计数器；`DreamGUI.Memory` 列出每个世界的画布网格段和 paint 行。
抓图、性能分析和基准测试的完整做法见[调试](../tools/debugging.md)和
[基准测试](../tools/bench.md)。
