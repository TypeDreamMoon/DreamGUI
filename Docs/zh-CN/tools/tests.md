---
title: "自动化测试套件"
description: "插件自带的自动化测试套件（一次完整运行超过 2400 个测试）、Invoke-DreamGUITests.ps1 及其预设、静态检查及其规则、测试宿主项目，以及发布闸门的打包文本冒烟测试。"
---

自动化测试套件是插件的一部分——上游没有。它位于 `DreamGUITests` 模块中（仅在编辑器中加载），其测试命名为 `DreamGUI.<Area>.<Sentence>`，例如
`DreamGUI.Button.ATapOnTheButtonClicksItOnce`；补间测试位于 `DreamTween.*` 下。一次 `All` 运行会运行**超过
2400 个**测试。

<a id="the-simplest-way-to-run-it"></a>

## 运行它的最简单方式

在编辑器的控制台中：

```text
Automation RunTests DreamGUI
```

或无头运行：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests DreamGUI" -unattended -nopause -NullRHI -TestExit="Automation Test Queue Empty"
```

`RunTests` 会按 `+` 拆分其过滤器并对各项取 OR：`StartsWith:X` 匹配以 `X.` 开头的名称，`^X$` 精确匹配，`Group:X` 展开一个 ini 组，其余任何内容都是不区分大小写的**子串**。过滤器不能包含
逗号、分号或引号——`-ExecCmds` 按逗号拆分，而 Automation 命令按分号拆分。

在 `-nullrhi` 下，引擎会自行丢弃所有标记为 `NonNullRHI` 的测试（即读取像素的那些），因此这些测试需要
`-RenderOffScreen`：一个没有窗口的真实 RHI。

<a id="the-runner-invoke-dreamguitestsps1"></a>

## 运行器：Invoke-DreamGUITests.ps1

你日常使用的是 `Tools/Tests/Invoke-DreamGUITests.ps1`。它负责构建、运行和判定，并说明实际运行了多少个测试。
它需要 PowerShell 7.2 或更高版本（`pwsh`）、`PATH` 上的 Python 3.8 或更高版本（作为 `python`），以及 git。

```powershell
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1                        # Quick: build, then the headless suite
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -NoBuild
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Filter "StartsWith:DreamGUI.Button" -Repeat 5
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Project <path>\MyGame.uproject -Engine <engine root>
```

它按顺序做六件事：

<a id="pre-flight"></a>

### 预检
引擎和项目存在；报告目录不在 C 盘（`-AllowSystemDrive` 或
`DREAMGUI_ALLOW_DRIVE_C=1` 允许如此）；Python 有响应；并且没有编辑器在其命令行中带有此 `.uproject`——打开着的
编辑器会占住构建需要替换的 DLL（LNK1104），并与运行共享 `Saved`，因此运行器会拒绝（退出码 2），
除非 `-AllowEditorOpen`。

<a id="static-checks"></a>

### 静态检查
`static_checks.py`，见下文。用 `-SkipStaticChecks` 跳过。

<a id="build"></a>

### 构建
整个编辑器目标，带 `-NoEngineChanges`：任何会重新编译或重写引擎中内容的构建都会在其第一个动作之前被拒绝。**绝不要 `-Module=`**——受限构建不会重写插件的模块清单，而
清单缺少某个模块的插件整体加载失败。用 `-NoBuild` 跳过。

<a id="manifest"></a>

### 清单
插件的 `Binaries\Win64\UnrealEditor.modules` 必须携带引擎的 BuildId 并列出 `DreamGUI.uplugin` 构建进编辑器的每一个模块，
每个模块都有一个非空 DLL。也使用 `-NoBuild` 检查。

<a id="run"></a>

### 运行
`UnrealEditor-Cmd.exe` 带 `Automation RunTests <filter>` 加上预设的参数；超过预设的超时时间
（`-TimeoutMinutes` 覆盖它）后，编辑器连同它启动的一切一起被终止。`-Repeat N` 在一次构建后运行 N 次，
每次都在一个全新的编辑器中进行。

<a id="digest"></a>

### 判定
`digest.py` 根据引擎的 JSON 报告和日志判定此次运行；见下文。

使用哪个项目：`-Project`，否则使用 `DREAMGUI_TEST_PROJECT` 环境变量，否则在测试宿主存在时使用它。被测试的
插件是该项目的 `Plugins` 目录里的 DreamGUI，它不必是运行器所在的检出——横幅会打印它的路径、分支和提交。引擎：`-Engine`，否则 `DREAMGUI_ENGINE`。

<a id="presets"></a>

### 预设

定义在 `presets.json` 中；运行器拥有该列表，且不依赖引擎的 ini 组。

| 预设 | 运行内容 | 编辑器参数 |
| --- | --- | --- |
| `Quick` | 除 PIE 层（`DreamGUI.Pie.*`）外的所有 `DreamGUI.*` 和 `DreamTween.*` 测试 | `-nullrhi` |
| `Interaction` | 声明在 `Private/Interaction` 和 `Private/Driver/Tests` 下的测试 | `-nullrhi` |
| `Designer` | `DreamGUI.Designer.*` | `-nullrhi` |
| `Rhi` | 所有标记为 `NonNullRHI` 的测试，PIE 层的和基准测试除外 | `-RenderOffScreen` |
| `Validate` | 同上，在 RHI 验证层下 | `-RenderOffScreen -rhivalidation` |
| `Pie` | `DreamGUI.Pie.*`（其 `NonNullRHI` 测试在 `PieRhi` 中运行） | `-nullrhi` |
| `PieRhi` | `DreamGUI.Pie.*` 的 `NonNullRHI` 测试，在真实 RHI 上 | `-RenderOffScreen` |
| `Exit` | `DreamGUI.Lifecycle.Smoke.*`，然后是编辑器自身的退出，之后搜索其日志 | `-nullrhi` |
| `Perf` | 基准测试 `DreamGUI.Performance.*`，独占其编辑器 | `-RenderOffScreen` |
| `All` | 全部内容，在一个编辑器中使用真实 RHI | `-RenderOffScreen` 加上 `-dpcvars`（关闭 GPU Scene 的预留缓冲） |

`All` 会关闭 GPU Scene 的预留缓冲，因为该套件创建了太多世界：每个测试世界的场景会预留
8 GiB 的 GPU 地址空间，被销毁的世界会一直保留其场景直到下一次垃圾回收（编辑器中大约相隔一分钟），而在每秒二十个世界的情况下，进程会在几秒内耗尽地址空间，之后 D3D12
会移除设备。无头运行时，同样的世界在 GPU 上不消耗任何资源。

每个预设都有一个**下限**：必须实际运行的最少测试数量，少于该数量即为基础设施失败
（退出码 2）。它阻止的是加载了但没有测试模块的插件被当作"0 个测试，0 个失败"通过。下限
只会上升。

<a id="exit-codes-and-the-report"></a>

### 退出码与报告

| 码 | 含义 |
| --- | --- |
| 0 | 绿色：运行的每个测试都通过，已知问题除外 |
| 1 | 红色：至少有一个非已知问题的测试失败 |
| 2 | 未判定：静态检查、构建、清单、崩溃、ensure、超时、缺失报告、测试数少于下限、本应运行却未运行的已声明测试，或编辑器以非零退出码退出 |

**带有 ensure 的运行不是绿色**，即使每个测试都通过：ensure 是测试未断言的 bug。

报告位于 `<project>\Saved\DreamGUITestReports\<timestamp>-<Preset>\`（用 `-ReportDir` 移动根目录）。要读取的页面
是 `summary.md`：判定结果、计数、每个失败及其首个错误、已知问题、ensure、崩溃、缓慢
和不稳定的测试。它旁边是供脚本使用的 `digest.json`、引擎自身的 `index.json` / `index.html`、编辑器日志
`run.log`，以及每个先前步骤的输出。报告根目录保留 `latest.txt` 和 `history.csv`——每次运行每个测试一行；在干净工作区运行中，同一提交、同一预设下既通过又失败的测试被列为
**flaky**。

`known-issues.json` 列出目前故意为红色的测试（带有原因、决定和日期）；被列出的失败不计入，但崩溃、ensure 或超时仍然计入。当被列出的测试通过时，摘要会说明——把
该条目移除，这样下一次回归才会再次变红。

<a id="pictures-and-goldens"></a>

### 图片与 golden

像素测试用于与 golden 图像比对的每张图片都会写入 `<project>/Saved/DreamGUITests/Captures/<name>.png`，
无论是否匹配；不匹配时，旁边的 `<name>.diff.png` 会用洋红色标出不同的像素。
golden 位于 `Source/DreamGUITests/Resources/Golden/`。尚无 golden 的图片会带警告通过：查看它，
一旦正确就复制到那里。当渲染器有意变更时，在编辑器命令行上加 `-DreamGUIWriteGoldens` 会将每张图片覆盖到其 golden。

`Perf` 预设会写入 `Saved/DreamGUITests/Perf/Benchmark.json` 和一份 CPU trace；`perf_report.py show / compare /
insights` 会读取它们。任何东西都不会因时间而失败：一个数字只有与同一台机器上另一次运行的数字放在一起才有意义。

<a id="static-checks-1"></a>

## 静态检查

```powershell
python Tools/Tests/static_checks.py
python Tools/Tests/static_checks.py --list-rules
```

构建之前跑的便宜检查。UHT 和 MSVC 形状的规则抓会让构建停下的东西；测试规则让套件守它自己的约定。

| 规则 | 级别 | 为什么 |
| --- | --- | --- |
| `reflected-name` | error | UHT 拒绝同名的第二个反射类型或委托 |
| `shadowed-property` | error | UHT 拒绝遮住祖先 UPROPERTY 的 UPROPERTY |
| `category-required` | error | BuildPlugin 把插件当引擎插件编译，那里 UHT 拒绝没有 Category 的公开属性或 Blueprint 可调用函数 |
| `redeclared-function` | error | UHT 拒绝 override 上的 `UFUNCTION()`，以及和祖先 UFUNCTION 同名的 UPROPERTY |
| `ufunction-param` | error | UHT 拒绝和类的 UPROPERTY 同名的 UFUNCTION 参数 |
| `param-hides-member` | error | C4458 在这个构建里是错误 |
| `test-unreadable` | error | 运行器读不懂的测试没法被选中、计数或预期 |
| `test-class-unique` | error | 同一个类的两个测试声明会定义两次 `RunTest` |
| `test-path-unique` | error | 框架按全名记测试，第二个会丢 |
| `test-path-format` | error | 预设、过滤器和覆盖矩阵都读这个名字：`DreamGUI.<Area>.<Sentence>` |
| `test-flags` | error | 没有 `EditorContext` 的测试在编辑器里永远不跑 |
| `test-tags` | error | 框架按给出的全名归档标签，打错字就静悄悄地归到一个不存在的测试下 |
| `hand-fed-hit` | error | 直接把命中结果喂给事件系统的测试，测的是没人能产生的指针——去驱动指针 |
| `pixels-need-rhi` | error | `-nullrhi` 下什么都不画；没有 `NonNullRHI` 的像素测试会在那里运行并失败，或者更糟，通过 |
| `rig-needs-bindtest` | error | 没绑定的驱动装置不向测试报告任何东西；它的步骤失败了测试也不失败 |
| `plan-label` | error | 计划标签不属于代码、测试名或注释 |
| `eol` | warn | 混合换行符让文件之后的每个 diff 都很吵 |
| `layering` | error | 包含了上层的下层模块没法和它拆开 |
| `layering-stale` | error | 待拆的边的清单只减不增：已经不存在的边要从清单上拿掉 |
| `engine-private-path` | error | 引擎的 `Private` 和 `Internal` 头文件会不打招呼地在版本之间变化；插件只通过公开头文件读引擎 |

一个应当保留的发现，可以就地放行（写在那一行或上一行），或者写进
`static-checks-allow.json`（路径 glob、可选的正则、理由）：

```cpp
HitContainer.HitResult.Widget = Target;   // static-checks: allow(hand-fed-hit) this test is about the event system's handling of a given hit
```

**分层规则**让运行时文件守住模块分层——最底下是 `DreamGUIRenderer` 和 `DreamTween`，
然后是核心 `DreamGUI`、输入 `DreamGUIInput`、并列的 `DreamGUIControls` 和 `DreamGUIExtensions`，最上面是
`DreamGUISamples`。`module-owners.csv` 说明每个文件属于哪个模块；一个文件只能包含自己模块和更低层的头文件，
不能包含更高层或同层兄弟的（`layering`）。仍然这样做的包含列在
`layering-allow.json` 里，这张清单只会变短。

**覆盖**：`Tools/Tests/COVERAGE.md` 是一张"控件 × 输入方式 × 配置"的表，每一格要么被一个驱动测试覆盖、
要么声明不适用并给出理由、要么是个洞。测试用标签认领格子（`[Pointer]`、`[Touch]`、
`[Nav]`、`[Text]` × `[Animated]`、`[Disabled]`、`[Scaled]`、`[World]`）；`coverage_matrix.py` 画出这张表，
`--fail-on-holes` 让它成为一道闸。

另外还有 `Invoke-DreamGUINightly.ps1` 用于每晚运行（把宿主的插件工作树移到一个提交，跑一组预设，写一份和上一次对比的摘要），
以及一个可选的 pre-push 钩子（`Install-DreamGUIHooks.ps1`，推送前跑 `Quick`）。

<a id="the-test-host"></a>

## 测试宿主

`Tools/TestHost/` 是一个最小的 Unreal 项目，只为构建 DreamGUI 和跑它的套件而存在，和你真正工作的项目分开：

- **你的编辑器可以开着。** 宿主的 `Plugins/DreamGUI` 是 DreamGUI 仓库的一个独立 **git worktree**，有自己的
  `Binaries/` 和 `Intermediate/`，所以构建和运行它不碰你的编辑器加载的任何东西。
- **别的会话改不到它。** 它测试的是工作树检出的已提交内容，不是你工作副本里未提交的状态。
- **"项目能构建"不等于"插件能构建"。** 宿主只启用 DreamGUI 和 Enhanced Input，没有自己的代码（除了下面的冒烟探针），
  能暴露插件忘了声明的依赖。它用的是 `UDreamGameViewportClient`，和 README 对游戏的要求一样。

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -WhatIf   # see what it would do
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -Root <host dir> -RepoPath <DreamGUI checkout> -Branch <branch>
```

