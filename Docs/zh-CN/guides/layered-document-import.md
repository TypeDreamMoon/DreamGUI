---
title: 导入分层文档
description: 把 .psd 或 .psb 变成 DreamUI 控件——一个 .dui 加它用到的贴图，编译成控件蓝图；每种图层变成什么、命名约定、字体，以及文档改了以后再导入。
---

分层文档（一个 `.psd`，大文件则用 `.psb`）导入后是一个 DreamUI 控件：一个把各图层排布成控件的 `.dui`、这些控件绘制的贴图，以及一个 *Source File* 指向该 `.dui` 的控件蓝图。蒙版、剪贴、混合模式、组、实时形状、填充和文字都会变成 DreamGUI 中执行相同功能的功能，因此界面仍然可编辑：`.dui` 就是普通的一个，设计器照常编辑它。

导入器是编辑器模块 `DreamGUIPsd`。文件由它自己读，不用第三方库，也不需要制作文件的那个软件。

<a id="importing"></a>

## 怎么导入

- **内容浏览器**：右键文件夹，**DreamGUI > Import Layered Document as DreamUI Widget...**，选一个或几个文件。通知里说导入了什么、没导入什么；完整报告在输出日志里。
- **内容浏览器的 Import**（或拖进浏览器）照引擎一贯的做法把 `.psd` 导成贴图。在 **项目设置 > Plugins > DreamGUI Layered Document Import** 里打开 *Handle Content Browser Import*，它就改为导成 DreamUI 控件。
- **Commandlet**，给脚本和持续集成用：

  ```
  UnrealEditor-Cmd.exe <project> -run=DreamPsdImport -Psd=<file> [-Folder=/Game/UI] [-Name=<name>] [-Dui=<file>]
      [-NoBlueprint] [-NoSave] [-DryRun] [-Dump] [-Report=<file>]
  ```

  `-Dump` 打印读取器眼中的文档（每个图层、它的种类、数据块和蒙版），不导入。`-DryRun` 只做映射并报告会写什么，什么都不写。导入成功时退出码为 0。`-run=DreamPsdImport -WriteSample=<file>` 写出一份小的示例文档，用到导入器映射的各项功能（整体淡出的组、带描边的圆角面板、文字、剪贴、九宫格 `@Button`、带蒙版的图片、滤色混合的光晕、调整图层和投影），可以拿来试。

把 `Hud.psd` 导入 `/Game/UI` 会生成：

| 什么 | 在哪 |
|---|---|
| 控件蓝图 | `/Game/UI/WBP_Hud` |
| 它的贴图 | `/Game/UI/Hud/`——像素用 `T_Hud_<layer>_<id>`，蒙版用 `TM_…`，渐变用 `TG_…`，图案用 `TP_…` |
| 该 `.dui` | `<Project>/DUI/UI/WBP_Hud.dui`，即蓝图转换器为同一个蓝图所使用的路径 |
| 清单 | `<Project>/DUI/UI/WBP_Hud.dui.psdimport.json`：这次导入写了什么，供下次导入用 |

<a id="what-each-layer-becomes"></a>

## 每种图层变成什么

根节点是画布大小、居中的 `Widget`；图层按文档里的叠放顺序做它的子节点，最下面的在前——正是 `.dui` 绘制子节点的顺序。每个控件放在它的图层覆盖的像素上，锚在父节点左上角。

