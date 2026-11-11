# 为 DreamGUI 做贡献

[English](CONTRIBUTING.md) · [简体中文](CONTRIBUTING.zh-CN.md)

感谢你的帮助。本页说明怎么报告问题、怎么提出改动，以及一个 pull request 合并前需要什么。
如果你用 AI agent，先让它读 [AGENTS.md](AGENTS.md)。

## 贡献方式

- **问题反馈。** 结果不对、崩溃，或行为与文档不符。
- **功能与语法提案。** DreamGUI 或 `.dui` 现在表达不了、做不到的事。
- **文档与翻译修正。** 有错、看不懂、缺内容，或翻译有问题的页面。
- **小修。** 错别字、原因明显的 bug、缺的测试。直接开 pull request。
- **功能。** 先开提案 issue，在那里把形状商量好再写代码。

## 报告问题

1. 用 [Issue 表单](https://github.com/TypeDreamMoon/DreamGUI/issues/new/choose)。空白 issue 已关闭。
2. 跑环境信息命令，把输出贴进表单。在游戏工程的根目录下：

   ```text
   pwsh -NoProfile -File Plugins/DreamGUI/Tools/Get-DreamGUIEnvironment.ps1
   ```

   它不启动任何东西，也不打印路径。`powershell`（Windows PowerShell 5.1）也能跑。插件不在工程的
   `Plugins` 目录下时，加 `-Project <路径>/<Project>.uproject`。
3. 给出能复现问题的最小 `.dui`，要能独立编译。
4. 有诊断码（`DUInnnn`）就写上，再附报错附近的几行日志。

AI 工具写出的 `.dui` 编不过，或用了不存在的名字？请用「AI 写错了」表单。这类反馈用来改进 agent 指南和示例。

## 提问与想法

用法提问、设计想法、作品展示请去 [Discussions](https://github.com/TypeDreamMoon/DreamGUI/discussions)，
不要开 issue。

## 开发环境

- Unreal Engine 5.8，源码版或官方安装版都行。只有 Win64 编译过、跑过。
- PowerShell 7.2 或更高（`pwsh`）、`PATH` 上名为 `python` 的 Python 3.8 或更高，以及 git。
- 一个宿主工程，本仓检出在它的 `Plugins/DreamGUI`。测试宿主脚本能建一个最小宿主，插件是你克隆的 git worktree：

  ```text
  pwsh -NoProfile -File Tools/TestHost/New-DreamGUITestHost.ps1 -Root <宿主目录> -RepoPath <你的克隆> -Branch <你的分支> -Detach -EngineRoot <引擎根目录>
  ```

  它的默认值是维护者本机的，所以四个值都要传。细节见
  [Tools/TestHost/README.md](Tools/TestHost/README.md) 和 [自动化测试](Docs/tools/tests.md)。
- 宿主脚本和测试运行器默认拒绝 C 盘。机器只有 C 盘时，设 `DREAMGUI_ALLOW_DRIVE_C=1`。
- 测试运行器会先编译宿主再测。编译整个编辑器 target，永远不要用 `-Module=`。

## 开 pull request 之前

跑 `Quick` 预设。它会跑静态检查、编译，再无头跑测试：

```text
pwsh -NoProfile -File Tools/Tests/Invoke-DreamGUITests.ps1 -Project "<Project>/<Project>.uproject" -Engine "<引擎根目录>"
```

先关掉这个工程的所有编辑器。退出码 0 是绿。把以 `RESULT` 开头的那一行贴进 pull request；完整报告在工程的
`Saved/DreamGUITestReports/<时间戳>-Quick/summary.md`。像素测试要真实 RHI，用 `-Preset Rhi` 跑。
全部预设见 [Tools/Tests/README.md](Tools/Tests/README.md)。

在同一个 pull request 里一并更新：

- **文档。** `Docs/` 里描述你改动的那一页。只改英文，见下文。
- **CHANGELOG.md。** 工程能察觉的改动，在 `## Unreleased` 下记一条（`### New`、`### Changed` 或 `### Fixed`）。
- **诊断码。** 新的 `DUInnnn` 码加进 `EDreamUIDiagnosticCode`，并写进两张表：`Docs/diagnostics/index.md` 和
  `Docs/diagnostics/codes.md`。码永不重新编号、永不复用。
- **类参考。** `Docs/Reference/` 由反射生成。改了反射类型，就用 `DreamGUIReferenceDocs` commandlet 重新生成；
  永远不要手改。
- **模块归属。** 新增或移动的运行时文件，在 `Tools/Tests/module-owners.csv` 里加上它的一行。
- **测试。** 新测试命名为 `DreamGUI.<Area>.<Sentence>`。加了测试就调高 `Tools/Tests/presets.json` 里该预设的下限。
- **属性 meta。** 新的 meta 标签一律以 `DreamGUI_` 开头（[属性 meta 标签](Docs/guides/property-meta.md)）。
- **编辑器界面文字。** 字符串用 `LOCTEXT`。中文翻译在 `DLoc/`，用 DreamLoc 维护。你加了或改了界面文字而没有用
  DreamLoc，请在 pull request 里说明。

`Tools/Tests/Install-DreamGUIHooks.ps1` 能装一个可选的 pre-push 钩子，替你跑 `Quick`。

## 提交与 pull request

- 提交要小，一个主题一个。标题写成 `type(scope): sentence`，例如
  `fix(text): every glyph's underlay draws before any glow`。
- 分支名用同一套类型：`feat/…`、`fix/…`、`docs/…`。
- 代码、注释、测试名和提交说明里不写计划标签（wave、batch、阶段编号）。
- 任何文件里都不写本机绝对路径和个人信息。路径写成 `Plugins/DreamGUI/...`。
- 按模板填写 pull request，链上它修的 issue。

## AI 辅助的贡献

欢迎 AI 辅助的 pull request，要求和其他 pull request 一样。

- 在 pull request 里写明用了什么工具和模型。
- 贴出你跑的测试命令的结果行。「应该没问题」不算证据。
- 每一行都由你负责。提交前读懂整个 diff。
- 没有测试证据的批量 AI 生成 pull request 会被关闭。
- 开工前让 agent 读 [AGENTS.md](AGENTS.md)。

## 文档与翻译

- `Docs/` 下的英文是源。文档站 <https://gui.toolchain.64hz.cn> 由它生成。
- `Docs/zh-CN/` 在改动进入 `main` 后由 CI 用 [dreamdocs](https://github.com/TypeDreamMoon/dreamdocs) 翻译，
  作为单独的 pull request 送来。不要在你的 pull request 里翻译。
- 要改一句中文，直接改中文页面。下一次翻译会保留你的修改。
- 永远不要改 `Docs/zh-CN/.align/`。它记录每段中文译自哪段英文。
- `README.zh-CN.md` 和 `CONTRIBUTING.zh-CN.md` 跟英文页面一起手工维护。

## 许可证

DreamGUI 使用 [MIT 许可证](LICENSE)。你的贡献按同样的条款授权。不签 CLA。代码的大部分内容是 Lex Liu 在
[LGUI / LexUI](https://github.com/liufei2008/LGUI) 里的作品，同样是 MIT；请保留它的声明。

## 行为准则与安全

- [行为准则](https://github.com/TypeDreamMoon/.github/blob/main/CODE_OF_CONDUCT.md)
- [安全策略](https://github.com/TypeDreamMoon/.github/blob/main/SECURITY.md)。安全漏洞不要在公开 issue 里报告。
