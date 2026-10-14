---
title: 基准测试
description: Tools/Bench 里的两面"按钮墙"——5000 个按钮的屏幕和 2688 个世界空间面板——怎么在 PIE 或 -game 里跑，怎么读它们留下的 CSV 和 trace，以及读数时要守的规矩。
---

`Tools/Bench` 是 DreamGUI 的性能工作所用的基准：两个场景，各是一类 UI 的最坏情况，在测试宿主（见[自动化测试](tests.md#测试宿主)）
上跑，用编辑器的 PIE 或编辑器二进制的 `-game` 进程。

| 模式 | 场景 | 压的是什么 |
| --- | --- | --- |
| `screen` | 一个屏幕空间画布上 5000 个按钮，点它的 Play 按钮后全部一起转，一轮接一轮 | 游戏线程：动画播放器、渲染层、画布更新 |
| `world` | 一个关卡里 2688 个世界空间按钮面板，每个都是自己的 actor 和画布，各自动画 | 两个线程：游戏线程上逐面板的 pass，渲染线程上每个面板一次绘制 |

**警告：**

**场景不在仓库里。** 它们是使用 DreamGUI 的项目的内容，不是插件的。跑之前把它们拷进宿主的 `Content`：屏幕墙是一个
  根上放着按钮、带 `Button_On_Clicked` 事件（或者带 `CallAnimation` 函数的行）的 widget Blueprint，用 `-ScreenWidget` 指定类；
  世界墙是一个放满 `DreamWorldWidgetActor` 的关卡，用 `-Map` 指定。默认值是它们在开发项目里的名字。

<a id="跑起来"></a>

## 跑起来

```powershell
$env:DREAMGUI_TEST_PROJECT = '<host>\DreamGUITestHost.uproject'
$env:DREAMGUI_ENGINE = '<engine root>'

pwsh -File Tools/Bench/bench_launch.ps1 -Mode world -Tag w1 -Csv 1          # PIE，动画窗口的 CSV profile
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Tag s1 -Game -Csv 1   # -game：帧里没有编辑器 UI
pwsh -File Tools/Bench/bench_series.ps1 -Runs world:warm,world:w1,world:w2,screen:s1,screen:s2 -Report r1
```

所有输出都进 `<host>/Saved/DreamGUIBench`：拷出来的日志、trace（`-Trace 1`）、采样（`-Sample 1`）。

`bench_launch.ps1` 是一次会话：在宿主上启动编辑器，由 `bench_run.py` 驱动，先测一个空闲窗口，再测动画窗口，打印 `[DreamPerf]`
行（帧数、平均、中位数、p95、最大值）。它只停自己启动的进程，同一项目的编辑器开着时拒绝启动。会话期间它会关掉测试宿主的两个校验开关
（`r.DreamUI.VerifyPartialPrepare`、`r.DreamUI.VerifyKeptPointers`）——它们花的正是捷径省下的。

| 参数 | 作用 |
| --- | --- |
| `-Mode` | `screen` 或 `world` |
| `-Tag` | 这次运行的名字，trace、采样和日志按它命名 |
| `-Game` | 用编辑器二进制的 `-game` 进程而不是 PIE |
| `-Map`、`-ScreenWidget` | 关卡；屏幕墙的 widget 类 |
| `-Csv 1`、`-Trace 1`、`-TraceChannels` | CSV profile；Insights trace；额外的 trace 通道（比如 `DreamUIDetail`） |
| `-StatsCmds` | 在动画窗口开始和结束时运行的控制台命令（`;` 分隔），比如 `DreamUI.Stats` |
| `-AbCmds` | A/B：先测 A 面，运行这些命令，再测 B 面 |
| `-SetupCmds` | 整个会话的控制台命令，两面一样，游戏启动时运行，比如 `r.DreamUI.RenderLayers 0` |
| `-Warmup`、`-Window`、`-Rounds` | 预热秒数、测量窗口秒数、轮数 |
| `-Sample 1`、`-SamplePhase` | 用 `StackSampler.cs` 采样游戏线程，在 `window`、`all`、`edges` 或 `ends` 阶段 |
| `-Cursor` | 指针停在哪：`away`（左上角，射线不进墙）、`center` 或 `keep` |

<a id="读-profile-和-trace-的脚本"></a>

## 读 profile 和 trace 的脚本

| 脚本 | 作用 |
| --- | --- |
| `bench_series.ps1` | 跑多个会话，把每个的 CSV 中位数放进一份报告；`warm` 开头的 tag 不计入 |
| `csv_summary.py` | CSV profile 各列的中位数、平均、p95（`--columns=`、`--dir=`） |
| `csv_spikes.py` | 每个预算之上的帧数，以及超过 `--over=` 的帧和它们的邻居 |
| `insights_top.py` | 一份 trace 在某个区间（`--region DreamPerf_Anim_A`）里每帧最重的计时器 |
| `frame_breakdown.py` | trace 里最慢的几帧，逐帧拆开：一个尖峰是由什么组成的 |
| `run_sampler.ps1`、`StackSampler.cs`、`cmp_samples.py` | 对运行中进程的一个线程做采样分析，以及比较两份采样报告 |

<a id="例子运动中的小字"></a>

## 例子：运动中的小字

小字（20 个设备像素及以下）从 coverage 字形画，文字移出像素网格时就得重绘。动起来时这要花多少，在一次会话里做 A/B 测：A 面 coverage 开，
B 面关（`DreamGUI.Text.SmallTextCoverage 0`）：

```powershell
# 跑两次：构建后的第一次启动在后台编译着色器，丢掉
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag covwarm -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov1 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0'
# 两面都关掉渲染层再测一次
pwsh -File Tools/Bench/bench_launch.ps1 -Mode screen -Game -Csv 1 -Trace 0 -Tag cov2 -StatsCmds 'DreamUI.Stats' -AbCmds 'DreamGUI.Text.SmallTextCoverage 0' -SetupCmds 'r.DreamUI.RenderLayers 0'
```

屏幕墙的标签是按钮自己的 16 px 文字，所以每一个都是小字，而且 5000 个在同一帧开始转。A 面对 B 面读 `[DreamPerf] screen anim A` 和
`anim B` 两行（CSV 只覆盖 A 面），计数器读每一面开始和结束时 `DreamUI.Stats` 的输出。以下情况说明有问题：

- 各轮第一帧（所有按钮开始转）里最差的一帧比 B 面高出 5 ms 或更多；
- 滚动时每帧的 `TextMoveRepaints` 接近正在移动的文字数（每次移动都重绘了）；
- 动画文字成为渲染层之后，每帧的 `SmallTextPlacements` 接近动画文字数；
- 稳定的轮次里 `CoverageFlushes` 不是 0。

世界空间里 coverage 从不运行，所以世界墙两面不应该有差别——它是对照组。

同样的问题在编辑器里、固定场景上的版本，是 `Perf` 预设的渲染基准：标签每帧滑动 0.37 像素、按整像素滚动、缩放、旋转（渲染层开和关）、
变焦、颜色脉动，每种运动在 coverage 开和关下各计时一次，结果在 `Saved/DreamGUITests/Perf/Benchmark.json`。

<a id="打包构建"></a>

## 打包构建

世界墙会自己播放，所以打包的 Development 构建不需要编辑器的 Python 也能测：用 `RunUAT.bat BuildCookRun` 烹饪打包世界墙的关卡，
用 `-csvCaptureFrames=1800 -ExitAfterCsvProfiling` 运行打包后的游戏（捕获结束就退出），再用 `csv_summary.py` 读新生成的 CSV。几点要注意：

- 构建参数加 `-DisableAdaptiveUnity`：宿主的插件是一个刚同步过文件的 git worktree，adaptive unity 会把每个文件都当成正在编辑的，
  逐个编译——几百个，一小时以上；
- 关卡没有 PlayerStart，默认视角从背后看面板，最初几帧在加载。`csv_summary.py` 的中位数不受影响，平均值受影响；
- 屏幕墙需要编辑器的 Python 把 widget 放上屏幕，打包构建里没有 Python：用 `-Game` 测它。

完整命令在 `Tools/Bench/README.md` 里。

<a id="读数的规矩"></a>

## 读数的规矩

- **丢掉构建后的第一次启动**：着色器在后台编译，那次的帧更慢。`bench_series.ps1` 会跑一个 `warm` 开头的 tag 并把它排除在报告外。
- **只在同一次坐下里比较。** 不同的日子，或者机器上任何别的东西变了之后（另一个程序、第二块显示器、窗口被挡住）的数不能比；60 FPS
  的帧率上限会把差别藏起来，直到藏不住。判断一个改动，用同一会话里的 A/B（`-AbCmds`），或者看采样 profile 里一个函数的占比。
- **PIE 要为编辑器付钱。** PIE 游戏线程大约一半是编辑器自己的 UI；`-Game` 去掉这部分，打包构建去掉剩下的。
- **trace 本身花帧时间**，每个计时作用域在作者的机器上约 0.3 µs：测量的会话用 `-Trace 0`，拆解是另一次、带 trace 的会话。DreamGUI
  逐画布、逐 widget、逐玩家的作用域在单独的 `DreamUIDetail` 通道上，除非写进 `-TraceChannels` 否则关闭。