| 图层 | 变成 |
|---|---|
| 像素 | `Image`，画图层像素的贴图，裁掉透明边 |
| 智能对象 | 同上，用它渲染出的像素 |
| 组 | 撑满父节点的 `Widget`。混合模式不是穿透、或有不透明度的组挂 `+ DreamLayerGroup`（隔离，不透明度给 `GroupOpacity`，混合模式给 `GroupBlendMode`），见[混合模式与图层组](../concepts/blend-modes-and-groups.md) |
| 画板 | 画板矩形大小的 `Widget`，内容裁到画板里 |
| 文字 | `Text`：文字内容，第一个样式段的字体（经字体映射表）、字号、颜色、字距、行距、下划线、删除线、仿粗体/仿斜体，第一个段落的对齐。段落文字保留文本框；点文字放在它画出的像素上 |
| 实时矩形、圆角矩形、圆 | `RectBlock`：形状的框、圆角、填充做 body、描边做边框（居中或外侧描边时框相应放大） |
| 不是圆的实时椭圆 | 带椭圆裁剪遮罩的 `RectBlock` |
| 其它形状 | 画它渲染出的像素的 `Image`；文档没存像素时，填充它的路径 |
| 纯色、渐变填充 | 铺满画布的 `RectBlock`；渐变画进 body 贴图 |
| 图案填充 | 平铺图案的 `Image` |

然后每个图层都有：

| 属性 | 变成 |
|---|---|
| 不透明度 | `RenderOpacity`（组的是 `GroupOpacity`） |
| 填充不透明度 | 本体的 alpha：图片的 tint、文字颜色、RectBlock 的 body 和边框 |
| 混合模式 | `BlendMode`，全部 27 种（线性减淡即 `Additive`），见[混合模式与图层组](../concepts/blend-modes-and-groups.md) |
| 剪贴 | `bClipToBelow`，见[遮罩与剪贴到下方图层](../concepts/clip-masks.md) |
| 隐藏 | `Visibility = Hidden`（关掉 *Import Hidden Layers* 则不导入隐藏图层） |
| 像素蒙版 | 读单通道蒙版贴图的裁剪遮罩，羽化和浓度烘进贴图；蒙版外为黑时，用 `ClippingMargin` 把裁剪收窄到蒙版矩形 |
| 矢量蒙版 | 是实时矩形、圆角矩形或椭圆时：按它的框裁剪形状（`ClippingCornerRadius` 或 `ClipMask.Shape = Ellipse`），羽化给遮罩。否则把路径填进蒙版贴图 |
| 组的蒙版 | 穿透的组：容器的裁剪遮罩（每个子节点都被遮——穿透本来就是这个意思）；隔离的组：它的 `GroupMask` |

打开 *Bake Layer Masks* 则把像素图层的蒙版乘进它的贴图：贴图更少，但之后没得改。

<a id="naming-conventions"></a>

## 命名约定

导入器读图层名里的这些词（关掉 *Naming Conventions* 就按原名读）：

| 名字里写 | 作用 |
|---|---|
| `!skip` | 不导入该图层（组则连同组内全部） |
| `@Button`、`@Toggle`、`@Slider` | 给图层的控件加对应行为（`+ UIButton`、`+ UIToggle`、`+ UISlider`） |
| `#9(l,t,r,b)` | 像素图层按九宫格画，边距以像素计；`#9(n)` 四边相同，`#9(h,v)` 水平、垂直 |

这些词会从生成控件 id 的名字里去掉：`Play @Button #9(8,8,8,8)` 生成控件 `Play_psd12`。

<a id="fonts"></a>

## 字体

文字的字体由 PostScript 名标识，它不是 DreamGUI 字体。导入设置里的 **Font Map** 把字体名（`Roboto-Bold`）或字族（`Roboto`）映射到 DreamGUI 字体资产；没有条目的字体用 **Default Font**（为空则用 DreamGUI 默认字体），导入会列出每个这样的字体。*Rasterize Text* 则把所有文字图层导成它像素的图片。

文字按它的像素定位、由 DreamGUI 的文字引擎绘制，所以字体换了、或两边对行高的量法不同时，会偏一两个像素：导入后检查一下文字。

<a id="importing-again"></a>

## 再次导入

右键蓝图选 **Reimport Layered Document**，或把同一个文件再导入到同一个文件夹。再次导入不会写新的 `.dui`，而是编辑它上次写的那个，用的是设计器写回所用的同一个文本补丁器，所以文件的格式、注释和你加的一切都保留。

