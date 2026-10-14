---
title: "有何不同"
description: "与 UMG 的结构性差异（首当其冲是文本溢出）、为什么名称沿用 UMG 以及对照表、分叉从哪个上游提交开始以及它重建了什么，还有致谢与许可。"
---

<a id="how-it-differs-from-umg"></a>

## 与 UMG 有何不同

在你决定用哪一方之前值得了解，因为差异是结构性的，而非表面上的。

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| 控件 | `SWidget`，保留模式的 Slate | 组件式树中的 `UObject` |
| 尺寸 | 内容决定尺寸：控件的尺寸**就是**它的期望尺寸 | 盒子优先：你编写一个矩形，内容在其中排布 |
| 文本 | 盒子随文本增长 | 文本在盒子中对齐，可能溢出盒子 |
| 摆放 | 相对插槽 | 锚点 + 轴心，与分辨率无关 |
| 复用 | 控件蓝图子类化 | 控件蓝图子类化，外加供内容使用的命名插槽 |
| 世界内 | `WidgetComponent`，一个渲染出的四边形 | 一等公民的渲染模式 |

文本上的差异是最让人意外的。在 UMG 中 `TextBlock` 无法溢出，因为它的盒子是从文本推导出来的；你能控制的是换行。
而这里矩形是你编写的，所以文本**可以**溢出它，并且你会得到 UMG 无需用到的控制项：`Margin`、`LineHeightPercentage`、`WrapTextAt` 以及 **Best Fit**（`bBestFit`：缩小字体直到放得下，下限为 `BestFitMinSize`），这是 UMG 和 Slate 都不提供的。

<a id="coming-from-umg-the-names-are-the-same"></a>

### 从 UMG 过来：名称是相同的

结构不同；词汇刻意保持一致。这里的控件沿用其 UMG 对应物的名称，并具有在那里的含义：`ScrollWidgetIntoView` 接受一个目标和一个内边距，滚动框有
`bFrontPadScrolling` 和 `WheelScrollMultiplier`，数值框有 `MinSliderValue` 和 `ClearMaxValue`，面板插槽有
`Nudge` 和 `bForceNewLine`，每个控件都有 `SetIsEnabled`、`SetRenderShear` 和一个 `FlowDirectionPreference`。详情面板能拧的每一个旋钮，蓝图在运行时都能拧，通过一个会推送更改的 setter，
而不是写入一个再也没人读取的字段。

名称无法保留的地方，会被写下来，而不是留待你去发现。`Resources/UMGParity` 为每个 UMG 类保存一张表——共 58 张，约 960 个面向蓝图的成员——每一行只说明三种情况之一：

| 状态 | 含义 |
| --- | --- |
| *adopt* | 名称相同，含义相同 |
| *map* | 在这里以另一个名称存在，或在另一个类型上，并说明是哪个 |
| *reject* | 有意不提供，并给出原因：没有可绘制的即时模式绘制上下文、矩形块没有逐实例材质，等等 |

自动化套件会将这些表与 UMG 自身的反射进行双向核对：引擎在升级中新增的成员会表现为一行不存在的记录，而一行指向本插件已重命名内容的记录会表现为一个无法解析的名称。逐类对照见 [UMG 对照](../controls/umg-parity.md)。

有两处差异值得提前了解：

- **新增的旋钮默认采用控件原有的行为**，而不是 UMG 的默认值，这样已有内容不会移动：`ScrollWhenFocusChanges` 在这里是 `AnimatedScroll`，在那里是 `NoScroll`，表中记录了每一个此类情况。
- **外观位于控件的样式结构体中，行为位于控件上**，所以少数 UMG 属性低了一层：`EntrySpacing` 是 `Style.RowSpacing`。

`Docs/Reference` 是属性与函数参考，每个类一页，每页末尾附有该类与 UMG 的对照。它从反射打印而来，而非手写，所以不会落后于头文件：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

本站的 [API 参考](../Reference/index.md)就是这些页面。

<a id="how-it-differs-from-upstream"></a>

## 与上游有何不同

