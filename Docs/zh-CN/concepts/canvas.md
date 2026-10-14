---
title: 画布
description: UDreamCanvas 做什么：把一棵子树合批成 draw call；根画布与嵌套画布、四种渲染模式、缩放与投影、排序、每控件透视的前提、按需创建的屏幕根，以及 DreamUI.Stats 量的是什么。
---

画布（`UDreamCanvas`）是让一棵控件树变成画面的东西。它是一个**行为** —— 挂在某个控件上的 `UDreamUIBehaviour` ——
负责它下面的整棵子树：收集每个视觉的几何，按层级顺序合批成尽量少的 draw call，交给渲染器。没有画布在上方的控件不会被画出来。

画布不是每帧都更新，只在需要时更新。会触发 draw call 重建的，基本只有三类变化：材质或纹理变了、控件的激活状态变了；
变换或顶点位置变了，导致 draw call 之间可能重叠；层级顺序变了（它直接决定绘制顺序）。即使要重建，也会先检查能否沿用上一次的
draw call。更新时画布只走那些请求了变化的控件，几何变了的网格段原地打补丁而不是重建，控件只是移动时保留原有的 draw call。

<a id="根画布与嵌套画布"></a>

## 根画布与嵌套画布

一棵树里最上面的画布是**根画布**，DreamGUI 的更新从根画布开始，一路走到它下面的每个元素。树里更深处还可以有画布，
它们是嵌套画布：

- 嵌套画布默认继承根画布的渲染模式（`GetActualRenderMode`）和渲染目标（`GetActualRenderTarget`）。
  `SetRenderMode` 只在根画布上才真正生效。
- `OverrideParameters` 是一个位掩码（`DefaultMaterial`、`RequireNormalAndTangent`、`BlendDepth`、`DepthFade`），
  决定嵌套画布在这几项上用自己的值还是继承父画布的。
- `bOverrideSorting` 打开后，它用自己的 `SortOrder`；否则按层级顺序排。
- `bForceRenderToTarget` 让这个画布不管根画布是什么模式都画进一张渲染目标 —— 这会断开它与上级的链接，让它自己成为根画布。

嵌套画布画进父画布的表面，而画布不是剪裁器，所以它自己的矩形说明不了什么可见。`bCullElementsOutsideCanvas`
（剔除完全在画布矩形外的元素，默认开）因此只在根画布或强制渲染到目标的画布上起作用。

<a id="渲染模式"></a>

## 渲染模式

`RenderMode`（`EDreamRenderMode`）决定根画布画到哪里、由谁来画：

| 模式 | 编辑器里的名字 | 谁来画 | 说明 |
| --- | --- | --- | --- |
| `ScreenSpaceOverlay` | Screen Space Overlay | DreamGUI 自己的渲染器 | 画在屏幕上；同一世界里有多个屏幕空间根时按 `SortOrder` 排 |
| `WorldSpace_DreamUI` | World Space - DreamUI Renderer | DreamGUI 自己的渲染器 | 放在关卡里，不受后处理影响 |
| `WorldSpace` | World Space - UE Renderer | 引擎的渲染管线 | 放在关卡里，作为普通图元参与光照、雾、后处理 |
| `RenderTarget` | Render Target | DreamGUI 自己的渲染器 | 画进一张 `UTextureRenderTarget2D` |

两种世界空间模式的取舍、交互和摆放见[世界空间 UI](world-space.md)；两种渲染器的区别见[渲染](rendering.md)。

<a id="屏幕空间缩放与投影"></a>

## 屏幕空间：缩放与投影

屏幕空间的根画布从本地玩家的视口取尺寸，再由"画布缩放器"那组属性（`DreamGUI-CanvasScaler` 分类）换算出根控件的尺寸：

| 属性 | 作用 |
| --- | --- |
| `ScaleMode` | `ConstantPixelSize`（1 单位 = 1 屏幕像素，默认）、`ScaleWithScreenSize`（按参考分辨率缩放）、`ScaleWithEngineDPI`（UMG 的规则：用引擎的 UI Scale Rule 和 DPI 曲线）、`Custom`（交给 `CustomScale` —— 见下） |
| `ReferenceResolution` | 参考分辨率，默认 `(1280, 720)` |
| `MatchFromWidthToHeight`（显示为 Match） | `MatchWidthOrHeight` 下按宽还是按高匹配 |
| `ScreenMatchMode` | `MatchWidthOrHeight`、`Expand`、`Shrink` |
| `ScreenSpaceRenderScale` | 以视口的几分之几的分辨率画 UI，再放大；低于 1 用清晰度换填充率 |
| `bFixedSizeInEditMode` / `SizeInEditMode` | 编辑模式下用固定尺寸，而不是跟着编辑器视口 |

`Custom` 档交给一个**自定义缩放对象**（`UDreamCanvasCustomScale`）：`Init` 与 `CalculateSizeAndScale` 两个虚函数算出画布尺寸与缩放，另有双向的 `ConvertPositionFromViewportToCanvas` / `ConvertPositionFromCanvasToViewport`。

