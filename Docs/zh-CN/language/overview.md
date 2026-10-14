---
title: "概览"
description: "什么是 .dui 文件、它放在哪里、它由哪些语句组成，以及它如何成为控件蓝图的层级。"
---

一个 `.dui` 文件是 DreamGUI 控件蓝图的源码：以文本形式表示的控件树、它们的属性、绑定和动画。蓝图将该文件指定为其 *Source File*，编译蓝图时会读取该文件，设计器会将编辑内容写回该文件。本页是手写该文件的参考。

<a id="where-a-file-lives"></a>

## 文件放在哪里

位于 `DUI/` 目录下：项目的（`<Project>/DUI/`）或已启用插件的（`<Plugin>/DUI/`）。不在 `Content/` 下——`.dui` 是源码，不是资产。

指向另一个文件的路径——无论是在蓝图的 Source File 中，还是在 `use` 之后——有三种写法：

| 写法 | 含义 |
|---|---|
| `Panels/Settings.dui` | 相对于某个 `DUI/` 根目录：先找项目的，然后依次找每个插件的，取第一个存在该文件的。 |
| `Plugin.MyPlugin:Panels/Settings.dui` | 该插件的 `DUI/` 目录。 |
| `D:/Work/Proj/DUI/Panels/Settings.dui` | 绝对路径，按原样使用。 |


<a id="file-structure"></a>

## 文件结构

一个文件是一系列语句。语句在其行尾或 `;` 处结束。注释以 `// …` 开始直到行尾，以及 `/* … */`。

在顶层，一个文件包含：

| 语句 | 用途 |
|---|---|
| `class /Game/UI/WBP_Settings` | 该文件作为其源码的蓝图。至多一个。 |
| `use …` | 使另一个文件或类可被本文件使用。参见 [`use`](use.md#use)。 |
| `resources { … }` | 具名常量。参见 [资源](styles-and-resources.md#resources)。 |
| `props { … }` | 该文件的类声明的属性。参见 [编写组件](components.md#writing-a-component)。 |
| `events { … }` | 该文件的类引发的事件。 |
| `viewmodels { … }` | 该文件的类持有的视图模型。参见 [视图模型](viewmodels.md#view-models)。 |
| `style Name { … }` | 一组具名的行。参见 [样式](styles-and-resources.md#styles)。 |
| `timeline Name { … }` | 一个动画。参见 [时间线](timelines.md#timelines)。 |
| 一个节点 | 树的根。 |

这些语句的顺序是自由的：样式可以声明在穿戴它的节点下方。编译为类的文件恰好有一个根节点（DUI2006）。没有根的文件是一个 *库*：它可以被 `use`，且不能被编译。

`class` 行承担两项职责。它说明该文件用于哪个蓝图（编译到另一个蓝图会警告，DUI6003），并且它的路径是文件中每个本地化字符串的命名空间，这样文件重命名时键能保持稳定。没有它时，文件自身的名称就是命名空间。
