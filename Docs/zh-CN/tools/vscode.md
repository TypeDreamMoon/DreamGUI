---
title: VS Code 扩展
description: DreamGUI Language Support 的安装、它能做什么、补全数据 DUI/.dui-symbols.json 从哪来、和正在运行的编辑器之间的桥，以及命令和设置。
---

[DreamGUI Language Support](https://github.com/TypeDreamMoon/dreamgui-language-support) 是 `.dui` 的 VS Code
扩展：高亮、补全、悬停、跳转、重命名、格式化、诊断，以及一座通往正在运行的 Unreal 编辑器的桥。它从不猜语言——
补全的数据来自插件导出的符号文件，语义层面的判断来自编译器自己的诊断。

<a id="安装"></a>

## 安装

扩展的 id 是 `typedreammoon.dreamgui-language-support`，需要 VS Code 1.85 或更新。它在
[VS Code Marketplace](https://marketplace.visualstudio.com/items?itemName=typedreammoon.dreamgui-language-support)
上：在扩展视图里搜 *DreamGUI Language Support*，或者：

```powershell
code --install-extension typedreammoon.dreamgui-language-support
```

也可以从仓库的 GitHub Release 下载 `.vsix`，用 `code --install-extension dreamgui-language-support-<version>.vsix`
或 VS Code 里的 *Extensions: Install from VSIX...* 安装。

0.9.1 及更早的版本 id 是 `typedreammoon.dreamui-language-support`（Marketplace 上这个名字已被占用，0.9.2 起改名）。
VS Code 把它当成另一个扩展，两份同时启用时每条诊断和补全都会出现两次：装新版后把旧的卸载，扩展启动时发现旧的也会提示。

<a id="从编辑器打开工作区"></a>

### 从编辑器打开工作区

*Tools ▸ Open DreamUI Workspace (VSCode)*（关卡工具栏 *Dream* 下拉菜单里的 *DreamUI Workspace* 是同一个）会：

1. 刷新符号导出，让补全数据在 VS Code 打开的那一刻就是新的；
2. 重写 `DUI/DreamUI.code-workspace`——列出项目的 `DUI/` 和每个已启用插件的 `DUI/` 根，固定 `*.dui` 的文件关联，
   并推荐这个扩展；项目还没有 `DUI/` 目录时，这个按钮可以是第一个创建它的东西；
3. 用 VS Code 打开，找不到就用系统默认编辑器，再不行用 Notepad。

通过这个工作区打开，而不是直接打开一个文件夹，扩展的工作区功能（跨文件索引、`use` 的解析）才会完整生效。
同一个菜单下的 *Rebuild DUI* 会在确认之后重新编译项目里每一个由文本支撑的 widget Blueprint，没加载的会先加载。

<a id="它能做什么"></a>

## 它能做什么

| 功能 | 说明 |
| --- | --- |
| 高亮 | 整个语法，包括绑定表达式、成员路径、`<->`、`+=` / `=` 路由、`use`、组件语法、`viewmodels` 块、`rows` 表、`timeline` 块；语义高亮按身份着色：节点 id 是它将成为的成员变量，样式是类，资源是只读常量 |
| 补全 | 内建标签、布局容器、组件类、逐类属性、枚举值、槽属性、资源类型、`@` 资源引用、`->` 事件名；组件实例上的 props、events 和插槽；`use … as` 别名和命名空间；`viewmodels` 块里的类型和 `=` 后面的四种来源；`Player.` 之后视图模型的成员（属性带类型、是否 FieldNotify、`<->` 能否写回，函数带参数），并且按用途筛选——路由给函数，`<->` 给可写的属性，循环源给数组——还能沿着对象成员（`Player.Stats.`）和视图模型列表上的循环（`for Item in Player.Items` 里的 `Item.`）继续补全（0.10.0 起） |
| 悬停 | 属性类型和枚举值、UPROPERTY 的说明和可粘贴的类默认值、别名解析到什么、`emit` 的签名、匿名节点会编译成的 id |
| 大纲与跳转 | widget 树的大纲（含 `if` / `else` 分支、循环、插槽）；`@Name`、样式、别名、`use` 目标、`emit`、prop、命名空间名的转到定义；Ctrl+T 跨所有 `.dui` 查 id、样式、资源 |
| 重命名与重构 | F2 重命名样式、资源时改掉所有用处；重命名节点 id 时写上 `(was: OldId)`，下次编译迁移图引用、绑定和动画；提取为资源、提取为样式、内联样式 |
| 格式化 | 按块深度缩进、`=` 和箭头两边一个空格、`}` 独占一行——和设计器写回打印的拼写一致，两者不会互相打架；结果会重新词法分析，改变任何一个 token 的格式化会被拒绝 |
| 诊断 | 编译器自己的代码和措辞、编译器自己的严重级别：词法码、单个文件能确定的结构码，以及符号驱动的检查。编译器始终是权威：它可能接受的东西，这里最多是警告 |
| 解释 | Problems 和悬停里的诊断码是链接，指向[诊断码](../diagnostics/index.md)里它的那一条（0.9.1 起）；离线时每条诊断也提供"解释 DUInnnn"操作，打开内置的代码表条目：什么意思、为什么触发、怎么修 |
| 快速修复 | 声明未知资源（DUI4007）、创建未知样式（DUI3004）、为只存在于另一个文件里的样式或资源加 `use` |
| 其他 | 每个 `#hex` 字面量上的颜色块和取色器；组件语法的代码片段；*DreamUI: New .dui File* 写出和编辑器 *Create Source File...* 一样的起步文件 |

语言**刻意没有**的东西（编译期条件、宏、变量、伪状态、`A | B` 写法的标志枚举、三元表达式、数组字面量……）扩展也不会
补全，理由见扩展的 README 和[.dui 概览](../language/overview.md)。

<a id="符号文件duidui-symbolsjson"></a>

## 符号文件：DUI/.dui-symbols.json

补全提供的恰好是编译器接受的东西，因为两者读的是**同一份清单**。编辑器模块把编译器知道的东西导出到项目的
`DUI/.dui-symbols.json`：

- **标签**来自构建器自己的表（包括 `Native.Button` 这类注册表标签）；
- **属性**来自写回扫描用的同一套反射规则；
- **枚举值**来自它们的 `UEnum`；
- 还有布局容器和它们的属性、事件、属性说明和类默认值；
- 能写进 `viewmodels` 的类（`UDreamViewModel` 的子类，以及任何实现了 `INotifyFieldValueChanged` 的非 widget 类）和它们的成员——
  `Player.` 之后的补全读的就是这一份（符号文件第 2 版；更旧的导出在这里什么都不补全，而不是去猜）。

它在两个时刻写出：**编辑器启动时**（类都已存在之后），以及**按需**——控制台命令：

```text
DreamUI.ExportSymbols
```

它只写进一个**已经存在的**项目 `DUI/` 目录：一个从没用过 `.dui` 的项目不应该在某次启动后突然拥有一个。没有
`DUI/` 时命令会在日志里说"nothing written"。

还没有符号文件时，打开一次 Unreal 编辑器（项目里要有 `DUI/` 目录）。所有语法驱动的功能没有它也能用，状态栏会显示
当前处于哪种模式——缺少符号文件是一条带说明的可见警告，而不是悄无声息的降级。符号文件更新后用
*DreamUI: Reload Symbols* 重新载入。

<a id="和编辑器之间的桥"></a>

## 和编辑器之间的桥

编辑器开着的时候，扩展能拿到单个文件的字符看不到的东西：

- **编译器的诊断，实时的。** 每次编译一个文本支撑的类，判定写进 `DUI/.dui-diagnostics.json`，出现在 Problems 面板里，
  来源是 `dui-compiler`——未知属性、错误的值、缺失的绑定函数这些语义层的问题。干净的编译写一个空数组，这就是清掉
  那个文件的波浪线的方式。
- **请求与回应。** 一个基于文件夹的协议，放在 `Saved/DreamGUI/Bridge/`：扩展投放请求，编辑器轮询、处理、写回应。
  `<-` 和 `->` 的补全会去问编辑器这个类真正声明了什么；`<->` 补全列出类的变量（FieldNotify 的排在前面，其余的每帧轮询）；
  输入 `/` 列出所有可嵌套的 widget 类；`/Game/...` 路径是链接，点一下在编辑器的内容浏览器里打开资产。
- **Reveal in Unreal Designer**（Ctrl+Alt+R）打开设计器并选中光标下的节点；**Compile This File**（Ctrl+Alt+B）立即编译，
  判定出现在 Problems 里。设计器就是这门语言的预览界面，桥只是把你带过去。
- **反方向**：在设计器的工具栏或层级右键菜单里点 *Reveal in VS Code*，编辑器写 `Bridge/reveal-to-editor.json`，VS Code
  跳到那个节点所在的行。

**警告：**

一个项目只能开一个编辑器。两个编辑器会互相抢对方的请求——这是记录在案、没有防御的情况。

<a id="命令与设置"></a>

## 命令与设置

| 命令 | 快捷键 | 作用 |
| --- | --- | --- |
| DreamUI: Reload Symbols (.dui-symbols.json) | | 重新读取符号文件 |
| DreamUI: New .dui File | | 新建一个起步 `.dui`（文件夹右键菜单里也有） |
| DreamUI: Reveal in Unreal Designer | Ctrl+Alt+R | 在设计器里打开并选中光标下的节点 |
| DreamUI: Compile This File | Ctrl+Alt+B | 让编辑器立即编译这个文件 |
| DreamUI: Clear Bridge Caches | | 清掉从编辑器问来的缓存 |

| 设置 | 默认值 | 含义 |
| --- | --- | --- |
| `dreamui.symbolsPath` | 空 | `.dui-symbols.json` 的显式路径。空表示在每个工作区文件夹的 `DUI/` 以及打开的文件的上级目录里找 |
