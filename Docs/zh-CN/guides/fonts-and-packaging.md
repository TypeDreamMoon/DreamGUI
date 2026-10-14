---
title: 字体与打包
description: 让 DreamGUI 的文本在打包后的游戏里和在编辑器里看起来一样：哪些字体自己会跟着走、怎么加彩色 emoji、游戏要带哪套 ICU 数据，以及怎么检查一个包。
---

这一页讲的是一段文本在打包后的游戏里要和编辑器里一样，需要什么：哪些字体自己会跟着走、哪些要你加，
怎么加一个彩色 emoji 字体，游戏要和哪套 ICU 数据一起打包，以及怎么检查一个包。这里说的都是插件在 Win64 上的行为；
没有别的平台构建过（见[平台](platforms.md)）。

<a id="自己会跟着走的"></a>

## 自己会跟着走的

<a id="默认字体"></a>

### 默认字体

`/DreamGUI/DefaultFont_DistanceField` 是 Roboto 的四个真实字面（常规、粗体、斜体、粗斜体），后面垫着 DroidSansFallback
负责中文、日文和韩文，每个字面都在轮廓多通道距离场上（怎么构建的见插件的 `Tools/Fonts/README.md`）。这五个资产都指向一个
引擎字体字面（`FontType` 为 Engine Font）：

- 烹饪时，`BeginCacheForCookedPlatformData` 把每个字面的字节拷进它的资产。编辑器之外，字体只读这些字节，所以什么都不用额外 stage。
- `/DreamGUI` 总会被烹饪：插件的 `Config/Game.ini` 把它放进了 `DirectoriesToAlwaysCook`，所以即使按显式包列表烹饪（`-map=`、chunk）也有它。
- DroidSansFallback 给包增加大约 3.9 MB。

<a id="按语言选字面"></a>

### 按语言选字面

字体的 `Fallbacks` 条目可以写明自己是给哪些文化用的（`Cultures`，如 `"zh-Hans"`、`"ja"`）：语言是这个文化的文本
（它的 `Language`，或富文本里的 `<lang=xx>`）会在字体的其它后备之前先试这个字面；条目开了 `bPreferOverPrimary` 时，
甚至排在字体自己的字面之前。

优先用这种条目，而不是 `bCultureFont` 和它的 `CultureFontMap`。那个开关在游戏文化改变时替换字体自己的字面，而它只对
加载策略为 Inline 的引擎字体字面有效：烹饪后 Lazy Load 或 Stream 的字面没有可供替换的字面数据，字体会打一条警告，
继续用烹饪时的那个字面。

<a id="彩色-emoji"></a>

## 彩色 emoji

默认字体**有意**不带彩色 emoji 字面。引擎的 `NotoColorEmoji.ttf` 在 `Engine/Content/Editor/Slate/Fonts` 下，那是打包后的游戏
拿不到的编辑器内容（Slate 只在编辑器里加载它）；而把它嵌进插件的字体，会给每个用 DreamGUI 的项目增加 7.8 MB。
没有彩色字面时，一个 emoji 有 `EmojiData` 条目（每个 emoji 一张图）就从那里画，否则从任何含有这个码位的单色字面画，
或者画成缺字方框。

<a id="选字体并留意许可证"></a>

### 选字体，并留意许可证

DreamGUI 能画 CBDT/CBLC 和 sbix 位图（Noto Color Emoji 属于前者）以及 COLRv0 图层的彩色字形。只有 COLRv1 或 SVG 数据的
字形算作缺字，WOFF2 字体则根本加载不了（引擎的 FreeType 没有 Brotli）。许可证各不相同：Noto Color Emoji 是 OFL ——
要随游戏附上它的许可证；Twemoji 的图是 CC-BY；Segoe UI Emoji 不允许再分发。

<a id="给它建一个字体资产三选一"></a>

### 给它建一个字体资产，三选一

先把字体文件拷进项目，然后为它建一个距离场字体资产（内容浏览器里的 *DreamUI FontData DistanceField*）。
它自己的设置几乎无关紧要：后备字形是画进**文本所用那个字体**的图集的，彩色字形按它自己的像素尺寸画，而不是画成距离场。

<a id="自定义字体文件嵌入资产"></a>

### 自定义字体文件，嵌入资产

