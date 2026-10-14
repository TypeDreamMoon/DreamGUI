---
title: "DreamGUI 文档"
description: "DreamGUI 文档"
---

<a id="dreamgui-documentation"></a>

# DreamGUI 文档

[English](../README.md) · 简体中文

本目录中的英文文档是插件的规范源文件。网站根据这些文件生成英文页面；中文译文保存在本插件的 `zh-CN/` 下，并在插件翻译 PR 经审阅合并后发布。

| 章节 | 从这里开始 |
| --- | --- |
| 概览 | [DreamGUI](index.md) |
| 入门 | [安装](getting-started/installation.md)、[第一个界面](getting-started/first-screen.md)、[工作流程](getting-started/workflow.md) |
| 概念 | [控件](concepts/widgets.md)、[控件树](concepts/widget-tree.md)、[渲染](concepts/rendering.md)、[输入](concepts/input.md) |
| 语言 | [.dui 参考](language/index.md)、[绑定](language/bindings.md)、[设计器写回](language/write-back.md) |
| 控件 | [控件目录](controls/index.md)、[样式](controls/styles.md)、[与 UMG 对照](controls/umg-parity.md) |
| 指南 | [字体与打包](guides/fonts-and-packaging.md)、[迁移](guides/migration.md)、[升级](guides/upgrading.md)、[平台](guides/platforms.md) |
| 工具 | [设计器](tools/designer.md)、[调试](tools/debugging.md)、[测试](tools/tests.md)、[脚本](tools/scripting.md) |
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

`zh-CN/` 树镜像英文源路径。导入的译文未经审阅；[首次迁移快照](TRANSLATIONS.md) 保留了原始导入和缺失译文列表。要查看当前状态，请运行 `python -B Docs/translation-status.py` 或查看网站仓库中的翻译状态 issue。`.translations/` 记录源版本，但不将导入视为批准。仅网站特有的说明仍保留在网站仓库中。

使用 `python -B Docs/translation-status.py` 在本地检查翻译状态；添加 `--all` 以包含所有源文件，或添加 `--json` 以获取结构化输出。这个只读工具不需要网站仓库或 Git 检出。