它的位置换算拿到的是「**画布填的那块视口**」上的像素位置（整张视口，或分屏下属于某个玩家的一块，左上为零点），**画布已经把那一块的角算进去了** —— 也就是说它量的是**从自己那块角**起的位置。这些函数**只在根画布上成立**，非根画布返回失败。

`ScaleWithEngineDPI` 下布局矩形在任何分辨率都保持曲线的设计尺寸，按设计尺寸排的界面永远不会因为空间不够而被裁掉，
只是画得更小；项目调 DPI 曲线时 UMG 和 DreamUI 一起变。

画布有一台虚拟相机。`ProjectionType`（默认 `Perspective`）和 `FieldOfView`（水平视场，默认 60 度）决定它：
眼睛站在 `Width * 0.5 / tan(FOV / 2)` 处，正好让画布矩形填满画面。视场越宽眼睛越近，下面每个透视作用域都越深。
`NearClipPlane` / `FarClipPlane` 在高级选项里；运行时用 `SetProjectionParameters` 一次改四个。

<a id="每个控件自己的透视"></a>

## 每个控件自己的透视

控件上的 `bPerspective` 让它的后代朝一只眼睛缩短，像 CSS 的 `perspective`（属性见[控件模型](widgets.md)）。
它的前提是：**一个使用透视投影的屏幕空间画布**。否则不起作用 —— 正交画布的眼睛在无穷远，任何仿射映射都到不了那里，
所以 `ProjectionType` 和 `FieldOfView` 被有意放在常规选项里而不是高级选项：找不到这两项的作者就没法校准这个功能。

它便宜的原因是画布**本来就**透过透视投影来画。一个子树不需要自己的投影或自己的 pass，只需要把几何重新瞄准，
让画布已有的投影画出子树那只眼睛本该看到的画面。这个重新瞄准是一个普通的仿射映射：
不改顶点格式、不改着色器、不多一个 draw call。嵌套的作用域从内向外逐层交接，最外层再映射到画布的眼睛。

<a id="排序"></a>

## 排序

- `SortOrder` 存为 `int16`，取值 -32768 到 32767；大的画在小的上面。嵌套画布要 `bOverrideSorting` 打开才用自己的值。
- `SetSortOrder(Value, PropagateToChildrenCanvas)` 在传播时保持子画布与它的相对顺序；
  `SetSortOrderToHighestOfHierarchy` / `SetSortOrderToLowestOfHierarchy` 把它放到同一层级所有画布的最上 / 最下。
- 屏幕页面：`AddToViewport` 的 `InSortOrder` 决定页面之间的前后。1000（`StackBaseSortOrder`）及以上留给页面栈，
  页面栈每次刷新都会重写它们的排序；手动放进这个区间的页面和栈的绘制顺序不确定，所以会记一条警告。
- 世界空间画布之间的顺序由 `UDreamWorldWidgetComponent::SortOrder` 写到根画布上。

<a id="渲染目标画布"></a>

## 渲染目标画布

`RenderTarget` 模式下，画布画进 `RenderTarget`；没指定就自己建一张（`AutoRenderTarget`，不保存、不复制）。

| 属性 | 作用 |
| --- | --- |
| `RenderTargetClearColor` | 清屏颜色，默认透明 |
| `RenderTargetUpdateMode` | `Automatic`（检测到变化才画，默认）、`Always`（每帧画）、`WhenRequest`（只在调用 `RequestUpdateForRenderTarget` 时画） |
| `RenderTargetSizeMode` | `RenderTargetFitToCanvas`（目标跟着画布，默认）、`CanvasFitToRenderTarget`（画布跟着目标）、`None` |
| `RenderTargetResolutionScale` | 目标跟着画布时的分辨率倍数 |

渲染目标画布由**它自己的一个渲染命令**来画，在它的网格段变化之后执行，而不是在它所在世界的某个视图里 ——
所以不管有没有东西在渲染它的世界，它都会更新。要显示画出来的内容，可以在另一棵树里放一个 `CanvasRenderTargetPreviewer`
视觉（`SetPreviewCanvas` 指定画布），或者用 `UDreamUIRenderTargetGeometrySource` 在关卡里生成一块显示它的几何 ——
平面（`Plane`）、柱面（`Cylinder`），或映射到一个静态网格上（`StaticMesh`）—— 再配一个 `UDreamUIRenderTargetInteraction`
让它能被点击。

<a id="屏幕根按需创建"></a>

## 屏幕根：按需创建

不需要在关卡里放任何"根"。屏幕上的 UI 挂在一个屏幕空间根画布下，由 `UDreamScreenUISubsystem` **每个本地玩家一个**，
第一次需要时创建：