`FontType` 为 Custom Font File，`bUseExternalFileOrEmbedInToUAsset` 关（默认）。编辑器读文件，把字节存进资产。
烹饪时如果文件还在，会再读一遍，所以文件改过之后没重存的资产不会发出过期的字节；烹饪时找不到可嵌入的东西是一个错误，
而不是悄悄发出一个没有字面的字体。

<a id="引擎字体字面"></a>

### 引擎字体字面

`FontType` 为 Engine Font。把文件导入成一个 Font Face，填进 `EngineFont`；烹饪时拷贝它的字节，和默认字体一样。三种里最稳的一种。

<a id="外部文件"></a>

### 外部文件

`bUseExternalFileOrEmbedInToUAsset` 开。游戏在运行时读这个文件，所以它必须被 stage：对 `Content/Fonts` 下的文件，在
`Config/DefaultGame.ini` 的 `[/Script/UnrealEd.ProjectPackagingSettings]` 下加
`+DirectoriesToAlwaysStageAsUFS=(Path="Fonts")`；`FontFilePath` 必须相对于项目目录（`bUseRelativeFilePath`）。
绝对路径在打包后的游戏里永远不起作用。

<a id="把它加成文本字体的后备只管-emoji"></a>

### 把它加成文本字体的后备，只管 emoji

在文本字体的 `Fallbacks` 里加一个条目，让它的 `Ranges` 只覆盖 emoji、别的都不管，这样它的数字、空格和符号永远不会顶替
文本字体自己的：

| 范围 | 是什么 |
| --- | --- |
| `0x23`、`0x2A`、`0x30`-`0x39` | 键帽的底字（`#`、`*`、数字），用于 `1` U+FE0F U+20E3 这样的序列 |
| `0xA9`、`0xAE` | (c) 和 (r) |
| `0x203C`-`0x3299` | 符号、箭头、装饰符号和带圈表意字中的 emoji |
| `0x1F000`-`0x1FAFF` | emoji 区块，含旗帜 |

范围匹配的是一个字簇的第一个码位；后面跟着的连接符、变体选择符、肤色和标签不需要自己的范围。`Cultures` 留空，`Scale` 保持 1。

**文本字体必须能装颜色。** 它要么在轮廓多通道距离场上（`SdfSource` 为 Outline Multi Channel，默认），要么是位图字体。
单通道距离场上的字体图集是 R8 —— 一个通道、没有颜色 —— 一个彩色字形也画不了：它的 emoji 会退回 `EmojiData` 或单色字面。

要求 emoji 呈现的字簇 —— 默认以 emoji 呈现的象形符号、后面跟 U+FE0F 的任何字符、旗帜、键帽、肤色、ZWJ 序列 —— 先试彩色字面
（字体的 `bPreferColorEmoji`，默认开）。其余都是文本呈现，先试单色字面，所以这些范围里的数字和符号只要文本字体有，仍然来自文本字体。
针对完整序列的 `EmojiData` 条目仍然优先于彩色字面。

**警告：**

**自己的材质。** 彩色字形、小字号的覆盖率字形和渐变绘制，都由 DreamGUI 自己的着色（`MF_DreamUI_Shade`）解码；
  一个材质带着这个函数的 `DreamUI_ShadeMarker` 参数时，才算经过它着色，DreamGUI 自己的材质都带着。用的材质不经过它时，
  文本会按"字体没有彩色字面"来排 emoji，而不是把一张彩色位图当距离场去画；它也拿不到覆盖率字形，渐变改用顶点色绘制。

<a id="代价"></a>

### 代价

Noto Color Emoji 的字节大约占 7.8 MB 内存，由它的字体资产持有；彩色字形本身在文本字体的图集里，每个 emoji 每个尺寸档一个。
一个图集切片默认是 2048 × 2048 BGRA：GPU 上 16 MiB，字体在 CPU 上保留的副本再占同样多，每个字体最多 `MaxFontAtlasSlices`（8）片。
控制台命令 `DreamGUI.Memory` 打印每个字体的图集装了什么 —— 切片、GPU 和 CPU 字节、格子、距离场字形、彩色字形和覆盖率字形 ——
`DreamGUI.Memory Json` 以 JSON 输出同样的内容。

<a id="库在打包后的游戏里做什么"></a>

## 库在打包后的游戏里做什么

