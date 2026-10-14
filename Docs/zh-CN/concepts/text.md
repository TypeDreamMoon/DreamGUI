---
title: 文本
description: 文本系统：距离场（MTSDF）默认字体、小字号的提示覆盖字形、回退字体与彩色 emoji，盒子优先的溢出规则与 Margin、LineHeightPercentage、WrapTextAt、Best Fit，富文本，渐变文字，按文化断行，增量布局，以及 DreamGUI.Text.* 控制台变量。
---

文本由 `UDreamText` 视觉绘制（`.dui` 标签 `Text`）。它的度量对照 Chrome 和 Slate 校过一遍、不一致的地方重写过：
字素簇和双向文本层级、连字、CSS 的行盒、按 Unicode 范围 / 文化 / 呈现方式选择回退字体、真正的粗体和斜体字面、彩色 emoji、
两端对齐、制表位和中间省略。

<a id="盒子优先"></a>

## 盒子优先

UMG 的 `TextBlock` 不会溢出，因为它的盒子是从文本推出来的；你控制的是换行。这里矩形是写出来的，文本在盒子里对齐，
放不下时由 `OverflowType` 决定怎么办：

| `OverflowType` | 放不下时 |
| --- | --- |
| `HorizontalOverflow` | 字符横向跑出矩形 |
| `VerticalOverflow`（默认） | 换行，纵向跑出矩形 |
| `Truncate` | 去掉右边放不下的字符 |
| `Ellipsis` | 用 `...` 替换放不下的部分 |
| `MiddleEllipsis` | 每行放不下时把中间换成 `...`，保留开头和结尾（Slate 的 MiddleEllipsis）；换行时改为最后一行的末尾省略 |

`bAutoWrapText` 让非 `VerticalOverflow` 的策略也换行（UMG 的 AutoWrapText）：配 `Ellipsis` 得到一段最后一行被省略的段落，
配 `Truncate` 得到一段在盒子底部被截断的段落。对齐是 `HAlign`（`Left`、`Center`、`Right`、`Justify`）和 `VAlign`（`Top`、`Middle`、`Bottom`）。

盒子是写出来的，所以有了 UMG 用不着的控制：

| 属性 | 作用 |
| --- | --- |
| `Margin` | 矩形和文本之间的内边距，像 UMG 文本的 Margin。换行、对齐、溢出测试都针对矩形**减去**它，所以它会收窄换行宽度，而不只是挪一下结果；它也会加进文本报告的首选尺寸 |
| `LineHeightPercentage` | 行距的倍数，`1` 是字体自己的行高。只改行与行之间的距离，字形大小不变。`FontSpace.Y` 是加在上面的固定距离：一个随字号缩放，一个不随 |
| `WrapTextAt` | 按这个宽度而不是内容盒的宽度换行，`0` 及以下用内容盒。盒子同时也是文本**对齐**的地方，所以更窄的换行宽度能得到一栏窄文字、仍在整个控件上居中。它不截断：`Truncate` 和 `Ellipsis` 仍按盒子量 |
| `MinDesiredWidth` | 向按内容定尺寸的父级报告的宽度下限，让一个暂时很短的标签（倒数的数字、还没到的名字）不把整行压塌 |
| `bBestFit` / `BestFitMinSize` | Best Fit：缩小字号直到放进内容盒。`FontSize` 变成**允许达到的**字号，`BestFitMinSize` 是最小能缩到多少，再小就任它溢出。`GetRenderedFontSize` 是实际画出的字号 |

Best Fit 来自 uGUI，UMG 和 Slate 都没有：它们的文本按内容定尺寸，盒子随文本长大；这里盒子是写出来的，恰恰需要它。
在面板里测量一段会换行的文本有一个坑，见[布局](layout.md)最后一节。

```dui
Widget Panel {
    Text Body {
        AnchorData.SizeDelta = (360, 120)
        Text = "盒子是作者定的：文本在盒子里对齐，装不下就按 OverflowType 处理。"
        FontSize = 16
        OverflowType = Ellipsis
        bAutoWrapText = true
        Margin = (12, 8, 12, 8)
        LineHeightPercentage = 1.2
    }

    Text Price {
        AnchorData.SizeDelta = (120, 40)
        Text = "1,234,567"
        FontSize = 32
        bBestFit = true
        BestFitMinSize = 12
    }

    Text Score {
        bRichText = true
        Text = "得分 <size=40><gradient=Gold>9999</gradient></size>"
    }
}
```

