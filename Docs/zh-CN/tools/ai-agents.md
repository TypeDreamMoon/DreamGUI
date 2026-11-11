---
title: 使用 AI 智能体
description: AI 智能体在编写 .dui 时可以借助的资源——AGENTS.md、语言参考、符号文件、无头 DreamUICompileWidgets commandlet 以及诊断文件——以及从提出请求到产出已编译控件蓝图文件的完整循环流程。
---

`.dui` 不在任何模型的训练数据中。仅凭通用知识编写它的智能体会臆造出并不存在的节点类型和属性。它需要的是参考文档、编译器接受的名称清单，以及编译并读取错误的方法。这三者都包含在插件中。DreamGUI 没有 agent skills，也没有 MCP 服务器。

<a id="what-there-is"></a>

## 现有资源

| 组成部分 | 位置 | 为智能体提供什么 |
| --- | --- | --- |
| 智能体指南 | 插件根目录下的 `AGENTS.md`；`CLAUDE.md` 导入它 | 下文的循环流程、智能体容易出错的规则，以及修改插件本身的规则 |
| 语言参考 | `Docs/language/`，从[概览](../language/overview.md) 开始；另有一个[完整示例](../language/example.md) | 每个语法结构，每个主题一页 |
| 示例 | `Content/Samples/HelloDreamGUI.dui`, `Content/Samples/NiagaraEffects.dui` | 可供复制的小型完整文件；测试套件会编译这两者，并在出现任何错误时失败。`Source/DreamGUITests/` 下的 `.dui` 文件是测试输入，而不是示例 |
| 符号文件 | `<Project>/DUI/.dui-symbols.json` | 编译器接受的每一种节点类型、属性、枚举值、事件和视图模型类（[符号文件](vscode.md#the-symbol-file-duidui-symbolsjson)） |
| 无头编译器 | `DreamUICompileWidgets` commandlet | 无需编辑器窗口即可从 `.dui` 创建、编译并保存控件蓝图（[从命令行编译](scripting.md#compiling-from-the-command-line)） |
| 诊断文件 | `<Project>/DUI/.dui-diagnostics.json` | 正在运行的编辑器最近一次编译每个文件时报告的内容 |
| 诊断目录 | [诊断](../diagnostics/index.md) | 每个 `DUInnnn` 代码的含义以及如何修复 |

<a id="giving-the-guide-to-an-agent-in-a-game-project"></a>

## 在游戏项目中把指南交给智能体

智能体会读取其工作文件夹根目录下的说明，这个文件夹是你的游戏项目，而不是插件。在项目自己的说明文件中添加一行：

```text
Before writing or fixing any .dui file, read Plugins/DreamGUI/AGENTS.md and follow its Authoring section.
```

在 Claude Code 中，在项目的 `CLAUDE.md` 里写一行 `@Plugins/DreamGUI/AGENTS.md` 即可导入该指南。

<a id="the-loop"></a>

## 循环流程

1. **阅读**概览、完整示例，以及[常见陷阱](../guides/pitfalls.md) 中的 `.dui` 部分。
2. 在使用每种节点类型和属性之前，先在 `DUI/.dui-symbols.json` 中**查阅**它们。编辑器会在启动时写入该文件，并在控制台执行 `DreamUI.ExportSymbols` 时再次写入，但前提是项目的 `DUI/` 文件夹存在。
3. **编写**文件，放在 `<Project>/DUI/` 下。其中的 `class` 行指定控件蓝图，例如 `class /Game/UI/WBP_Hello`。
4. **编译。** 在编辑器关闭的情况下，把控件列在清单文件 `<Project>/DUI/widgets.json` 中：

   ```json
   { "widgets": [ { "asset": "/Game/UI/WBP_Hello", "source": "Hello.dui" } ] }
   ```

   然后运行：

   ```text
   "<Engine>/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "<Project>/<Project>.uproject" -run=DreamUICompileWidgets -Manifest=DUI/widgets.json
   ```

   清单路径相对于项目文件夹，`source` 相对于 `DUI/`。组件要排在使用它的屏幕之前。只要有失败项，退出码就非零，运行结束时输出 `DreamUICompileWidgets: N compiled, M failed, of K.`
5. **读取错误。** 每条错误都会以 `File.dui(line,col): error DUI3001: …` 的形式打印出来。在[目录](../diagnostics/index.md) 中查找该代码，修正对应行，然后重新编译，直到每个控件都显示 `compiled and saved`。

<a id="with-the-editor-open"></a>

### 在编辑器打开的情况下

该 commandlet 会保存它编译的蓝图，因此不要在编辑器打开项目时运行它。编辑器打开时，请改用以下方式：

- 保存 `.dui` 会重新编译由它构建的**已加载**类（[保存以重新编译](../getting-started/workflow.md#save-to-recompile)）。*Tools ▸ Rebuild DUI* 会重新编译所有以文本为后端的控件蓝图。
- 每次编译后，编辑器都会重写 `DUI/.dui-diagnostics.json`：每个文件一个条目，每个条目包含一个 `{ "code", "severity", "line", "column", "message" }` 列表。`"code": 3001` 即 `DUI3001`。列表为空表示该文件编译无错误。智能体可以读取此文件而不必查看日志。

<a id="what-agents-get-wrong"></a>

## 智能体常犯的错误

- 来自其他 UI 系统的名称：符号文件中不存在的类型或属性会报错（`DUI3003`、`DUI4001`），绝不会被静默忽略。
- 项目中存在样式表时使用 `Style.X = v`：它能编译通过，但在 `StyleSource = Inline` 之前不会有任何效果（[样式](../controls/styles.md#in-dui-and-at-run-time)）。
- 把 `.dui` 放在 `Content/` 下。源文件应放在 `DUI/` 下。
- 清单中的 `asset` 与文件的 `class` 行不一致。
- 在设计器中编辑由 `.dui` 支撑的蓝图的层级结构。层级结构归文件所有；设计器会拒绝结构性编辑。

<a id="when-the-agent-still-gets-it-wrong"></a>

## 当智能体仍然出错时

使用[*AI 写错了*表单](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose) 提交 issue。请提供所用的工具和模型、你原始写下的请求，以及它生成的 `.dui`。这些报告正是指南和示例得以改进的依据。使用智能体为插件本身做贡献：参见[贡献指南](../contributing/index.md)。
