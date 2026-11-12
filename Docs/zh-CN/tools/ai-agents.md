---
title: 使用 AI 智能体
description: AI 智能体在编写 .dui 时可以借助的资源——DreamMCP 端点及其布局与预览、skills、自包含的参考文档、dui.ps1 和 commandlet——以及从提出请求到产出适配屏幕的已编译控件蓝图、把一屏界面做出来的循环流程。
---

`.dui` 不在任何模型的训练数据中。仅凭通用知识编写它的智能体会臆造出并不存在的节点类型和属性；即便名称写对了，它仍然会把界面布局搞得很糟，因为它看不到这些界面。它需要的是参考文档、编译器接受的名称、一种能快速编译并读取错误的办法，以及一种能看到自己所做结果的办法。这些全都在插件里。

<a id="what-there-is"></a>

## 现有资源

| 组成部分 | 位置 | 为智能体提供什么 |
| --- | --- | --- |
| DreamMCP | 通过 [DreamMCP 服务器](https://github.com/TypeDreamMoon/dream-mcp) 以 `dream_gui` 工具的形式访问的[端点](dreammcp.md) | `check`、`compile`、`diagnostics`、`lookup`、`layout`（每个节点的像素矩形和布局问题，用文字描述）、`preview`（一张图片及其数值）、`info`——在用户的编辑器或后台引擎中，只需几分之一秒 |
| skills | `.skill/dream-gui-create`、`-verify`、`-diagnose`；由 `.skill/sync-skills.ps1` 发布到 `.claude/skills/` | 固定步骤，每步一条命令：查名称、编写、检查、布局、修复 |
| 参考文档 | `.skill/reference/`：`dui.md`（整门语言浓缩于一页）、`example.md`、`pitfalls.md`、`controls.md`、`diagnostics.md` | 作者所需的内容，每页均可独立使用；DreamMCP 通过 `reference` 提供它们 |
| 智能体指南 | 插件根目录下的 `AGENTS.md`；`CLAUDE.md` 导入它 | 下文的循环流程，以及智能体容易出错的规则 |
| `dui.ps1` | `.skill/dui.ps1` | 从命令行调用端点的 `check`、`compile` 和 `layout`，供 CI 和没有 DreamMCP 服务器的智能体使用 |
| 符号文件 | `<Project>/DUI/.dui-symbols.json` | 每一种节点类型、属性、枚举值、事件和视图模型类（[符号文件](vscode.md#the-symbol-file-duidui-symbolsjson)）；`lookup` 读取同一张表 |
| commandlet | `-run=DreamUICompileWidgets` | 从清单创建、编译并保存控件蓝图（[从命令行编译](scripting.md#compiling-from-the-command-line)） |
| 诊断目录 | [诊断](../diagnostics/index.md) | 每个 `DUInnnn` 代码的含义以及如何修复，包括布局类代码（DUI8xxx） |

<a id="giving-the-guide-to-an-agent-in-a-game-project"></a>

## 在游戏项目中把指南交给智能体

智能体会读取其工作文件夹根目录下的说明，这个文件夹是你的游戏项目，而不是插件。在项目自己的说明文件中添加一行：

```text
Before writing or fixing any .dui file, read Plugins/DreamGUI/AGENTS.md and follow its Authoring section.
```

在 Claude Code 中，在项目的 `CLAUDE.md` 里写一行 `@Plugins/DreamGUI/AGENTS.md` 即可导入该指南，而 `pwsh -NoProfile -File Plugins/DreamGUI/.skill/sync-skills.ps1` 会发布 skills。更新插件后请再次运行；`-Check` 会说明已发布的副本是否过期。

<a id="the-loop"></a>

## 循环流程

使用 `dream_gui` 工具（调用形式为 `{ "action": ..., "args": { ... } }`）：

1. **阅读**参考文档（`reference`），先看语言页。
2. **查阅**每一种节点类型和属性之后再写：`lookup { "name": "Text" }`、`Text.FontSize`、`VerticalBox`、`Widget`（每个节点都接受的参数）、`slot`（`@slot` 接受的参数）。
3. **写入** `<Project>/DUI/` 下的文件；其 `class` 行指明控件蓝图，例如 `class /Game/UI/WBP_Hello`。
4. **检查**：`check { "file": "DUI/Hello.dui" }` 不写入任何内容，并列出每个错误及其行号和代码。
5. **布局**：`layout { "file": "DUI/Hello.dui", "sizes": "1280x720, 2560x1440" }` 编译屏幕（先编译它放置的组件），并以屏幕作答，一行一个节点——类型、id、`x y w h`、其文本以及是否放得下——还会给出问题：超出屏幕、文本被截断、尺寸为零或小到无法按下、按钮相互重叠、边缘偏差一两个像素、在另一尺寸下会出问题的地方。修正后再次布局，直到不再报告任何问题。
6. **查看**：`preview` 把屏幕绘制成 PNG；看不到图像的智能体则读取其覆盖率、颜色以及难以辨认的文本。

没有 DreamMCP 时，`dui.ps1` 从命令行执行第 4 步和第 5 步（以及 `compile`）。当用户的编辑器正在为该工程提供服务时，它使用该编辑器；否则会为这次调用启动一个无头引擎：

```text
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 check DUI/Hello.dui
pwsh -NoProfile -File Plugins/DreamGUI/.skill/dui.ps1 layout DUI/Hello.dui -Sizes "1280x720"
```

它以 `File.dui(line,col): error DUI3001: …` 的形式打印诊断信息，并以 `RESULT=OK` 或 `RESULT=FAILED` 结束（退出码为 0、1，无法运行时为 2）。此时名称来自 `DUI/.dui-symbols.json`，当工程的 `DUI/` 文件夹存在时，编辑器会在启动时写入它。

<a id="with-the-editor-open"></a>

### 在编辑器打开的情况下

当 *Project Settings ▸ Plugins ▸ Dream GUI ▸ Editor ▸ Enable DreamMCP Endpoint* 开启时（[端点](dreammcp.md)），编辑器自己就会提供 DreamMCP 服务，因此智能体的调用会发往它，而一次编译会把蓝图留在内存中供你保存，就像保存 `.dui` 一样。保存 `.dui` 还会重新编译由它构建的**已加载**类（[保存以重新编译](../getting-started/workflow.md#save-to-recompile)），并且编辑器会在每次编译后重写 `DUI/.dui-diagnostics.json`。

<a id="what-agents-get-wrong"></a>

## 智能体常犯的错误

- 来自其他 UI 系统的名称：符号表中不存在的类型或属性会报错（`DUI3003`、`DUI4001`），绝不会被静默忽略。先用 `lookup`。
- 以中心为基准测量的绝对位置：屏幕在一种尺寸下刚好合适，在另一种尺寸下就会溢出（`DUI8001`、`DUI8006`）。锚定到边缘以及使用 `Spacing` 的盒子在每种尺寸下都能保持。
- 文本位于一个由它决定宽度的盒子中，且没有 `WrapTextAt`：会被测量为尽可能窄（`DUI8002`）。
- 项目中存在样式表时使用 `Style.X = v`：它能编译通过，但在 `StyleSource = Inline` 之前不会有任何效果（[样式](../controls/styles.md#in-dui-and-at-run-time)）。
- 把 `.dui` 放在 `Content/` 下。源文件应放在 `DUI/` 下。
- 在设计器中编辑由 `.dui` 支撑的蓝图的层级结构。层级结构归文件所有；设计器会拒绝结构性编辑。

<a id="when-the-agent-still-gets-it-wrong"></a>

## 当智能体仍然出错时

使用[*AI 写错了*表单](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose) 提交 issue。请提供所用的工具和模型、你原始写下的请求、它生成的 `.dui`，以及 `layout` 对此的说明。这些报告正是指南、技能和布局规则得以改进的依据。使用智能体为插件本身做贡献：参见[贡献](../contributing/index.md)。
