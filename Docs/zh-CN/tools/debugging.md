---
title: "调试与测量"
description: "不开调试器看 DreamGUI 画了什么、花了多少：DreamUI.Capture、DreamUI.Stats 与 Insights 作用域、stat DreamGUI、DreamGUI.Memory、两个校验开关、诊断命令、可供 A/B 的控制台变量和日志分类。"
---

两个问题——"它画了什么"和"它花了多少"——都能不用调试器回答。

<a id="what-it-drew-dreamuicapture"></a>

## 它画了什么：DreamUI.Capture

```text
DreamUI.Capture [Directory]
```

把当前世界的 UI 写成 PNG：视口一张，每个渲染到渲染目标的根画布各一张。不给目录时写进
`Saved/DreamUI/Captures` 下一个新建的、带时间戳的目录。画布直接画在屏幕上时没有自己的图——它是视口那张图的一部分，
截视口就行。

同样的事是一个 Blueprint 库，[`UDreamUICaptureLibrary`](../Reference/DreamUICaptureLibrary.md)：

| 函数 | 作用 |
| --- | --- |
| `CaptureAll` | 控制台命令做的事：`Viewport.png` 加每个渲染到目标的根画布的 `<widget name>.png`，返回写出的文件 |
| `SaveViewportToPng` | 世界所在的视口——场景加上面的每个面板，玩家看到的样子 |
| `SaveCanvasToPng` | 一个渲染到目标的画布画了什么 |
| `SaveRenderTargetToPng` | 一张渲染目标的像素，保留 alpha |
| `GetCaptureDirectory` | `<Project>/Saved/DreamUI/Captures`，相对路径从这里算 |

每次调用都要把图从 GPU 读回来，所以会等渲染线程把手上的活做完：这是用来看的工具，不是每帧做的事。没有 GPU
（`-nullrhi`）时没东西可读，调用在日志里说明并返回 false。在游戏里，视口是直接画进窗口的、帧与帧之间没有纹理，
所以 `SaveViewportToPng` 返回 false 并提示改用引擎自己的截图请求。

<a id="what-it-cost-dreamuistats-and-insights"></a>

## 它花了多少：DreamUI.Stats 与 Insights

```text
DreamUI.Stats
```

打印**自上一次 `DreamUI.Stats` 以来**的帧逐阶段的花费，然后重新开始计数——所以在想测的那段前后各敲一次。

| 阶段 | 是什么 |
| --- | --- |
| `ManagerTick` | UI 管理器的 tick |
| `CanvasUpdate` | 画布更新 |
| `Batching` | 合批 |
| `DrawCallSubmit` | 提交绘制调用 |
| `RenderRecord` | 渲染线程录制 |

每个阶段给出每帧毫秒数和运行次数。后面是计数器，每帧平均和总数：`BatchesRecorded`、
`VerticesRecorded`、`SectionUploads`、`UploadedBytes`、`DataTextureUpdates`、`GeometryCopies`、`SectionReuses`、
`WidgetsUpdated`、`SectionPatches`、`DrawCallRebuilds`、`InPlaceRefreshes`、渲染层的 `RenderLayerMoves` /
`Promotions` / `Demotions`，以及文字的 `TextPaints`、`TextMoveRepaints`、`SmallTextPlacements`、`SharpenSweepTexts`、
`SharpenRepaints`、`CoverageItemsDrawn`、`CoverageGlyphLookups`、`CoverageRastersSync`、`CoverageJobs`、
`CoverageFlushes` 和 `FontAtlasUploadBytes`。

**在 Unreal Insights 里**，每个阶段也是一个具名作用域，名字以 `DreamUI_` 开头（`DreamUI_LayoutPass`、
`DreamUI_UpdateRootCanvases`、`DreamUI_TextLayout`、`DreamUI_RecordScreenSpace`、`DreamUI_WorldSpaceRaycast`……），
在 CPU 通道上。每个画布、每个分段、每个 widget 都要跑一次的细粒度作用域在一个单独的通道
`DreamUIDetail` 上，默认关闭——几千个 widget 的墙上它们会跑几千次，每次读两遍时钟，开着的一帧就说明不了没开的一帧。需要时：

```text
-trace=default,DreamUIDetail
Trace.Enable DreamUIDetail
```

前者在命令行上，后者在控制台里。

**`stat DreamGUI`** 是插件的 stat 组：显示画布的合批、绘制调用更新、几何体更新、顶点变换、布局、属性绑定轮询、
根画布更新等计时，以及动画播放器的两个计数（自己计时的、由 Sequencer 更新的）。

<a id="what-it-holds-dreamguimemory"></a>

## 它占了多少：DreamGUI.Memory

```text
DreamGUI.Memory
DreamGUI.Memory Json
DreamGUI.Memory File=<path>
```

每个字体的字形图集（切片数、GPU 字节数和 CPU 副本、格子、字形、字体文件字节）、精灵图集页，以及每个有 UI 管理器的世界里
画布网格的分段和绘制参数行。`Json` 改打印 JSON，`File=` 把 JSON 写进文件（相对于 `Saved`）。打包测试里的探针就是用它
记录内存报告的。