id 以 `_psd<n>` 结尾的节点由图层 *n* 生成（图层 id 在文档里改名、换叠放顺序都不变）。再次导入时：

- 图层没了，它的节点也删掉。你嵌在那个节点里的东西先移到它的父节点，并列出。
- 图层改名后，它的节点也会用 `(was: OldId)` 改名，因此图表引用、绑定和动画轨道都会跟着它走。
- 图层移动或换顺序，节点跟着移；新图层的节点插到文档里它所在的位置。你加的节点位置不变。
- 生成节点上，导入器写的行（位置、不透明度、可见性、剪贴、混合模式、裁剪遮罩、笔刷、RectBlock 的 body/边框/圆角、文字属性、图层组属性）改写成文档的新值；上次写了、这次不写的行删掉。其余每一行都是你的，保留。你改过的导入器的行会被覆盖并列出；你改成绑定（`<-`）的行保留，并列出。
- 你删掉的生成节点会回来，并列出。想彻底不导入，在图层名里写 `!skip`。
- 根节点的位置只在第一次导入时写，屏幕可以按需要给根节点设锚点。
- 上次生成、这次不用的贴图会列出来，留给你删。

<a id="not-imported-yet"></a>

## 暂未导入

已从文档读出并在导入报告里列出，但还没变成 DreamGUI 功能：

- **图层样式**（投影、内阴影、内外发光、斜面和浮雕、光泽、颜色/渐变/图案叠加、描边）。图层照常导入，不带样式。
- **调整图层**（色阶、曲线、色相/饱和度、亮度/对比度、色彩平衡、黑白、照片滤镜、通道混合器、渐变映射、曝光度、自然饱和度、反相、色调分离、阈值）。不生成节点。
- **文字变形**。变形的文字导成它像素的图片。

<a id="limits"></a>

## 限制

- **颜色**：RGB 和灰度原样导入；索引色经颜色表；CMYK 和 Lab 按公式转成 sRGB，不读颜色配置文件，颜色接近但不完全一致—— 想要准确，先在制作软件里转成 RGB。16 位和 32 位文档降为 8 位（32 位是线性的，会编码成 sRGB）。
- **渲染目标上的正常混合**：DreamGUI 在屏幕上按编码值混合正常图层，和文档自带的合成图一样；画进渲染目标时则在线性空间混合。半透明图层画进渲染目标会和文档略有不同。
- **文字样式段**：有多个样式段的文字整体用第一段的样式。
- **形状**：只有未变换的矩形、圆角矩形、椭圆是实时形状块；旋转或斜切过的、直线、自定义形状、组合路径按像素画。
- **旧文档**没有图层 id 时，节点按图层位置命名，增删图层后再导入可能对错节点。

<a id="settings"></a>

## 设置

**项目设置 > Plugins > DreamGUI Layered Document Import**（`UDreamPsdImportSettings`，存在 `DefaultEditor.ini`）：

| 设置 | 默认 | |
|---|---|---|
| Font Map | 空 | 字体名或字族 → DreamGUI 字体 |
| Default Font | DreamGUI 默认 | 未映射字体用的字体 |
| Rasterize Text | 关 | 文字图层导成图片 |
| Import Hidden Layers | 开 | 隐藏图层导入为隐藏 |
| Trim Transparent Pixels | 开 | 贴图裁掉透明边 |
| Naming Conventions | 开 | 读 `!skip`、`@Button`、`#9(...)` |
| Bake Layer Masks | 关 | 蒙版乘进贴图 |
| Mask Feather Scale | 1 | 蒙版模糊的 sigma = 羽化 × 此值 |
| Max Gradient Texture Size | 512 | 渐变贴图最长边 |
| Handle Content Browser Import | 关 | 内容浏览器的 Import 把 `.psd` 导成控件 |
| Blueprint Prefix | `WBP_` | 蓝图名 = 前缀 + 文档名 |
