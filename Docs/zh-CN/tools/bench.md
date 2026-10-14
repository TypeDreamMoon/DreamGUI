---
title: "基准测试"
description: "Tools/Bench 里的两面按钮墙——一屏 5000 个按钮和一个关卡里 2688 个世界空间面板——如何在 PIE 或 -game 中运行它们、如何读取它们留下的 CSV profile 和 trace，以及读取这些数字的规则。"
---

`Tools/Bench` 收录了 DreamGUI 性能工作所依据的基准：两个场景，各代表一类 UI 的最坏情况，在测试主机上运行（见[自动化测试套件](tests.md#the-test-host)），可在编辑器的 PIE 中运行，也可在编辑器二进制文件的 `-game` 进程中运行。

| 模式 | 场景 | 开销所在 |
| --- | --- | --- |
| `screen` | 一个屏幕空间画布上的 5000 个按钮，点击其 Play 按钮后全部同时转动，一轮接一轮 | 游戏线程：动画播放器、渲染层、画布更新 |
| `world` | 一个关卡中的 2688 个世界空间按钮面板，每个都是独立的 actor 和画布，各自独立播放动画 | 两个线程：游戏线程上逐面板的处理，渲染线程上每个面板一次绘制 |

**注意：**

  **这些场景不在仓库中。** 它们属于使用 DreamGUI 的项目的内容，而不是插件的内容。运行前请将它们复制到测试主机的 `Content` 中：对于屏幕墙，是一个控件蓝图，其根节点包含这些按钮，并带有一个 `Button_On_Clicked` 事件（或带有 `CallAnimation` 函数的行），其名称由 `-ScreenWidget` 指定；对于世界墙，是一个由 `DreamWorldWidgetActor` 组成的关卡，其名称由 `-Map` 指定。这些默认值就是它们在开发项目中所用的名称。

<a id="running"></a>

## 运行

```powershell
$env:DREAMGUI_TEST_PROJECT = '<host>\DreamGUITestHost.uproject'
$env:DREAMGUI_ENGINE = '<engine root>'

pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Tag w1 -Csv 1          # PIE, CSV profile of the animated windows
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag s1 -Game -Csv 1   # -game: no editor UI in the frame
pwsh -File Tools/Bench/bench_series.ps1 -Runs world:warm,world:w1,world:w2,screen:s1,screen:s2 -Report r1
```

所有内容都会输出到 `<host>/Saved/DreamGUIBench`：复制的日志、trace（`-Trace 1`）、采样（`-Sample 1`）。

`bench_launch.ps1` 是一次会话：它在主机上启动编辑器，由 `bench_run.py` 驱动，先测量一个空闲窗口，然后测量动画窗口，并打印 `[DreamPerf]` 行（帧数、平均值、中位数、p95、最大值）。它只会停止自己启动的进程，并且当同一项目的编辑器正在运行时拒绝启动。在会话期间，它会关闭测试主机的两个校验开关（`r.DreamUI.VerifyPartialPrepare`、`r.DreamUI.VerifyKeptPointers`）——它们的开销正是这些快捷方式所节省的。

| 参数 | 作用 |
| --- | --- |
| `-Mode` | `screen` 或 `world` |
| `-Tag` | 本次运行的名称；trace、采样和复制的日志都以此命名 |
| `-Game` | 在编辑器二进制文件上使用 `-game` 进程，而不是 PIE |
| `-Map`、`-ScreenWidget` | 关卡；屏幕墙的控件类 |
| `-Csv 1`、`-Trace 1`、`-TraceChannels` | 一个 CSV profile；一个 Insights trace；额外的 trace 通道（比如 `DreamUIDetail`） |
| `-StatsCmds` | 在动画窗口开始和结束时运行的控制台命令（以 `;` 分隔），例如 `DreamUI.Stats` |
| `-AbCmds` | 一次 A/B 对比：先测量 A 面，然后执行这些命令，再测量 B 面 |
| `-SetupCmds` | 整个会话的控制台命令，两面相同，在游戏启动时运行，例如 `r.DreamUI.RenderLayers 0` |
| `-Warmup`、`-Window`、`-Rounds` | 预热秒数、测量窗口秒数、轮数 |
| `-Sample 1`、`-SamplePhase` | 在 `window`、`all`、`edges` 或 `ends` 阶段，用 `StackSampler.cs` 对游戏线程采样 |
| `-Cursor` | 指针停留的位置：`away`（左上角，没有射线射入墙内）、`center` 或 `keep` |

<a id="scripts-that-read-profiles-and-traces"></a>

## 读取 profile 和 trace 的脚本

| 脚本 | 作用 |
| --- | --- |
| `bench_series.ps1` | 多次会话，将每次会话的 CSV 中位数汇总到一份报告中；以 `warm` 开头的标签会被排除 |
| `csv_summary.py` | 一份 CSV profile 各列的中位数、平均值和 p95（`--columns=`、`--dir=`） |
| `csv_spikes.py` | 有多少帧超过了各个预算，以及超过 `--over=` 的帧及其相邻帧 |
| `insights_top.py` | 一份 trace 在某个区间（`--region DreamPerf_Anim_A`）内最重的计时器，按帧统计 |
| `frame_breakdown.py` | 一份 trace 中最慢的帧，逐帧拆解：一次尖峰是由什么构成的 |
| `run_sampler.ps1`、`StackSampler.cs`、`cmp_samples.py` | 对运行进程某个线程的采样分析器，以及对其两份报告的比较 |

<a id="an-example-small-text-under-motion"></a>

## 示例：运动中的小字

小字（20 个设备像素及以下）从覆盖字形绘制，当文字移出设备的像素网格时必须重新绘制。它在物体运动时的开销，在一次会话中作为 A/B 来测量——A 面开启覆盖，B 面关闭（`DreamGUI.Text.SmallTextCoverage 0`）：

```powershell
# twice: the first launch after a build compiles shaders in the background, and is discarded
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag covwarm -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov1 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
# the same with render layers off on both sides
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov2 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0' -SetupCmds 'r.DreamUI.RenderLayers 0'
```

屏幕墙的标签是其按钮自身的 16 px 文字，因此每一处都是小字，而且全部 5000 个都在同一帧开始转动。将 A 面与 B 面对照，读取 `[DreamPerf] screen anim A` 和 `anim B` 行（CSV 仅覆盖 A 面），并从每一面开始和结束时 `DreamUI.Stats` 的输出中读取计数器。出现以下情况说明有问题：

- 各轮第一帧（每个按钮开始转动）中最差的一帧比 B 面高出 5 ms 或更多；
- 滚动期间每帧的 `TextMoveRepaints` 接近正在移动的文字数量（每次移动都重绘一次）；
- 当动画文字成为渲染层之后，每帧的 `SmallTextPlacements` 接近动画文字的数量；
- 在稳定的轮次中 `CoverageFlushes` 不是 0。

覆盖从不在世界空间中运行，因此世界墙的两面不应有差异——它是对照组。

同样的问题，在编辑器内、固定场景上的版本，是 `Perf` 预设的渲染基准：它的标签每帧滑动 0.37 个像素、按整像素滚动、对其缩放进行补间、旋转（渲染层开和关）、缩放并脉动其颜色，每种运动都在覆盖开和关的情况下计时，结果在 `Saved/DreamGUITests/Perf/Benchmark.json` 中。

<a id="a-packaged-build"></a>

## 打包构建

世界墙会自行播放，因此打包的 Development 构建无需编辑器的 Python 即可测量：用 `RunUAT.bat BuildCookRun` 烹饪并打包世界墙的关卡，用 `-csvCaptureFrames=1800 -ExitAfterCsvProfiling` 运行打包后的游戏（捕获结束时会结束游戏），并用 `csv_summary.py` 读取新的 CSV。需要了解的事项：

- 向构建中添加 `-DisableAdaptiveUnity`：主机的插件是一个 git worktree，其文件刚刚通过一次同步复制过来，而自适应 unity 会把每一个文件都当作正在编辑的文件，逐个编译——几百个文件，耗时一小时或更久。
- 该关卡没有 PlayerStart：默认视角从背后看向面板，并且最初几帧还在加载。`csv_summary.py` 的中位数不受此影响；其平均值会受影响。
- 屏幕墙需要编辑器 Python 放置到屏幕上的控件，而打包构建没有 Python：请改用 `-Game` 来测量它。

完整命令在 `Tools/Bench/README.md` 中。

<a id="reading-the-numbers"></a>

## 读取这些数字

- **丢弃构建后的第一次启动**：着色器在后台编译，它的帧因此更慢。
  `bench_series.ps1` 运行一个以 `warm` 开头的标签，并将其排除在报告之外。
- **在一次运行中比较。** 不同日期，或者机器上其他任何东西（另一个程序、第二台显示器、窗口被遮挡）发生变化前后的数字
  不可比较；60 FPS 的帧率上限会掩盖差异，直到它掩盖不住。判断一个改动，用同一会话里的 A/B（`-AbCmds`），或者用函数在采样 profile 中的占比。
- **PIE 要为编辑器付出代价。** PIE 的游戏线程大约一半是编辑器自己的 UI；`-Game` 将其排除，打包构建会排除其余部分。
- **trace 本身要花帧时间**，在作者的机器上每个计时作用域约 0.3 µs：测量的会话用
  `-Trace 0` 运行，拆解是另一次带 trace 的会话。DreamGUI 的逐画布、逐控件和逐玩家作用域在单独的通道上，
  `DreamUIDetail`，除非在 `-TraceChannels` 中指定，否则关闭。