脚本随时可以重跑：缺的模板文件补上，一样的不动，不一样的列出来并保留（`-Force` 才替换，每个旧文件留一份
`.bak`）；已有的 worktree 从不删除、重置或切换。`-Root` 默认不接受 C 盘，除非
`-AllowSystemDrive`。之后运行器在宿主的 `.uproject` 存在时会自动用它。

宿主每次运行都会加载**旧资产夹具**：插件在 1.0.0 保存的 widget Blueprint 和一个关卡——之后的版本都要能继续加载的基线——
以及保存时它们的快照。`DreamGUI.Compatibility` 测试拿它们和快照对比，没有重定向就移动或改名了的类、改动中丢了的属性都会在这里现形。
第一套夹具是 2026-09-28 在任何类在模块之间移动之前保存的，靠插件的 CoreRedirects 才能加载；1.0.0 不再带重定向，所以它被一套
1.0.0 保存的夹具换掉了。夹具是输入，永远不是输出——写夹具的命令拒绝覆盖已存在的夹具。

<a id="the-packaged-text-smoke-test-and-the-release-gate"></a>

## 打包文本冒烟测试与发布闸门

套件的其余部分都在编辑器里、未烹饪的内容上跑。这一个是**打包的游戏**：一屏依赖烹饪和打包预设的文本用例——从烹饪后的字节读的字体、
按文化的回退、彩色 emoji、coverage 字形画的小字、两端对齐的段落、日文换行、安全区——由探针逐字段记下来，再和同一份构建在未烹饪
内容上的运行对比。

