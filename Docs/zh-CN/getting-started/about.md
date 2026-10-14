---
title: 与 UMG 和上游的区别
description: DreamGUI 与 UMG 的结构性区别（尤其是文本溢出）、为什么名字和 UMG 一样以及对照表、它从哪个上游提交分出来又重做了什么，以及致谢与许可证。
---

<a id="和-umg-的区别"></a>

## 和 UMG 的区别

在两者之间做选择之前值得先知道这些，因为区别是结构上的，不是外观上的。

| | UMG / Slate | DreamGUI |
| --- | --- | --- |
| 控件 | `SWidget`，保留模式的 Slate | 组件式树里的一个 `UObject` |
| 尺寸 | 内容决定尺寸：控件的尺寸**就是**它的期望尺寸 | 盒子优先：你给出一个矩形，内容在里面排布 |
| 文本 | 盒子随文本长大 | 文本在盒子里对齐，可能溢出盒子 |
| 摆放 | 相对插槽 | 锚点 + 轴心，与分辨率无关 |
| 复用 | 控件蓝图子类化 | 控件蓝图子类化，外加给内容用的命名插槽 |
| 在世界里 | `WidgetComponent`，渲染成一个四边形 | 一等公民的渲染模式 |

最让人意外的是文本那一行。在 UMG 里 `TextBlock` 不可能溢出，因为它的盒子就是从文本推出来的，你能控制的只是换行。
这里矩形是你写的，所以文本**可以**溢出它，于是也就有了一些 UMG 用不着的控制项：`Margin`、`LineHeightPercentage`、
`WrapTextAt`，以及 **Best Fit**（`bBestFit`，把字号缩到放得下为止，下限 `BestFitMinSize`）—— UMG 和 Slate 都没有这一项。

<a id="从-umg-过来名字是一样的"></a>

### 从 UMG 过来：名字是一样的

结构不同，词汇却刻意保持一致。这里的控件用它在 UMG 里对应物的名字，含义也和那里一样：`ScrollWidgetIntoView`
接受一个目标和一个边距，滚动框有 `bFrontPadScrolling` 和 `WheelScrollMultiplier`，数值框有 `MinSliderValue` 和
`ClearMaxValue`，面板插槽有 `Nudge` 和 `bForceNewLine`，每个控件都有 `SetIsEnabled`、`SetRenderShear` 和
`FlowDirectionPreference`。详情面板能拧的每一个旋钮，蓝图在运行时都能拧，而且是通过一个会把改动推出去的 setter，
不是写一个再也没人读的字段。

名字保留不下来的地方，是写下来的，而不是留给你自己去发现。`Resources/UMGParity` 里每个 UMG 类一张表 —— 共 58 张，
大约 960 个面向蓝图的成员 —— 每一行只说三种话之一：

| 状态 | 含义 |
| --- | --- |
| *adopt* | 同名、同义 |
| *map* | 在这里换了名字，或在另一个类型上，并写明是哪个 |
| *reject* | 有意不提供，并写明原因：没有可供绘制的即时模式绘制上下文、矩形块没有逐实例材质，等等 |

自动化测试拿这些表和 UMG 自己的反射双向核对：引擎升级后多出来的成员会变成"表里没有这一行"，表里一行指向的名字
被本插件改掉了，会变成"这个名字解析不到"。逐类的对照见[UMG 对照](../controls/umg-parity.md)。

有两处区别值得提前知道：

- **新加的旋钮默认是控件原来的行为**，而不是 UMG 的默认值，这样已有的内容不会动。比如 `ScrollWhenFocusChanges`
  这里是 `AnimatedScroll`，UMG 是 `NoScroll`；表里记下了每一个这样的情况。
- **外观在控件的样式结构体里，行为在控件本身上**，所以有几个 UMG 属性在低一层：`EntrySpacing` 在这里是 `Style.RowSpacing`。

`Docs/Reference` 是逐类的属性与函数参考，每页末尾附那个类与 UMG 的对照。它从反射打印出来，不是手写的，所以不会落后于头文件：

```powershell
UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs
```

本站的 [API 参考](../Reference/index.md)就是这些页面。

<a id="和上游的区别"></a>

## 和上游的区别