<a id="the-two-verification-switches"></a>

## 两个校验开关

为一个"画错了"或"没画出来"的报告准备的：

| 变量 | 打开后 |
| --- | --- |
| `r.DreamUI.VerifyPartialPrepare 1` | 画布每次从上一次增量准备（只重做请求了的、新来的、移动了的 widget 画的东西），都和一次全量准备对比；有差异就是一个点名画布的 ensure |
| `r.DreamUI.VerifyKeptPointers 1` | DreamGUI 不每帧查找而是**保留**的对象——widget 的画布、画布的渲染层、UI 管理器的画布、动画属性绑定的对象——在每次使用时也查找一遍；不一致就是一个说明是哪个的 ensure |

两者默认都关，开着有代价（全量准备、查找本来省下的开销）。测试宿主在 `Config/DefaultEngine.ini` 里把它们打开，
所以整个测试套件都在它们下面跑：

```ini
[ConsoleVariables]
r.DreamUI.VerifyPartialPrepare=1
r.DreamUI.VerifyKeptPointers=1
```

基准测试（`Tools/Bench`）在测量的会话里会把它们关掉。文字的增量布局有自己的对应开关：
`DreamGUI.Text.VerifyIncremental 1` 让每次基于保留状态的布局都再从头布一次并比较，第一处差异写进日志，用新的那份。

<a id="diagnostic-commands"></a>

## 诊断命令