| 部件 | 在哪 |
| --- | --- |
| 屏幕 | `Tools/TestHost/Template/DUI/TextSmoke.dui`，拷到宿主的 `DUI/` |
| 资产 | `/Game/DreamGUISmoke`，由 `Tools/TestHost/make_text_smoke_assets.py` 生成 |
| 探针 | 宿主游戏模块里的 `DreamGUIPackagedSmoke.cpp`，由 `-DreamGUITextSmoke=<dir>` 打开，Shipping 也可以 |
| 对比 | `Tools/Tests/compare_text_smoke.py`，两次运行一致时退出码 0 |

探针等玩家就位，把 `WBP_TextSmoke` 放上视口，等每个字形落地、小字稳定，然后写出 `TextSmoke.json`（每段文本的显示列表、
字体的回答、回退条目、ICU 的回退、安全区、帧时间、`DreamGUI.Memory Json` 的内存报告）和视口截图 `TextSmoke.png`，
再让游戏退出。整个流程——构建编辑器目标、生成资产、构建游戏目标的 Development 和 Shipping、未烹饪的参考运行、带 `-I18NPreset=EFIGSCJK` 的
烹饪打包、打包后的运行、对比——要构建两个目标并烹饪，在作者的机器上大约一个小时；写成一个脚本放后台跑。完整命令在
`Tools/TestHost/README.md` 里。

**发版闸门**是一个版本打 tag 之前要过的：BuildPlugin（为编辑器、为游戏目标的 Development 和 Shipping 编译插件），加上这个打包文本冒烟测试。**闸门没为某个版本跑过之前，那个版本的打包游戏就没有被运行过。** Win64 是唯一被构建和运行过的平台，见
[平台](../guides/platforms.md)。