其他排版属性：`bUseKerning`、`bLigatures`（默认开）、`FontSpace`（字距与行距的固定加量）、`TabSize`（制表位，以空格计，默认 8，
CSS 的 `tab-size`）、`TextJustify` 和 `LastLineAlign`（两端对齐时额外空间加在哪、最后一行怎么对齐）、`TextTransform`（只改显示的大小写）、
`FlowDirection`（`Auto` 交给双向算法）、`bUnderline` 和 `bStrikethrough`（富文本的 `<u>`、`<s>` 叠在它们之上）。

<a id="字体"></a>

## 字体

字体是资产，一条继承链上是**四个**类型：抽象基类 **`UDreamUIFontData_BaseObject`**（「字体数据能做什么」的全部接口 ——
要字形、要整形字体、要行高、要回退面），中间抽象子类 **`UDreamUIFontData_FreeTypeRender`**（用 FreeType 读字体文件），
以及它的两个子类 —— 距离场字体 **`UDreamUIFontData_DistanceField`**（插件默认字体用这个）和位图字体 **`UDreamUIFontData_Bitmap`**。
⚠️ `_Bitmap` 的源码批注写明它**已不再维护**，新功能不再实现。项目默认字体在 `UDreamGUISettings::DefaultFont`。

- **默认字体就是 Slate 的**：Roboto，带真正的常规、粗体、斜体、粗斜体字面，CJK 用 DroidSansFallback，画在轮廓多通道距离场上
  （`/DreamGUI/DefaultFont_DistanceField`）。
- **距离场**：`SdfSource` 默认 `OutlineMultiChannel`（MTSDF，拐角更锐利，BGRA 图集）。描边、下衬（投影）、发光和歌词式填充这些效果
  （`TextStyle`，即 `FDreamTextStyle`）由内置着色器画，需要距离场字体并且开着 *Use Built-in UI Shader*；长度以 em 计，所以同一个样式在任何字号下看起来一样。
  样式按控件存进画布的控件属性纹理，所以样式不同的文本仍然合进同一个 draw call。
- **粗体和斜体**：`BoldFont`、`ItalicFont`、`BoldItalicFont` 指向真正的字面；没有时才合成。
- **回退字体**：`Fallbacks` 里每一项（`FDreamUIFontFallback`）有 `Font`、`Ranges`（码点范围）、`Cultures`（适用的语言）、`Scale`、
  `bPreferOverPrimary`。旧的 `FallbackFontArray` 在加载时转成默认设置的 `Fallbacks` 项。中文的文化名会被补上书写体系
  （`zh-Hans` / `zh-Hant`），所以按它们写的回退在打包后的游戏里也能命中。
- **彩色 emoji**：支持 CBDT/CBLC、sbix、COLRv0；`bPreferColorEmoji` 默认开。阴影、描边、长阴影修改器给彩色 emoji 做的拷贝是剪影。

怎么随游戏发出字体文件、彩色 emoji 字体和 ICU 数据，见[字体与打包](../guides/fonts-and-packaging.md)。

<a id="小字号"></a>

## 小字号

小字号的屏幕文本 —— 每 em 不超过 `SmallTextMaxPixelSize` 个设备像素，默认 20 —— 从**提示过的覆盖字形**绘制，放在设备像素网格上，
和 Slate 一样清晰（`UDreamGUISettings::bSmallTextCoverage`，默认开）。更大的字照常从距离场画。

- 带描边、发光或下衬的小字：字面用覆盖字形（描边薄于 2 个设备像素时不提示），效果仍从距离场画、对齐到它
  （`SmallTextEffectFace`，`Auto`；设成 `Field` 让这种文本完全留在距离场上）。
- 渲染层里的小字：层静止 3 帧之后才改用覆盖字形，移动时用距离场。
- 字面柔化（softness）和膨胀（dilation）会让文本留在距离场上。
- 单个文本可以用 `SmallTextRaster = Off` 永远留在距离场；单个字体有自己的 `SmallTextCoverage`、`SmallTextMaxPixelSize`、`CoverageHinting`。
- `SmallTextContrast`（默认 1）调覆盖字形的对比度；`bSmallTextCorrection`（默认开）让这些字号下仍从距离场画的字加深一些。
  一个世界每帧最多把 `SmallTextRepaintBudgetPerFrame`（512）个文本重绘到覆盖字形上。
- 字体在有文本还在用的时候保留覆盖字形的图集格子；超过两倍 `MaxCoverageCells`（默认 4，在 *Project Settings > Plugins > DreamUI*）
  后，新的小字字形排队等待，期间从距离场画，每个字体只警告一次。
- 不通过 `MF_DreamUI_Shade` 着色的自定义材质拿不到覆盖字形。

