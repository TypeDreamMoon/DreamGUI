---
title: 贡献
description: 如何报告问题、提出更改并让拉取请求获得合并——问题表单与环境命令、仓库布局、在宿主项目中构建、Quick 测试门禁、随更改需要更新的内容、文档与翻译流程，以及 AI 辅助贡献。
---

DreamGUI 在 [GitHub](https://github.com/TypeDreamMoon/DreamGUI) 上接收缺陷报告、提案、文档修复和拉取请求。本页是总览。拉取请求必须遵守的检查清单是仓库中的 [CONTRIBUTING.md](../../../CONTRIBUTING.md)。AI 代理从 [AGENTS.md](../../../AGENTS.md) 开始。

<a id="reporting-a-problem"></a>

## 报告问题

Issue 通过表单创建；不提供空白 Issue。

| 表单 | 用途 |
| --- | --- |
| 缺陷报告 | 输出错误、崩溃，或与本文档不一致的行为 |
| 功能或语法提案 | DreamGUI 或 `.dui` 尚无法表达或完成的事情。在提交功能拉取请求前先创建一个提案 |
| AI 写错了 | AI 工具编写的 `.dui` 无法构建、看起来不对，或使用了不存在的名称 |
| 文档或翻译 | 此处页面有误、不清楚、缺失或翻译质量差 |

缺陷报告和 AI 表单要求提供环境命令的输出。请从游戏项目文件夹中运行该命令：

```text
pwsh -NoProfile -File Plugins/DreamGUI/Tools/Get-DreamGUIEnvironment.ps1
```

它会打印插件的版本和提交、引擎的版本和类型、系统、其 UI 语言以及 shell。它不会启动任何内容，也不会打印路径。当插件不在项目的 `Plugins` 文件夹内时，传入 `-Project <path to>/<Project>.uproject`。问题和想法请前往 [Discussions](https://github.com/TypeDreamMoon/DreamGUI/discussions)。

<a id="the-repository"></a>

## 仓库

| 文件夹 | 内容 |
| --- | --- |
| `Source/` | 十三个模块：八个运行时模块、四个编辑器侧模块（`UncookedOnly`）以及测试模块（[模块层](../getting-started/project-layout.md#module-layers)） |
| `Shaders/`, `Config/`, `Content/`, `Resources/` | 各模块加载的内容：着色器、默认设置、字体和其他资源、图标以及 UMG 一致性表 |
| `Docs/` | 本手册。英文是源文；`Docs/zh-CN/` 是其翻译 |
| `Tools/Tests/` | 测试运行器、其预设以及静态检查 |
| `Tools/TestHost/` | 测试套件用于构建和运行的最小宿主项目 |
| `DLoc/` | 编辑器 UI 的翻译，由 DreamLoc 维护 |
| `ThirdParty/` | msdfgen，以单文件副本提交，因此直接克隆即可构建 |

完整布局见[插件文件夹中的内容](../getting-started/project-layout.md#what-is-in-the-plugin-folder)。

<a id="building"></a>

## 构建

你需要 Unreal Engine 5.8（源码版或启动器版）、PowerShell 7.2 或更高版本、Python 3.8 或更高版本以及 git。Win64 是唯一构建和运行的平台。请在将检出内容作为 `Plugins/DreamGUI` 的项目中构建；[测试宿主](../tools/tests.md#the-test-host) 是一个最小项目，其中插件是你的克隆的 git 工作树：

```text
pwsh -NoProfile -File Tools/TestHost/New-DreamGUITestHost.ps1 -Root <host folder> -RepoPath <your clone> -Branch <your branch> -Detach -EngineRoot <engine root>
```

其默认值来自维护者的机器，因此请传入全部四个值。构建完整的编辑器目标，切勿使用 `-Module=`：受限构建会使插件的模块清单过期，随后插件将整体加载失败。

<a id="the-test-gate"></a>

## 测试门禁

每个拉取请求首先运行 `Quick` 预设：

```text
pwsh -NoProfile -File Tools/Tests/Invoke-DreamGUITests.ps1 -Project "<Project>/<Project>.uproject" -Engine "<engine root>"
```

它会运行静态检查、构建、检查模块清单，并在无人值守的编辑器中运行无头测试套件。请先关闭该项目的所有编辑器。退出码 `0` 为绿色，`1` 为红色，`2` 表示无法判定本次运行。它会打印一行以 `RESULT` 开头的内容：将该行粘贴到拉取请求中。其他预设（用于像素测试的 `Rhi`、`Designer`、`Pie`、`All`）、报告、已知问题以及静态检查规则见[自动化测试套件](../tools/tests.md)。`python Tools/Tests/static_checks.py` 仅运行静态检查，不需要引擎。

<a id="what-to-update-with-a-change"></a>

## 更改时需要更新的内容

- 此处描述你所更改行为的页面。
- `CHANGELOG.md`：在 `## Unreleased` 下、位于 `### New`、`### Changed` 或 `### Fixed` 中的一行，用于记录项目能够察觉的任何变化。
- 新增诊断代码：`EDreamUIDiagnosticCode`，以及两张表，[诊断](../diagnostics/index.md) 和 `Docs/diagnostics/codes.md`。诊断代码绝不重新编号或重复使用。
- 反射类型发生更改：使用 `DreamGUIReferenceDocs` commandlet 重新生成[类参考](../Reference/index.md)。绝不手动编辑它。
- 新增或移动的运行时文件：它在 `Tools/Tests/module-owners.csv` 中的行。
- 新增测试：采用 `DreamGUI.<Area>.<Sentence>` 形式的名称，并提高 `Tools/Tests/presets.json` 中预设的下限。
- 新增属性元标签：使用 `DreamGUI_` 前缀（[属性元标签](../guides/property-meta.md)）。

<a id="docs-and-translations"></a>

## 文档与翻译

`Docs/` 下的英文是本站的源文。更改进入 `main` 后，CI 会使用 [dreamdocs](https://github.com/TypeDreamMoon/dreamdocs) 将更改内容翻译到 `Docs/zh-CN/`，并单独打开一个拉取请求，因此贡献只更改英文。中文句子通过编辑中文页面来修正；下次运行会保留该编辑。`Docs/zh-CN/.align/` 记录每个中文段落来自哪个英文段落，且绝不手动编辑。

<a id="ai-assisted-contributions"></a>

## AI 辅助贡献

欢迎按与其他贡献相同的条件提交。请在拉取请求中注明工具和模型，粘贴测试运行中 `RESULT` 那一行，并在提交前审查每一行：打开拉取请求的人对其负责。没有测试证据的批量 AI 生成拉取请求会被关闭。[与 AI 代理协作](../tools/ai-agents.md) 说明了代理需要处理的内容。

<a id="license-and-conduct"></a>

## 许可与行为准则

贡献内容按插件的 [MIT 许可证](../../../LICENSE) 授权；没有 CLA。[行为准则](https://github.com/TypeDreamMoon/.github/blob/main/CODE_OF_CONDUCT.md)与[安全策略](https://github.com/TypeDreamMoon/.github/blob/main/SECURITY.md)适用于本项目的每一个仓库。