DreamGUI 是 Lex Liu 的 [LGUI / LexUI](https://github.com/liufei2008/LGUI) 的分叉，而**不是**它的直接替代品。

**基线。** 从上游 `LexUI/5.7` 的 `765efeaf1`（2026-07-13）分叉，之后又将直到
`97d281376`（2026-07-21）的上游提交 rebase 进来：`97d281376` 是本代码起步时的上游，其内容是本仓库的 `5b48c42a`。要与它对比，而不是与 `765efeaf1` 或某个合并基对比，否则那些 rebase 进来的提交会被算作本分叉的改动。之后的上游修复是手工移植的。

上游仍在积极开发，但这两条线已经无法廉价地合并了：

| | 上游 | 这里 |
| --- | --- | --- |
| 引擎 | UE 5.7 —— LexUI 线上没有 5.8 分支 | **UE 5.8** |
| 布局 | FlexBox + Grid 一族，仍在开发中 | **整族删除**；只有 UMG 形状的面板 |
| 编辑器 | —— | 设计器大部分重建 |

布局上的分裂是其中最尖锐的。上游 2026-08-08 针对
`ULexLayoutContainerFlexBox` 中无限循环的修复在这里毫无意义，因为该类已不复存在。

<a id="what-was-rebuilt"></a>

### 重建了什么

- **布局**，沿用了 Blink 和 Yoga 的思路。测量是 `const` 的，并与应用分离；面板排布成一个不可变片段，一次写入即提交；期望尺寸在一次布局过程中被记忆；失效带有原因，所以移动一个控件不再重新测量整条祖先链。旧的 Lex
  布局一族（`ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、
  `ULexLayoutSelfGrid`，以及 `ELexUILayoutMode` 开关）被删除了。
- **设计器**，对照 UMG 的控件设计器进行了审查。视口拾取按控件的*矩形*而不是按渲染出的三角形——仅布局的面板没有网格，所以射线永远打不中它们，这导致面板既点不中也拖不进去。此后新增了：悬停反馈、逐轴缩放手柄、锚点徽章、框选、在画布上拖动改父级、从内容浏览器拖放到设计面、调色板收藏、按类型搜索，以及创建、粘贴和拖放的撤销覆盖。
- **文本**，即上面列出的四个控制项。
- **透视**，逐控件并由其子树继承。它需要一块带透视投影的屏幕空间画布，否则不起作用。
- **渲染变换**，扩展到三维，所以控件可以在布局中做动画，而布局不会与之较劲。
- **输入**，按玩家分开。每个本地玩家都有自己的指针、焦点和文本目标，所以分屏的第二个玩家可以自行悬停、聚焦和输入；鼠标、手指和脚本指针各有自己的 id 区间，不再共用一个 id。一个可选的 Slate 输入源在视口之前接收输入，所以 UI 在引擎的 UI-only 输入模式下仍能工作。
- **渲染。** 画布只遍历那些要求改变的控件，几何发生变化的分段就地打补丁而不是重建，并通过渲染线程代理回答其材质的参数，而不是每次绘制调用一个材质实例。渲染目标画布由它自己的渲染命令绘制，所以无论是否有东西在渲染它的世界，它都会更新。各屏幕效果共用一种在渲染图纹理上读取、裁剪和写回屏幕的方式。

另外两处结构性变化：上游的 prefab 资产模型没有了，连同 `SavePrefab`、`Apply`、
`ClearLoadedPrefab` 和 *Save on Apply*——一棵 UI 树现在是一个类（见 [控件蓝图](../concepts/widget-blueprints.md)）；
自动化套件是插件的一部分，而上游没有（见 [测试](../tools/tests.md)）。该插件
不附带重定向：以旧类名保存的资产需用 1.0.0 重存一次，为此借用了 2.1.0 的重定向块；见 [迁移](../guides/migration.md)。

<a id="credits-and-license"></a>

## 致谢与许可

MIT——见仓库中的 `LICENSE`：

```text
Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu
```

本软件的很大一部分仍是 Lex Liu 的工作成果，依据
[原始项目](https://github.com/liufei2008/LGUI)的 MIT 条款使用。**任何本代码的副本或其实质部分都必须附带 MIT 声明，包括你的。**
捆绑的 msdfgen（Viktor Chlumsky，MIT）同样如此，其
`ThirdParty/msdfgen-single-file/LICENSE.txt` `Config/FilterPlugin.ini` 会被打包进 BuildPlugin 的输出。

源码与问题反馈：https://github.com/TypeDreamMoon/DreamGUI