<a id="富文本"></a>

## 富文本

`bRichText` 打开后文本里的标签生效；`RichTextTagFilterFlags` 决定哪些种类被解析（`Bold`、`Italic`、`Underline`、`Strikethrough`、
`Size`、`Color`、`Superscript`、`Subscript`、`CustomTag`、`Image`、`Hyperlink`、`Language`、`Gradient`）；被过滤掉的标签当普通文字显示
（例如关掉 `Language` 时的 `<lang>`）。
例如 `<b>`、`<u>`、`<s>`、`<size=40>`、`<color=…>`、`<sup>` / `<sub>`、行内图片 `<img=Tag/>`（可带尺寸和 `baseline`、`top` 这类对齐词）、
可点击的 `<a=Id>`（`OnHyperlinkClickedBP`）、`<lang=ja>`（里面的文字按那种语言选回退字体和字形），以及下一节的 `<gradient=Name>`。
字面的 `<` 写成 `&lt;`。

自定义标签和样式来自 `RichTextCustomStyleData`（`UDreamUIRichTextCustomStyleData`），行内图片来自 `RichTextImageData`。
控件库里的 [`UDreamRichTextBlock`](../Reference/DreamRichTextBlock.md) 是对应 UMG `RichTextBlock` 的控件。

<a id="渐变文字"></a>

## 渐变文字

文本的字面、描边，以及叠在字面上的一层，各自可以用渐变来画。三层都在 `FDreamTextStyle` 上：`FacePaint`、`OutlinePaint`、
`OverlayPaint`，每层是一个 `FDreamTextPaint`（`bEnabled`、共享预设 `Preset`、或自带的 `Gradient`）。

- **种类**：`Linear`、`Radial`、`Conic`、`Diamond`、`Corners`（四角色，TextMeshPro 的那种），最多 16 个色标；超出两端时 `Pad`、`Repeat`、`Reflect`；
  在 sRGB、线性光或 Oklab 里混色。
- **写法和 CSS 一样**：`linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)`，在细节面板、`.dui` 里，或作为富文本的一段。
  按 Chrome 的方式读，只有两点不同：`to <corner>` 无论盒子什么形状都取正方形的角度；颜色名是 CSS 的（`green` 是 #008000，而 `<color=green>` 是 #00FF00）。
- **量在什么上**：`PaintBoxHorizontal` / `PaintBoxVertical` 分轴选 `TextBlock`、`ContentBox`、`Line`、`Glyph` 或 `Run`。
- **字面的渐变替代文本自己的颜色**，就像 CSS 的 `background-clip: text`；里面的 `<color>` 段是纯色。叠加层用 `OverlayBlend`（`Normal`、`Add`）混到字面上。
- **预设**：`UDreamGradientAsset` 资产，或项目设置里按名字存的 CSS 字符串（`GradientPresets`）。用同一个渐变的文本在 GPU 上共享一行。
- **富文本**：`<gradient=Name>…</gradient>`，名字依次查自定义样式里的同名项、项目预设，最后把名字本身当 CSS 读（不带空格）。
  `Gradient` 这个过滤位是新加的：`RichTextTagFilterFlags` 存成不含它的掩码的文本会把标签当文字显示。

渐变的相位、角度、中心和缩放可以动画，而**不重新布局、也不重绘**文本 —— 每次只写文本 paint 表里的一个像素。
`FacePaintPhase`、`OutlinePaintPhase`、`OverlayPaintPhase`、`PaintAngleOffset` 能在 Sequencer 里打关键帧；
[`UDreamTextPaintLibrary`](../Reference/DreamTextPaintLibrary.md) 提供 `PaintPhaseTo`、`PaintAngleTo` 和 `PlayShimmer`：

```cpp
#include "Core/Text/DreamTextPaintLibrary.h"

// Title 是一个 UDreamText*
FDreamTextPaint Face;
Face.bEnabled = true;
FDreamGradient::ParseCss(TEXT("linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)"), Face.Gradient);
Title->SetFacePaint(Face);

// 一条两端透明的亮带，以 Add 叠到字面上：这就是一道扫光
FDreamTextPaint Band;
Band.bEnabled = true;
FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, transparent 40%, white 50%, transparent 60%)"), Band.Gradient);
Title->SetOverlayPaint(Band);
Title->SetOverlayBlend(EDreamTextOverlayBlend::Add);

UDreamTextPaintLibrary::PlayShimmer(Title, 1.2f, 0.0f, -1);   // 每遍 1.2 秒，直到停止
```

不通过 `MF_DreamUI_Shade` 着色的材质改用顶点色画渐变（这时动画会触发重绘）。