打包后的游戏链接的库和编辑器相同：

- **FreeType 2.14.1**（引擎的 Win64 Release 库）：PNG 位图（CBDT、sbix）和 COLRv0 图层可用；没有 Brotli，所以 WOFF2 字体加载不了；
  SVG 字形被跳过，只有 COLRv1 的字形算作缺字。
- **HarfBuzz 2.4.0** 在除专用服务器之外的所有目标上做字形整形；专用服务器不画文本。
- **ICU 64** 负责断行、断词，并回答一个文化会退回到哪些名字。`Emoji_Presentation` 和 `Extended_Pictographic` 编译在库里，
  所以 emoji 检测在任何打包预设下都可用；更新的 emoji 来自插件自己的表。

只用过 Win64 的库，这些库在其它平台的构建从没被链接进 DreamGUI。打包后的游戏实际怎样，由打包文本冒烟测试（见下文）检查；
在发布闸门为一个版本跑过它之前（见[平台](platforms.md)），这一节说的"打包后的游戏"，其实是编辑器用同样的库所做的事。

<a id="icu-数据打包-efigscjk或者-all"></a>

## ICU 数据：打包 EFIGSCJK，或者 All

游戏带多少 ICU 数据由打包预设决定：*Project Settings ▸ Packaging ▸ Internationalization Support*（`InternationalizationPreset`），
默认是 English；`BuildCookRun` 的 `-I18NPreset=` 可以覆盖它。编辑器总是有全部数据，所以文本可能在编辑器里一种排法、在包里另一种。

**任何有中文、日文或韩文文本的游戏都用 EFIGSCJK 打包；有泰文、老挝文、高棉文或缅甸文的用 All。** 在 English 预设下：

- **游戏根本无法把文化切换**到中文或日文。
- **断行失去词典。** English 只带最基本的字符、单词和行规则。CJK 文本于是在任意两个字符之间断开，没有短语词典
  （`PhraseWrap`，即 CSS 的 `word-break: auto-phrase`，退回逐字断行），也没有日文和中文的断行裁剪规则；
  泰文、老挝文、高棉文和缅甸文需要只有 `All` 才带的词典，只能在空格处断行。

<a id="中文按书写系统退回"></a>

### 中文按书写系统退回

引擎通过 ICU 的 likely subtags 找出一个文化用哪种书写系统，而 English 和 EFIGSCJK 的数据都不带它：在打包后的游戏里，
`"zh-CN"` 只退回到 `zh-CN, zh`，而编辑器里是 `zh-Hans-CN, zh-CN, zh-Hans, zh`。DreamGUI 给中文名补回了书写系统 ——
`zh-CN`、`zh-SG` 和 `zh` 补 `Hans`，`zh-TW`、`zh-HK` 和 `zh-MO` 补 `Hant` —— 所以给 `"zh-Hans"` 和 `"zh-Hant"` 的后备条目在包里
和在编辑器里一样能匹配上。没有这一步，这类条目在包里永远匹配不到一段 zh-CN 文本，一个同时有日文和简体中文后备的字体
就可能把汉字画成日文字形。其它语言的条目按语言和地区匹配，那是每个预设都有的。

<a id="断行跟随游戏的文化"></a>

### 断行跟随游戏的文化

行与词的迭代器按当前文化的 locale（`FInternationalization` 的当前文化）创建，文化改变时重新创建。早先它们跟的是操作系统的语言，
同一个游戏在日文 Windows 和英文 Windows 上断日文行的方式不同。

<a id="检查一个包"></a>

## 检查一个包

- 在打包游戏（Development）的控制台里，`DreamGUI.Memory` 打印字体、精灵图集和画布占了什么；`DreamGUI.Memory File=<path>`
  把它写成 JSON，相对路径写在 `Saved/` 下。
- 插件测试宿主里的**打包文本冒烟测试**，把一屏这些情况 —— 小字号文本、混排的一行、按文化后备的中文和日文、彩色 emoji、两端对齐的段落、
  日文段落、安全区 —— 分别放进一个打包游戏和同一构建在未烹饪内容上的运行里，逐字段比对两边（安全区各自对照自己的视口）。
  发布闸门在打标签之前跑它。怎么跑见 `Tools/TestHost/README.md` 的 "The packaged text smoke test" 一节和[测试](../tools/tests.md)。
