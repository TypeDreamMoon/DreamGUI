---
title: "VS Code 扩展"
description: "DreamGUI Language Support 的安装、它能做什么、补全数据 DUI/.dui-symbols.json 从哪来、与正在运行的编辑器之间的桥，以及它的命令和设置。"
---

[DreamGUI Language Support](https://github.com/TypeDreamMoon/dreamgui-language-support) 是 `.dui` 的 VS Code 扩展：高亮、补全、悬停、导航、重命名、格式化、诊断，以及一座通往正在运行的 Unreal 编辑器的桥。它从不猜测语言——补全数据来自插件导出的符号文件，语义判定来自编译器自己的诊断。

<a id="installing"></a>

## 安装

扩展的 id 是 `typedreammoon.dreamgui-language-support`，需要 VS Code 1.85 或更高版本。它在 [VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=typedreammoon.dreamgui-language-support) 上：在扩展视图里搜索 *DreamGUI Language Support*，或者：

```powershell
code --install-extension typedreammoon.dreamgui-language-support
```

`.vsix` 也在仓库的 GitHub releases 上，可用 `code --install-extension dreamgui-language-support-<version>.vsix` 或在 VS Code 里使用 *Extensions: Install from VSIX...* 安装。

0.9.1 及更早版本的 id 是 `typedreammoon.dreamui-language-support`（Marketplace 上该名称已被占用，所以 0.9.2 改名）。VS Code 会把它当作另一个扩展，两个同时启用时每条诊断和补全都会出现两次：安装新版后请卸载旧版；扩展在发现旧版时也会提示。

<a id="opening-the-workspace-from-the-editor"></a>

### 从编辑器打开工作区

*Tools ▸ Open DreamUI Workspace (VSCode)*（或关卡工具栏 *Dream* 下拉菜单里的 *DreamUI Workspace*，这两者是同一个）：

1. 刷新符号导出，让补全数据在 VS Code 打开的那一刻就是最新的；
2. 重写 `DUI/DreamUI.code-workspace`——即项目的 `DUI/` 和每个已启用插件的 `DUI/` 根、固定 `*.dui` 关联，并推荐该扩展；当项目还没有 `DUI/` 目录时，这个按钮可以是第一个创建它的操作；
3. 用 VS Code 打开它，找不到则回退到系统默认编辑器，再回退到 Notepad。

通过这个工作区打开而不是直接打开一个文件夹，才能点亮扩展的工作区功能（跨文件索引、解析 `use`）。同一菜单下的 *Rebuild DUI* 会在询问之后重新编译项目中每一个由文本支撑的 widget Blueprint，未加载的会先加载。

<a id="what-it-does"></a>

## 它能做什么

| 功能 | 说明 |
| --- | --- |
| 高亮 | 整个语法，包括绑定表达式、成员路径、`<->`、`+=` / `=` 路由、`use`、组件语法、`viewmodels` 块、`rows` 表和 `timeline` 块；语义高亮按身份着色：节点 id 是它将成为的成员变量，样式是类，资源是只读常量 |
| 补全 | 内建标签、布局容器、组件类、逐类属性、枚举值、槽属性、资源类型、`@` 资源引用、`->` 事件名；组件实例的 props、events 和 slots；`use … as` 别名和命名空间；`viewmodels` 块中的类型以及 `=` 之后的四种来源；`Player.` 之后视图模型的成员（带类型的属性、是否 FieldNotify 以及 `<->` 能否写回；带参数的函数），并按路径的用途筛选——路由提供函数，`<->` 提供可写属性，循环源提供数组——并沿着对象成员（`Player.Stats.`）继续，以及进入对视图模型列表的循环（`for Item in Player.Items` 中的 `Item.`）（自 0.10.0 起） |
| 悬停 | 属性类型和枚举值、UPROPERTY 的说明和可粘贴的类默认值、别名解析到什么、`emit` 的签名、匿名节点会编译成的 id |
| 大纲与导航 | widget 树的大纲（含 `if` / `else` 分支、循环、slots）；`@Name`、样式、别名、`use` 目标、`emit`、props、命名空间名的转到定义；Ctrl+T 在所有 `.dui` 中查找每个 id、样式和资源 |
| 重命名与重构 | F2 重命名样式或资源会改掉所有用处；重命名节点 id 会写入 `(was: OldId)` 子句，让下一次编译迁移图引用、绑定和动画；提取为资源、提取为样式、内联样式 |
| 格式化 | 按块深度缩进，`=` 和箭头两边一个空格，`}` 独占一行——与设计器写回打印的拼写一致，因此两者不会冲突；结果会重新词法分析，改变任何一个 token 的格式化会被拒绝 |
| 诊断 | 编译器自己的代码和措辞、编译器自己的严重级别：词法码、单个文件能确定的结构码，以及符号驱动的检查。编译器始终是权威：它可能接受的内容在这里最多是警告 |
| 解释 | Problems 和悬停中的诊断代码会链接到 [Diagnostics](../diagnostics/index.md) 下它的条目（自 0.9.1 起）；离线时每条诊断都提供“解释 DUInnnn”操作，打开内置的代码表条目：它是什么意思、为什么触发、如何修复 |
| 快速修复 | 声明未知资源（DUI4007）、创建未知样式（DUI3004）、为只存在于另一个文件中的样式或资源添加 `use` |
| 其他 | 每个 `#hex` 字面量上的颜色块和取色器；组件语法的代码片段；*DreamUI: New .dui File*，写出与编辑器 *Create Source File...* 相同的起步内容 |

语言**刻意没有**的东西（编译期条件、宏、变量、伪状态、以 `A | B` 写出的标志枚举、三元表达式、数组字面量……）扩展也不会补全；原因见扩展的 README 和 [.dui 概览](../language/overview.md)。

<a id="the-symbol-file-duidui-symbolsjson"></a>

## 符号文件：DUI/.dui-symbols.json

补全提供的恰好是编译器接受的内容，因为两者读的是**同一份清单**。编辑器模块把编译器知道的内容导出到项目的 `DUI/.dui-symbols.json`：

- **标签**来自构建器自己的表（包括 `Native.Button` 之类的注册表标签）；
- **属性**来自写回扫描所用的同一套反射规则；
- **枚举值**来自它们的 `UEnum`；
- 以及布局容器及其属性、事件、属性说明和类默认值；
- `viewmodels` 行可以命名的类（`UDreamViewModel` 的子类，以及任何不是 widget 且实现了 `INotifyFieldValueChanged` 的类）及其成员——`Player.` 之后的补全读的就是它（文件版本 2；更旧的导出在这里什么都不补全，而不是去猜）。

它在两个时刻写出：**编辑器启动时**（类都已存在之后），以及**按需**，通过控制台命令：

```text
DreamUI.ExportSymbols
```

它只写入一个**已经存在的**项目 `DUI/` 目录：一个从未用过 `.dui` 的项目不应该在某次启动后突然拥有一个。没有 `DUI/` 时，该命令会在日志中说明什么都没写入。

还没有符号文件？在有 `DUI/` 目录的情况下，用 Unreal 编辑器打开一次项目。所有语法驱动的功能没有它也能用，状态栏会说明你处于哪种模式——缺少符号文件是一条带说明的可见警告，而不是静默降级。文件变化后，*DreamUI: Reload Symbols* 会重新读取它。

<a id="the-bridge-to-the-editor"></a>

## 与编辑器之间的桥

编辑器运行时，扩展能拿到单个文件的字符看不到的东西：

- **编译器诊断，实时。** 每次编译一个由文本支撑的类，其判定都会写入
  `DUI/.dui-diagnostics.json`，它们会作为 `dui-compiler` 出现在“问题”面板中——语义层：未知
  属性、错误的值、缺失的绑定函数。一次干净的编译会写入一个空数组，这正是清除该
  文件波浪线的方式。
- **请求与响应。** `Saved/DreamGUI/Bridge/` 中的投放文件夹协议：扩展投放一个请求，
  编辑器轮询、处理并写入响应。`<-` 和 `->` 补全会询问编辑器该类究竟
  声明了什么；`<->` 补全会列出该类的变量（FieldNotify 的排在最前——其余每帧轮询）；
  输入 `/` 会提供所有可嵌套的控件类；`/Game/...` 路径是链接，会在编辑器的内容
  浏览器中打开该资产。
- **在 Unreal 设计器中显示**（Ctrl+Alt+R）会打开设计器并选中光标下的节点；**编译此
  文件**（Ctrl+Alt+B）立即编译，判定出现在“问题”中。设计器是这门语言的预览界面——桥
  只是把你带到那里。
- **反过来**：在设计器工具栏或层级结构上下文菜单中的 *在 VS Code 中显示* 会让
  编辑器写入 `Bridge/reveal-to-editor.json`，然后 VS Code 跳转到该节点所在的行。

**注意：**

  一个项目，一个编辑器。两个编辑器会互相抢对方的请求——这种情况会被记录，但不会加以防御。

<a id="commands-and-settings"></a>

## 命令与设置

| 命令 | 快捷键 | 作用 |
| --- | --- | --- |
| DreamUI: Reload Symbols (.dui-symbols.json) | | 再次读取符号文件 |
| DreamUI: New .dui File | | 写入一个起始 `.dui`（也出现在文件夹的上下文菜单中） |
| DreamUI: Reveal in Unreal Designer | Ctrl+Alt+R | 打开设计器并选中光标下的节点 |
| DreamUI: Compile This File | Ctrl+Alt+B | 让编辑器立即编译此文件 |
| DreamUI: Clear Bridge Caches | | 丢弃从编辑器回答中缓存的内容 |

| 设置 | 默认值 | 含义 |
| --- | --- | --- |
| `dreamui.symbolsPath` | 空 | `.dui-symbols.json` 的显式路径。为空时会搜索每个工作区文件夹的 `DUI/` 目录以及打开文件上方的文件夹 |
