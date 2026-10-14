---
title: "概览"
description: "DreamGUI 是 Unreal Engine 5.8 的 3D UI 插件：控件是挂在树上、带矩形、轴心和锚点的 UObject，由画布合批绘制，可以铺在屏幕上，也可以立在关卡里。"
---

DreamGUI 是 Unreal Engine 5.8 的一套 UI 系统。它的一个控件是一个 `UObject`：有一个矩形、一个轴心（pivot）
和一组锚点，挂在一棵树上；整棵树由一块画布（`UDreamCanvas`）合批，尽量少地变成 draw call。
这是 Unity uGUI 的形状，而不是 Slate 的。

这个形状换来三件 UMG 做不直接的事：

- **UI 在世界里。** 画布可以画在屏幕空间，也可以贴在关卡里任意角度的面上，受光或不受光，
  两种情况下命中测试都是对的。
- **一棵树就是一个类。** UI 树写在控件蓝图（`UDreamWidgetBlueprint`）里，靠子类化、以及把一个控件类
  嵌进另一棵树来复用 —— 和 UMG 的控件蓝图是同一种做法。树也可以写成文本：一个 `.dui` 文件就是
  一个控件蓝图的源码，设计器的改动写回这个文件。
- **逐控件的透视。** 一个控件可以建立一个透视，它的子树按这个透视缩短，就像 CSS 的
  `perspective`。

代价是 Slate 的生态用不上：UMG 的控件、样式和绑定在这里都不适用。DreamGUI 有自己的一整套控件，
名字刻意沿用 UMG 的叫法，叫不了同名的地方都写明了原因（见
[它和 UMG、和上游有什么不同](getting-started/about.md)）。

| | |
| --- | --- |
| 插件版本 | `1.0.0`，第一个公开版本（之前的开发版编号是 1.x、2.0、2.1，见[更新日志](changelog/index.md)） |
| 引擎 | Unreal Engine `5.8`，源码引擎与 Epic 启动器安装版都可以 |
| 平台 | Win64 是唯一构建并运行过的平台（见[平台](guides/platforms.md)） |
| 运行时模块 | `DreamGUIRenderer`、`DreamGUI`、`DreamGUIInput`、`DreamGUIControls`、`DreamGUIExtensions`、`DreamGUISamples`、`DreamTween` |
| 编辑器侧模块 | `DreamGUIEditor`、`DreamGUIK2Nodes`（`UncookedOnly`）、`DreamGUITests`（`Editor`） |
| 依赖插件 | `EnhancedInput`（插件自己启用） |
| 源文件 | `.dui`，放在项目或插件的 `DUI/` 目录下 |
| 项目设置 | *Project Settings ▸ Plugins ▸ Dream GUI* 与 *Project Settings ▸ Plugins ▸ DreamUI* |
| 许可证 | MIT |
| 来历 | Lex Liu 的 [LGUI / LexUI](https://github.com/liufei2008/LGUI) 的分叉，不是它的直接替代品 |

<a id="at-a-glance"></a>

## 一眼看上去

```dui
class /Game/UI/WBP_Hello

Widget Root {
    AnchorData.AnchorMin = (0, 0)
    AnchorData.AnchorMax = (1, 1)
    AnchorData.SizeDelta = (0, 0)
    + Overlay {}

    Text Greeting {
        AnchorData.SizeDelta = (600, 60)
        Text     = "Hello, DreamGUI"
        FontSize = 32
        HAlign   = Center
        @slot HorizontalAlignment = Center
        @slot VerticalAlignment   = Center
    }
}
```

`Root` 锚定在四个角上、不留边，所以它铺满它被加进去的东西；`+ Overlay` 让它把子节点
叠在一起排布；`Greeting` 是一段 600 × 60 的文本，`@slot` 两行是它对上层 Overlay 提的要求：
水平、垂直都居中。把一个控件蓝图的 *Source File* 指向这个文件、编译，它就是一个能加到视口、也能拖进关卡
的类 —— 完整的一步一步在[第一个界面](getting-started/first-screen.md)。

<a id="where-to-start"></a>

## 从哪里开始

  - [快速上手](getting-started/installation.md) — 安装、第一个界面、项目里的目录结构、日常的创作循环。
  - [核心概念](concepts/widgets.md) — 控件、画布、世界空间、控件蓝图、布局、文本、渲染、输入、动画。
  - [.dui 语言](language/overview.md) — 节点与值、插槽与组件、样式、use、组件、视图模型、绑定、if 与 for、rows、时间轴、写回。
  - [控件库](controls/index.md) — 按钮、开关、滑条、列表、对话框、标签页……以及样式表和与 UMG 的逐项对照。
  - [工具](tools/designer.md) — 设计器、VS Code 扩展、调试命令、测试套件、基准测试、脚本接口。
  - [迁移与升级](guides/migration.md) — 从 LGUI、LexUI 或早期构建迁过来，以及开发版之间的升级。
  - [打包与平台](guides/fonts-and-packaging.md) — 字体、彩色 emoji、ICU 数据怎么随游戏发出去；哪些平台真的跑过。
  - [诊断码](diagnostics/index.md) — 每条 .dui 诊断都是一个 DUInnnn 代码，按编译阶段分成七族。
  - [API 参考](Reference/index.md) — 从反射生成的逐类属性与函数参考，每页末尾附与 UMG 的对照。
  - [更新日志](changelog/index.md) — 每个版本改了什么。
