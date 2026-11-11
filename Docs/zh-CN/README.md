<a id="dreamgui-documentation"></a>

# DreamGUI 文档

[English](../README.md) · 简体中文

本目录中的英文文档是插件的规范源文件。网站根据这些文件生成英文页面；中文译文保存在本插件的 `zh-CN/` 下，合并进本插件后即发布。

| 章节 | 从这里开始 |
| --- | --- |
| 概览 | [DreamGUI](index.md) |
| 入门 | [安装](getting-started/installation.md)、[第一个界面](getting-started/first-screen.md)、[工作流程](getting-started/workflow.md) |
| 概念 | [控件](concepts/widgets.md)、[控件树](concepts/widget-tree.md)、[渲染](concepts/rendering.md)、[输入](concepts/input.md) |
| 语言 | [.dui 参考](language/index.md)、[绑定](language/bindings.md)、[设计器写回](language/write-back.md) |
| 控件 | [控件目录](controls/index.md)、[样式](controls/styles.md)、[与 UMG 对照](controls/umg-parity.md) |
| 指南 | [字体与打包](guides/fonts-and-packaging.md)、[迁移](guides/migration.md)、[升级](guides/upgrading.md)、[平台](guides/platforms.md)、[常见陷阱](guides/pitfalls.md) |
| 工具 | [设计器](tools/designer.md)、[调试](tools/debugging.md)、[测试](tools/tests.md)、[脚本](tools/scripting.md)、[AI 代理](tools/ai-agents.md) |
| 贡献 | [报告问题、构建、测试门禁](contributing/index.md) |
| 参考 | [生成的类参考](Reference/index.md) |
| 诊断 | [概览](diagnostics/index.md)、[诊断目录](diagnostics/codes.md) |
| 变更 | [版本历史](changelog/index.md) |

<a id="maintaining-these-documents"></a>

## 维护这些文档

- 使用普通 Markdown 和相对链接编辑主题文件。代码示例和 API 标识符保持其确切拼写。
- `Reference/` 由 `DreamGUIReferenceDocs` 命令从反射生成。请更新源声明或生成器并重新生成它；不要手动编辑生成的页面。
- 旧的 `DuiLanguage.md`、`FontsAndPackaging.md` 和 `Migration.md` 路径是面向现有仓库链接和打包使用者的导航页面。完整文本保存在主题文件中。
- 网站页面 ID 是稳定的。源文件移动会更新网站仓库中的 `docs-sync/config.json`，而不是公开 URL。
- 来自网站 PR [#2](https://github.com/TypeDreamMoon/gui.toolchain.64hz.cn/pull/2) 和 [#3](https://github.com/TypeDreamMoon/gui.toolchain.64hz.cn/pull/3) 的文档新增内容在此维护，包括控件标签、控件树、网格修改器、精灵、歌词、反射绑定和事务性文本写回。

`zh-CN/` 目录树与英文源路径一一对应。旁边的 `zh-CN/.align/` 记录每段中文是从哪一段英文译来的，所以英文改动后只重译改过的段落。要修正译文，直接编辑中文页即可，下次运行会保留你的修改。仅网站使用的说明保留在网站仓库中。

译文由 [dreamdocs](https://github.com/TypeDreamMoon/dreamdocs) 生成和检查：`dreamdocs status` 列出过期和缺失的段落，`dreamdocs translate` 翻译这些段落，`dreamdocs check --plugin .` 是 CI 运行的检查。本插件的设置在 `dreamdocs.json` 中。