- `UDreamUIBPLibrary::AddWidgetOfClassToViewport(this, WidgetClass)`：建一个 Widget Blueprint 实例并放上屏幕，
  返回值的类型跟着传入的类走，不需要 Cast；
- `UDreamScreenUISubsystem::AddToViewport` / `AddToPlayerScreen` / `CreateWidgetOnScreen` / `RemoveFromViewport`：
  同样的事，可以指定玩家；
- `GetOrCreateScreenRoot` / `UDreamUIBPLibrary::GetOrCreateScreenSpaceUIRoot`：直接拿到那个根。

分屏是常态而不是一种模式：玩家 1 和玩家 2 各有自己的根画布、自己带着 `UserIndex` 的屏幕空间射线检测器、自己的页面栈，
一个人推上来的全屏页面盖不住另一个人。和 UMG 不同的是这里没有共享层可退：`AddToViewport` 就是给控件所属玩家的
`AddToPlayerScreen`。射线检测器和事件系统同样按需创建（见[输入](input.md)）。

2.0 之前要放进关卡的根 Blueprint（`ScreenSpaceRoot`、`WorldSpaceRoot_DreamRenderer`、`WorldSpaceRoot_UERenderer`）已经删除，
没有重定向，见[迁移](../guides/migration.md)。

<a id="其他画布属性"></a>

## 其他画布属性

| 属性 | 作用 |
| --- | --- |
| `DefaultMaterial` | 画默认 UI 元素用的材质 |
| `bRequireNormalAndTangent` | 顶点数据里带法线和切线 |
| `bEnableDepthTest` | 建一张深度纹理做深度测试，给用不透明材质的 `StaticMesh` 用；只对屏幕空间和渲染目标模式有效 |
| `BlendDepth` / `DepthFade` | 只用于 World Space - DreamUI Renderer：被场景深度遮挡的程度和深度渐隐 |
| `TraceChannel` | 世界空间 UI 被交互射线命中所用的通道 |
| `bAllowDropFrame` | draw call 太耗时时允许丢掉画布的一帧（高级选项） |
| `DefaultMeshType` | 用自定义的 `UDreamUIMeshComponent` 子类来画（高级选项） |

`SetDrawCallRebuildSuspended(true)` 冻结画布的 draw call 列表，期间顶点刷新照常（移动、变色的控件继续更新），被推迟的重建在解冻时
立刻执行。这是 Slate 的 `UInvalidationBox` 对 DreamGUI 的 draw call 列表做的事，`UDreamInvalidationBox` 行为就是用它实现的。

<a id="dreamuistats-量的是什么"></a>

## DreamUI.Stats 量的是什么

控制台命令 `DreamUI.Stats` 打印自上一次 `DreamUI.Stats` 以来每帧的平均开销，然后重新计数。它一直在计：每个阶段两端各读一次时钟、
一次原子加，每个根画布每帧几次，所以基准测试读到的、这个命令打印的、Unreal Insights 里看到的是同一批帧。
总数是**整个进程**的，不是某个画布或某个世界的 —— 一帧的开销就是这么多。

| 阶段 | 线程 | 是什么 |
| --- | --- | --- |
| `ManagerTick` | 游戏线程 | UI 管理器的 tick：行为、布局、剪裁，以及下面每个根画布的更新 |
| `CanvasUpdate` | 游戏线程（在 `ManagerTick` 之内） | 根画布及其子画布把控件、剪裁和几何更新到最新 |
| `Batching` | 工作线程 | 用游戏线程备好的几何拼 draw call |
| `DrawCallSubmit` | 游戏线程 | 把拼好的 draw call 放进网格段和材质，发给渲染线程 |
| `RenderRecord` | 渲染线程 | 把 UI 的 pass 录进这一帧的渲染图 |

`CanvasUpdate` 在 `ManagerTick` 之内，两者不能相加；`Batching` 和 `RenderRecord` 跑在别的线程上，与游戏线程并行。
每个阶段同时也是一个 Insights 里的 CPU 作用域，名字是 `DreamUI_` 加阶段名。

计数器则说明做了多少事，例如录制的批次数和顶点数（`BatchesRecorded`、`VerticesRecorded`）、上传的网格段和字节数
（`SectionUploads`、`UploadedBytes`）、重建了 draw call 的画布数（`DrawCallRebuilds`）、只移动而原地刷新的画布数
（`InPlaceRefreshes`）、只打补丁的网格段（`SectionPatches`）、原样沿用的网格段（`SectionReuses`）、画布更新看过的控件数
（`WidgetsUpdated`），以及渲染层的移动、提升和撤销（`RenderLayerMoves`、`RenderLayerPromotions`、`RenderLayerDemotions`）
和文本重绘（`TextPaints`）。

`stat DreamGUI` 仍然显示它的计数器；`DreamGUI.Memory` 列出每个世界的画布网格段和 paint 行占的内存。
抓图、性能分析和基准测试的完整做法见[调试](../tools/debugging.md)和[基准测试](../tools/bench.md)。
