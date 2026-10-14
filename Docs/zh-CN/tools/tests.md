---
title: 自动化测试
description: 插件自带的自动化测试套件（一次全量运行超过 2400 个测试）、按预设运行的 Invoke-DreamGUITests.ps1、静态检查及其规则、测试宿主项目，以及发版前的打包文本冒烟测试。
---

自动化测试套件是插件的一部分——上游没有测试。它在 `DreamGUITests` 模块里（只在编辑器中加载），测试名的形式是
`DreamGUI.<领域>.<一句话>`，比如 `DreamGUI.Button.ATapOnTheButtonClicksItOnce`；Tween 的测试在 `DreamTween.*` 下。
一次 `All` 运行跑**超过 2400 个**测试。

<a id="最简单的跑法"></a>

## 最简单的跑法

在编辑器的控制台里：

```text
Automation RunTests DreamGUI
```

或者无头地：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -ExecCmds="Automation RunTests DreamGUI" -unattended -nopause -NullRHI -TestExit="Automation Test Queue Empty"
```

`RunTests` 的过滤器按 `+` 拆开、各项取"或"：`StartsWith:X` 匹配以 `X.` 开头的名字，`^X$` 精确匹配，`Group:X` 展开
ini 里的组，其余是不分大小写的**子串**。过滤器里不能有逗号、分号和引号——`-ExecCmds` 按逗号拆，Automation 命令按分号拆。

`-nullrhi` 下引擎会自己丢掉所有标了 `NonNullRHI` 的测试（读像素的那些），所以要跑它们得用 `-RenderOffScreen`：有真 RHI、
没有窗口。

<a id="测试运行器invoke-dreamguitestsps1"></a>

## 测试运行器：Invoke-DreamGUITests.ps1

真正日常用的是 `Tools/Tests/Invoke-DreamGUITests.ps1`。它负责构建、运行和判定，并告诉你实际跑了多少个测试。
需要 PowerShell 7.2+（`pwsh`）、`PATH` 上的 Python 3.8+（`python`）和 git。

```powershell
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1                        # Quick：构建，然后跑无头套件
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Preset Rhi -NoBuild
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Filter "StartsWith:DreamGUI.Button" -Repeat 5
pwsh -NoProfile -File Tools\Tests\Invoke-DreamGUITests.ps1 -Project <path>\MyGame.uproject -Engine <engine root>
```

它按顺序做六件事：

<a id="预检"></a>

### 预检
引擎和项目存在；报告目录不在 C 盘（`-AllowSystemDrive` 或 `DREAMGUI_ALLOW_DRIVE_C=1` 放行）；Python 能应答；没有编辑器的
命令行里带着这个 `.uproject`——开着的编辑器会占住构建要替换的 DLL（LNK1104），还会和测试共用 `Saved`。所以默认拒绝
（退出码 2），除非 `-AllowEditorOpen`。

<a id="静态检查"></a>

### 静态检查
`static_checks.py`，见下文。`-SkipStaticChecks` 跳过。

<a id="构建"></a>

### 构建
完整的编辑器目标，带 `-NoEngineChanges`：任何会重编或重写引擎文件的构建在执行第一个动作之前就被拒绝。**从不用
`-Module=`**——受限构建不重写插件的模块清单，清单里少一个模块，整个插件就加载失败。`-NoBuild` 跳过。

<a id="清单"></a>

### 清单
插件的 `Binaries\Win64\UnrealEditor.modules` 必须带着引擎的 BuildId，并列出 `DreamGUI.uplugin` 在编辑器里构建的每一个模块，
每个都有一个非空的 DLL。`-NoBuild` 时也检查。

<a id="运行"></a>

### 运行
`UnrealEditor-Cmd.exe` 加 `Automation RunTests <过滤器>`，再加预设的参数；超过预设的超时（`-TimeoutMinutes` 覆盖）就连同它
启动的一切一起杀掉。`-Repeat N` 在一次构建后跑 N 次，每次一个新编辑器。

<a id="判定"></a>

### 判定
`digest.py` 读引擎的 JSON 报告和日志，下文。

测哪个项目：`-Project`，否则环境变量 `DREAMGUI_TEST_PROJECT`，否则测试宿主（存在的话）。被测的插件是那个项目
`Plugins` 目录里的 DreamGUI，不一定是运行器所在的那份——横幅会打印它的路径、分支和提交。引擎：`-Engine`，否则
`DREAMGUI_ENGINE`。

<a id="预设"></a>

### 预设

定义在 `presets.json` 里，运行器自己维护，不依赖引擎的 ini 组。

| 预设 | 跑什么 | 编辑器参数 |
| --- | --- | --- |
| `Quick` | 除 PIE 层（`DreamGUI.Pie.*`）以外的所有 `DreamGUI.*` 和 `DreamTween.*` | `-nullrhi` |
| `Interaction` | 声明在 `Private/Interaction` 和 `Private/Driver/Tests` 下的测试 | `-nullrhi` |
| `Designer` | `DreamGUI.Designer.*` | `-nullrhi` |
| `Rhi` | 所有标了 `NonNullRHI` 的，PIE 层和基准除外 | `-RenderOffScreen` |
| `Validate` | 同上，开 RHI 验证层 | `-RenderOffScreen -rhivalidation` |
| `Pie` | `DreamGUI.Pie.*`（其中 `NonNullRHI` 的在 `PieRhi` 里跑） | `-nullrhi` |
| `PieRhi` | `DreamGUI.Pie.*` 里 `NonNullRHI` 的，真 RHI | `-RenderOffScreen` |
| `Exit` | `DreamGUI.Lifecycle.Smoke.*`，然后编辑器自己退出，之后搜索它的日志 | `-nullrhi` |
| `Perf` | 基准 `DreamGUI.Performance.*`，独占一个编辑器 | `-RenderOffScreen` |
| `All` | 全部，一个编辑器、真 RHI | `-RenderOffScreen` 加关掉 GPU Scene 预留缓冲的 `-dpcvars` |

`All` 关掉 GPU Scene 的预留缓冲，是因为这个套件建的世界太多：每个测试世界的场景要预留 8 GiB 的 GPU 地址空间，销毁的世界要等下一次
垃圾回收（编辑器大约一分钟一次）才释放场景，而套件每秒建二十个世界，几秒内地址空间就用完了，D3D12 随后移除设备。无头时这些世界在
GPU 上不花任何东西。

每个预设有一个**下限**：至少要真正跑这么多个测试，少了就是基础设施失败（退出码 2）。它挡住的是"测试模块没加载，于是 0 个测试、
0 个失败"冒充通过。下限只升不降。

<a id="退出码与报告"></a>

### 退出码与报告

| 码 | 含义 |
| --- | --- |
| 0 | 绿：跑了的测试全过（已知问题除外） |
| 1 | 红：至少一个不在已知问题里的测试失败 |
| 2 | 没法判定：静态检查、构建、清单、崩溃、ensure、超时、缺报告、少于下限、该跑的声明测试没跑，或编辑器以非零码退出 |

**有 ensure 的运行不算绿**，即使每个测试都过了：ensure 是测试没断言到的 bug。

报告在 `<project>\Saved\DreamGUITestReports\<时间戳>-<预设>\`（`-ReportDir` 改根目录）。要读的是 `summary.md`：结论、计数、
每个失败和它的第一条错误、已知问题、ensure、崩溃、慢测试和不稳定的测试。旁边有给脚本用的 `digest.json`、引擎自己的
`index.json` / `index.html`、编辑器日志 `run.log`，以及每一步的输出。报告根目录维护 `latest.txt` 和 `history.csv`——后者每次运行
每个测试一行，同一提交、同一预设、干净工作区下既过又挂过的测试被列为 **flaky**。

`known-issues.json` 列出暂时故意是红的测试（带原因、决定和日期）；列出的失败不计入结论，但崩溃、ensure 和超时照算。
一个列出的测试通过了，摘要会说出来——把条目删掉，下一次回归才会再变红。

<a id="图片和-golden"></a>

### 图片和 golden

每个和 golden 图对比的像素测试，不管匹不匹配，都把图写到 `<project>/Saved/DreamGUITests/Captures/<name>.png`；不匹配时旁边的
`<name>.diff.png` 用品红标出不同的像素。golden 在 `Source/DreamGUITests/Resources/Golden/`。还没有 golden 的图会带警告通过——
看一眼，对了就拷过去。渲染器有意改变时，编辑器命令行加 `-DreamGUIWriteGoldens` 覆盖所有 golden。

`Perf` 预设写 `Saved/DreamGUITests/Perf/Benchmark.json` 和一份 CPU trace；`perf_report.py show / compare / insights` 读它们。
没有任何东西会因为时间而失败：一个数只有和同一台机器上另一次运行的数放在一起才有意义。

<a id="静态检查-1"></a>

## 静态检查

```powershell
python Tools/Tests/static_checks.py
python Tools/Tests/static_checks.py --list-rules
```

便宜的检查，跑在构建之前：UHT 和 MSVC 形状的规则抓会让构建停下的东西，测试规则让套件守它自己的约定。

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
| `pixels-need-rhi` | error | `-nullrhi` 下什么都不画；没有 `NonNullRHI` 的像素测试会在那里失败，或者更糟，通过 |
| `rig-needs-bindtest` | error | 没绑定的驱动装置不向测试报告任何东西，它的步骤失败了测试也不失败 |
| `plan-label` | error | 计划标签不属于代码、测试名或注释 |
| `eol` | warn | 混合换行符让文件之后的每个 diff 都很吵 |
| `layering` | error | 包含了上层的下层模块没法和它拆开 |
| `layering-stale` | error | 待拆的边的清单只减不增：已经不存在的边要从清单上拿掉 |
| `engine-private-path` | error | 引擎的 `Private` 和 `Internal` 头文件会不打招呼地变；插件只通过公开头文件读引擎 |

一个应当保留的发现，可以就地放行（写在那一行或上一行），或者写进 `static-checks-allow.json`（路径 glob、可选的正则、理由）：

```cpp
HitContainer.HitResult.Widget = Target;   // static-checks: allow(hand-fed-hit) this test is about the event system's handling of a given hit
```

**分层规则**让运行时文件守住模块分层——最底下是 `DreamGUIRenderer` 和 `DreamTween`，然后是核心 `DreamGUI`、输入
`DreamGUIInput`、并列的 `DreamGUIControls` 和 `DreamGUIExtensions`，最上面是 `DreamGUISamples`。`module-owners.csv` 说明每个文件属于
哪个模块，一个文件只能包含自己模块和更低层的头文件，不能包含更高层或同层兄弟的（`layering`）。仍然这样做的包含列在
`layering-allow.json` 里，这张清单只会变短。

**覆盖**：`Tools/Tests/COVERAGE.md` 是一张"控件 × 输入方式 × 配置"的表，每一格要么被一个驱动测试覆盖、要么声明不适用并给出理由、
要么是个洞。测试用标签认领格子（`[Pointer]`、`[Touch]`、`[Nav]`、`[Text]` × `[Animated]`、`[Disabled]`、`[Scaled]`、`[World]`），
`coverage_matrix.py` 画出这张表，`--fail-on-holes` 让它成为一道闸。

另外还有每晚运行的 `Invoke-DreamGUINightly.ps1`（把宿主的插件工作树移到一个提交，跑一组预设，写一份和上一次对比的摘要），以及一个可选的
pre-push 钩子（`Install-DreamGUIHooks.ps1`，推送前跑 `Quick`）。

<a id="测试宿主"></a>

## 测试宿主

`Tools/TestHost/` 是一个最小的 Unreal 项目，只为构建 DreamGUI 和跑它的套件而存在，和你真正工作的项目分开：

- **你的编辑器可以开着。** 宿主的 `Plugins/DreamGUI` 是 DreamGUI 仓库的一个独立 **git worktree**，有自己的 `Binaries/` 和
  `Intermediate/`，所以构建和运行不碰你的编辑器加载的任何东西。
- **别的会话改不到它。** 它测试的是工作树检出的已提交内容，不是你工作副本里未提交的状态。
- **"项目能构建"不等于"插件能构建"。** 宿主只启用 DreamGUI 和 Enhanced Input，没有自己的代码（除了下面的冒烟探针），
  能暴露插件忘了声明的依赖。它用的是 `UDreamGameViewportClient`，和 README 对游戏的要求一样。

```powershell
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -WhatIf   # 看它会做什么
pwsh -NoProfile -File Tools\TestHost\New-DreamGUITestHost.ps1 -Root <host dir> -RepoPath <DreamGUI checkout> -Branch <branch>
```

脚本随时可以重跑：缺的模板文件补上，一样的不动，不一样的列出来并保留（`-Force` 才替换，旧文件留一份 `.bak`）；已有的 worktree
从不删除、重置或切换。`-Root` 默认不接受 C 盘（`-AllowSystemDrive` 放行）。之后运行器在宿主的 `.uproject` 存在时会自动用它。

宿主每次运行都会加载**旧资产夹具**：插件在 1.0.0 保存的 widget Blueprint 和一个关卡——之后的版本都要能继续加载的基线——
以及保存时它们的快照。`DreamGUI.Compatibility` 测试拿它们和快照对比，没有重定向就移动或改名了的类、改动中丢了的属性都会在这里现形。
第一套夹具是 2026-09-28 在任何类在模块之间移动之前保存的，靠插件的 CoreRedirects 才能加载；1.0.0 不再带重定向，所以它被一套
1.0.0 保存的夹具换掉了。夹具是输入，永远不是输出——写夹具的命令拒绝覆盖已存在的夹具。

<a id="打包文本冒烟测试与发版闸门"></a>

## 打包文本冒烟测试与发版闸门

套件的其余部分都在编辑器里、未烹饪的内容上跑。这一个是**打包的游戏**：一屏依赖烹饪和打包预设的文本用例——从烹饪后的字节读的字体、
按文化的回退、彩色 emoji、coverage 字形画的小字、两端对齐的段落、日文换行、安全区——由探针逐字段记下来，再和同一份构建在未烹饪
内容上的运行对比。

| 部件 | 在哪 |
| --- | --- |
| 屏幕 | `Tools/TestHost/Template/DUI/TextSmoke.dui`，拷到宿主的 `DUI/` |
| 资产 | `/Game/DreamGUISmoke`，由 `Tools/TestHost/make_text_smoke_assets.py` 生成 |
| 探针 | 宿主游戏模块里的 `DreamGUIPackagedSmoke.cpp`，由 `-DreamGUITextSmoke=<dir>` 打开，Shipping 也可以 |
| 对比 | `Tools/Tests/compare_text_smoke.py`，两次运行一致时退出码 0 |

探针等玩家就位，把 `WBP_TextSmoke` 放上视口，等每个字形落地、小字稳定，然后写出 `TextSmoke.json`（每段文本的显示列表、字体的回答、
回退条目、ICU 的回退、安全区、帧时间、`DreamGUI.Memory Json` 的内存报告）和视口截图 `TextSmoke.png`，再让游戏退出。整个流程——构建
编辑器目标、生成资产、构建游戏目标的 Development 和 Shipping、未烹饪的参考运行、带 `-I18NPreset=EFIGSCJK` 的烹饪打包、打包后的运行、
对比——要构建两个目标并烹饪，在作者的机器上大约一个小时，写成一个脚本放后台跑。完整命令在 `Tools/TestHost/README.md` 里。

**发版闸门**是一个版本打 tag 之前要过的：BuildPlugin（为编辑器、为游戏目标的 Development 和 Shipping 编译插件），加上这个打包文本冒烟测试。
**闸门没为某个版本跑过之前，那个版本的打包游戏就没有被运行过。** Win64 是唯一被构建和运行过的平台，见[平台](../guides/platforms.md)。