<a id="按文化断行"></a>

## 按文化断行

断行和分词的迭代器按**游戏当前的文化**创建，文化变化时重建（2.0 跟随的是操作系统的语言）。

- 有 CJK 文本的游戏要连同对应的 ICU 数据一起打包（EFIGSCJK 预设；泰语及其邻近语言要 All）。
- 编辑器里，DreamGUI 自己的那份 ICU 被指向引擎的 ICU 数据（`Engine/Content/Internationalization`），所以编辑器和打包后的游戏一样按游戏文化断行；
  读不到数据时日志说一次，退回引擎默认文化的迭代器。
- `PhraseWrap = CJKDictionary` 换行时用 ICU 的词典把 CJK 词语保持在一起（CSS 的 `word-break: auto-phrase`），只对 `VerticalOverflow` 有意义，
  需要打包的 ICU 数据含 CJK 词典，否则悄悄退回逐字断行。
- `Language` 写文本所用的语言（`"ja"`、`"zh-Hans"`、`"en-US"`）：优先用为它准备的回退字体，HarfBuzz 也按语言选字形（`locl`）。
  留空是游戏当前语言；富文本的 `<lang=xx>` 覆盖它。断行**始终**跟随游戏文化，不管这里写什么。

<a id="增量布局"></a>

## 增量布局

正在被输入的文本框，或者长文本（256 个元素以上）在连续两次布局里内容都变了，就会保留上次布局找到的东西，只重新布局编辑触及的部分：
在长段落里，只有编辑点两边的一个窗口被重新整形和测量，再拼回去；显示列表也在原处编辑，而不是整个重写。
一个以前整形过的词 —— 同样的字面、字号、书写系统、方向、语言和特性 —— 从整形缓存里取，不再整形。

增量布局和从零开始的布局，结果逐个元素完全相同；下面那些开关只是拿时间换。编辑落在以标点、数字或 emoji 开头的长段落开头附近，
或者紧跟在行内图片之后时，按整段的方式整形。

<a id="控制台变量"></a>

## 控制台变量

| 变量 | 默认 | 作用 |
| --- | --- | --- |
| `DreamGUI.Text.SmallTextCoverage` | `-1` | `-1` 按项目设置；`0` 对所有字体关掉覆盖字形（包括设成 On 的）；`1` 对所有交给项目决定的字体打开。改动会重绘所有文本 |
| `DreamGUI.Text.SmallTextMaxPixelSize` | `0` | 大于 0 时，替换所有没有自己上限的字体的 `SmallTextMaxPixelSize`，用来把覆盖字形强加到更大的标签上做测量 |
| `DreamGUI.Text.SmallTextOnMove` | `0` | 用覆盖字形画的小字移出像素网格时：`0` 每次移动都从覆盖字形重绘；`1` 移动时保留覆盖四边形、静止 3 帧后重绘；`2` 先从距离场重绘一次，静止 3 帧后再回到覆盖字形 |
| `DreamGUI.Text.IncrementalLayout` | `1` | `0`：每次布局都从零开始，什么也不保留 |
| `DreamGUI.Text.IncrementalParse` | `1` | `0`：每个元素都重新读 |
| `DreamGUI.Text.IncrementalMeasure` | `1` | `0`：被编辑的段落整段测量（需要 `IncrementalParse`） |
| `DreamGUI.Text.InPlaceDisplayList` | `1` | `0`：显示列表整个重写 |
| `DreamGUI.Text.VerifyIncremental` | `0` | `1`：每个基于保留结果的布局都再从零做一遍并比较，记下第一处差异、改用新结果。用来找增量布局的错，Shipping 构建里没有 |
| `DreamGUI.Text.ShapeCache` | `1` | `0`：每段都整个整形，不用缓存 |
| `DreamGUI.Text.ShapeCacheKB` | `4096` | 整形缓存的预算，单位 KB；4096 约能放 15,000 个词，最久未用的先走。改动会清空缓存 |
| `DreamGUI.Text.ShapeCacheFlush` | 命令 | 清空整形缓存，计数保留 |

另有两个开关在测量出结论之前默认关着：项目设置 `bFieldTextCorrection`（`SmallTextMaxPixelSize` 以上的距离场文本也用小字的对比度和
线性光混合，免得字重在分界处跳变），和控制台变量 `DreamGUI.Scroll.SnapToDevicePixels`（2D 画布上的滚动视图按整设备像素移动内容）。
`DreamGUI.Memory` 列出每个字体的字形图集（切片、GPU 字节和 CPU 拷贝、格子、字形、字面字节）。