| 命令 | 作用 |
| --- | --- |
| `dreamgui.ListPendingWidgets` | 列出已经创建但还没加到任何地方的 widget（`ConstructWidget` 之后、`AddChild` 之前的"停放"状态），以及它们等了多久。把它们放在一个具名数组里是为了不被回收，能列出来是为了不让那里变成东西悄悄堆积的地方 |
| `DreamGUI.Diag.FindTreeBridges` | 列出当前世界的关卡持有的对象（actor、它的组件或子对象）指向某棵 DreamGUI widget 树的每一个引用，以及那是谁的树。播放会话复制关卡时会顺着这种引用进入树 |
| `DreamGUI.Text.ShapeCacheFlush` | 清空文字整形缓存，计数器保留 |
| `DreamUI.ExportSymbols` | 重写 `DUI/.dui-symbols.json`（编辑器），见 [VS Code 扩展](vscode.md) |
| `DreamUI.Binding.Dump` | 列出每个带属性绑定的活 user widget：多少绑定是订阅的（变化时才更新）、多少每帧读、多少是常量、多少是旧格式，以及每个每帧读的绑定是因为哪个成员才留在轮询上的。见[绑定什么时候更新](../language/bindings.md#bindings-and-routes) |
| `DreamUI.Binding.ForcePoll 1` | 之后初始化的 widget，所有按依赖订阅的绑定都改回每帧读。值不更新时用它区分"没人宣布这次变化"和"它从来没变"，默认 0 |

⚠️ **不是所有 DreamUI 的「命令」都是控制台命令。** 编辑器侧有两个名字同族的入口走的是**工具栏与 Tools 菜单**，
不是控制台：`DreamUI.OpenWorkspace`（重写并打开 `DUI/DreamUI.code-workspace`）和 `DreamUI.RebuildAll`
（整项目重编译文本支撑的控件蓝图，**会先弹确认框**）。它们各有 toolbar 与 Tools 菜单两个 id
（`DreamUI.OpenWorkspaceTools` / `DreamUI.RebuildAllTools`），见[设计器](designer.md)。

追踪类变量，打开后往日志里写：`dreamgui.LayoutTrace 1`（每次面板排列和它提交的每个矩形）、
`dreamgui.ScrollBoxTrace 1`（每次改变状态的滚动框拖动增量和物理 tick），以及 `dreamgui.DumpMaterialDraws 1`
（每次屏幕空间材质绘制尝试走了哪个分支，直到设回 0）。

<a id="switches-for-ab-runs"></a>

## 可以拿来做 A/B 的开关

这些变量存在是为了测量：一次会话里先测 A，切换，再测 B。默认值就是出厂行为。

**渲染层与画布**

| 变量 | 默认 | 含义 |
| --- | --- | --- |
| `r.DreamUI.RenderLayers` | 1 | 渲染变换一直在变的控件成为渲染层；画布在 GPU 上移动它下面的几何体 |
| `r.DreamUI.RenderLayerPromoteFrames` | 2 | `RenderLayer` 为 Auto 的控件，连续这么多帧渲染变换都变了就升为渲染层 |
| `r.DreamUI.RenderLayerDemoteFrames` | 60 | 静止这么多帧后就降回去，重新和画布其余部分合批 |
| `r.DreamUI.RenderLayerMaxPerCanvas` | 8192 | 一个画布最多为设为 Auto 的控件建多少渲染层 |
| `r.DreamUI.ParallelLayerRowsMin` | 2048 | 渲染层多于这个数时，画布算出它们的位置并在多个线程上写它们的表行 |
| `r.DreamUI.QuietRenderLayers` | 1 | 一个移动的渲染层下面没有谁需要知道这次移动时，不遍历进去 |
| `r.DreamUI.RefreshMovesInPlace` | 1 | 控件只是移动了的画布保留绘制调用，只刷新顶点和包围盒 |
| `r.DreamUI.DeferTransformNotifications` | 1 | 控件的移动在 UI 管理器下一次 flush 时宣布一次，而不是每次写都宣布 |
| `r.DreamUI.AsyncVertexTransformMinVertices` | 256 | 顶点数至少这么多的几何体在 worker 上变换 |
| `r.DreamUI.ParallelVertexRefreshMinCanvases` | 32 | 一帧里有这么多画布要刷新顶点时，用 task graph 的 worker |
| `r.DreamUI.ParallelLayerPlacementMinCanvases` | 32 | 同上，放置渲染层 |
| `r.DreamUI.ParallelRaycastMinCanvases` | 32 | 根画布多于这个数时，世界空间射线检测的便宜包围测试并行 |
| `r.DreamUI.ParallelRaycastMinVisuals` | 2048 | 一个画布的视觉多于这个数时，射线检测在多线程上看它们 |
| `r.DreamUI.EditorWorldTickIntervalDuringPlay` | 0.5 | 播放会话进行时，正在编辑的关卡最多隔这么多秒更新一次它的 UI |

**动画**

| 变量 | 默认 | 含义 |
| --- | --- | --- |
| `DreamUI.Animation.Ticker` | 1 | 动画播放器由 DreamGUI 的 ticker 驱动，只 tick 有事可做的 |
| `DreamUI.Animation.DirectEvaluation` | 1 | 只含普通属性轨道的动画直接从通道求值，不经过 Sequencer 的实体系统 |
| `DreamUI.Animation.LitePlayer` | 1 | 这种动画自己在循环边界之间计时，没在播放的播放器跳过每帧更新 |
| `DreamUI.Animation.ReusePlayers` | 1 | 播完的播放器留着给同一个动画的下一次播放 |
| `DreamUI.Animation.MaxSparePlayers` | 2 | 每个动画组件留几个空闲播放器 |
| `DreamTween.MaxStepSeconds` | 0 | 一帧最多推进 tween 多少步；0 关闭。加载卡顿前刚开始的入场动画不会直接跳到结尾 |

**文字与滚动**

| 变量 | 默认 | 含义 |
| --- | --- | --- |
| `DreamGUI.Text.SmallTextCoverage` | -1 | 小字是否从 hinted coverage 字形画。-1 跟随项目设置，0 全部关，1 对所有交给项目决定的字体打开。改变会重绘所有文字 |
| `DreamGUI.Text.SmallTextMaxPixelSize` | 0 | 大于 0 时覆盖项目的小字像素上限，用来把 coverage 强加到更大的标签上做测量 |
| `DreamGUI.Text.SmallTextOnMove` | 0 | 小字移出像素网格时：0 每次移动都重绘；1 保留 coverage 四边形，静止 3 帧后重绘；2 先从距离场画一次，静止后再换回 coverage |
| `DreamGUI.Text.IncrementalLayout` | 1 | 正在输入的文本框，或内容连续两次变了的长文本，只重新布局编辑碰到的部分 |
| `DreamGUI.Text.IncrementalParse` | 1 | 增量布局从文本本身找到编辑的位置，只读编辑周围的窗口 |
| `DreamGUI.Text.IncrementalMeasure` | 1 | 长段落里只重新整形、测量编辑周围的窗口 |
| `DreamGUI.Text.InPlaceDisplayList` | 1 | 上次写出的显示列表在原地编辑 |
| `DreamGUI.Text.ShapeCache` | 1 | 整形过的词（同字体、字号、文字、方向、语言、特性）从缓存取 |
| `DreamGUI.Text.ShapeCacheKB` | 4096 | 整形缓存的预算，约 15000 个词；改变会清空缓存 |
| `DreamGUI.Scroll.SnapToDevicePixels` | 0 | 2D 画布上的滚动视图按整设备像素移动内容，小字滚动时保住自己的像素 |

<a id="log-categories"></a>

## 日志分类

| 分类 | 谁写 |
| --- | --- |
| `DreamGUI` | 核心以及输入、控件、扩展、示例模块 |
| `LogDreamGUIRenderer` | 渲染器 |
| `DreamTween` | Tween |
| `DreamGUIEditor` | 设计器和资产工具（文本编译、写回、桥） |
| `DreamGUIK2Nodes` | 蓝图节点 |
| `LogDreamGUIReferenceDocs` | 参考文档 commandlet |

用引擎自己的方式调详细程度：命令行上 `-LogCmds="DreamGUI Verbose"`，或控制台里 `Log DreamGUI Verbose`。