DreamGUI 是 Lex Liu 的 [LGUI / LexUI](https://github.com/liufei2008/LGUI) 的分叉，但**不是**它的直接替代品。

**基线。** 从上游 `LexUI/5.7` 的 `765efeaf1`（2026-07-13）分出，之后又把上游直到 `97d281376`（2026-07-21）的提交 rebase 了进来：
`97d281376` 才是这份代码起步的上游，它的内容对应本仓库的 `5b48c42a`。要对比，就和它比，不要和 `765efeaf1` 或某个
merge base 比，否则那些 rebase 进来的提交会被算成本分叉的改动。之后的上游修复是手工移植的。

上游还在积极开发，但两条线已经没法便宜地合并了：

| | 上游 | 这里 |
| --- | --- | --- |
| 引擎 | UE 5.7 —— LexUI 线上没有 5.8 分支 | **UE 5.8** |
| 布局 | FlexBox + Grid 一族，仍在开发 | **整族删除**，只有 UMG 形状的面板 |
| 编辑器 | —— | 设计器大部分重做 |

布局上的分裂最彻底。上游 2026-08-08 修了 `ULexLayoutContainerFlexBox` 里的一个死循环，这个修复在这里毫无意义 ——
这个类已经不存在了。

<a id="重做了什么"></a>

### 重做了什么

- **布局**，沿着 Blink 和 Yoga 的思路。测量是 `const` 的，和应用分开；面板排布成一个不可变的片段，一次写入提交；
  一次布局过程中期望尺寸会被记忆；失效带着原因，所以移动一个控件不再重新测量整条祖先链。旧的 Lex 布局一族
  （`ULexLayoutContainerFlexBox`、`ULexLayoutContainerGrid`、`ULexLayoutSelfFlexBox`、`ULexLayoutSelfGrid`，以及
  `ELexUILayoutMode` 开关）被删掉了。
- **设计器**，对照 UMG 的控件设计器逐项审过。视口拾取按控件的**矩形**，而不是按渲染出的三角形 —— 只做布局的面板没有网格，
  射线永远打不中它们，于是面板既点不中也拖不进去。此后加了：悬停反馈、逐轴的缩放手柄、锚点徽章、框选、在画布上拖动改父级、
  从内容浏览器拖放到设计面上、调色板收藏、按类型搜索，以及创建、粘贴、拖放的撤销覆盖。
- **文本**，加了上面列出的四个控制项。
- **透视**，逐控件，并由子树继承。需要一块使用透视投影的屏幕空间画布，否则不起作用。
- **渲染变换**，扩展到三维，所以一个控件可以在布局里做动画，布局不会和它较劲。
- **输入**，按玩家分开。每个本地玩家有自己的指针、焦点和文本目标，分屏的第二个玩家可以自己悬停、聚焦、打字；鼠标、手指和
  脚本指针各有自己的 id 区间，不再共用一个 id。可选的 Slate 输入源在视口之前听输入，让 UI 在引擎的 UI-only 输入模式下也能工作。
- **渲染。** 画布只走那些要求改变的控件，几何变了的分段就地打补丁而不是重建，并且通过渲染线程上的材质代理回答材质参数，
  不再每个 draw call 一个材质实例。渲染目标画布由自己的 render command 绘制，所以不管有没有东西在渲染它的世界，它都会更新。
  各个屏幕效果共用一套在渲染图纹理上读取、裁剪、写回屏幕的方式。

另外两件结构上的事：上游的 prefab 资产模型没有了，连同 `SavePrefab`、`Apply`、`ClearLoadedPrefab` 和 *Save on Apply* ——
一棵 UI 树现在是一个类（见[控件蓝图](../concepts/widget-blueprints.md)）；自动化测试套件是插件的一部分，上游没有（见[测试](../tools/tests.md)）。
插件不带 redirect：按旧类名保存的资产，要借来 2.1.0 的 redirect 段、用 1.0.0 重存一次，见[迁移](../guides/migration.md)。

<a id="致谢与许可证"></a>

## 致谢与许可证

MIT，见仓库里的 `LICENSE`：

```text
Copyright (c) 2026-present TypeDreamMoon
Copyright (c) 2019-present Lex Liu
```

这份软件相当大的部分仍然是 Lex Liu 的工作，依照[原项目](https://github.com/liufei2008/LGUI)的 MIT 条款使用。
**MIT 声明必须随这份代码的任何拷贝或其实质部分一起走 —— 包括你的。** 插件自带的 msdfgen（Viktor Chlumsky，MIT）同理，
它的 `ThirdParty/msdfgen-single-file/LICENSE.txt` 由 `Config/FilterPlugin.ini` 一并打进 BuildPlugin 的输出。

源码与问题反馈：https://github.com/TypeDreamMoon/DreamGUI
