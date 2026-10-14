---
title: "打包游戏中的字体"
description: "打包游戏中的字体"
---

<a id="fonts-in-a-packaged-game"></a>

# 打包游戏中的字体

DreamGUI 文本要在打包游戏里看起来和编辑器里一样，需要哪些条件：哪些字体会自己打包进去、
哪些需要你添加、如何添加彩色 emoji 字体、游戏必须随包打包哪些 ICU 数据，以及如何检查一个
包。这里的一切都是插件在 Win64 上的行为；没有构建过其他平台（见 README 的
[平台](https://gui.toolchain.64hz.cn/en/docs/guides/platforms)）。

<a id="1-what-ships-by-itself"></a>

## 1. 哪些会自己打包进去

**默认字体。** `/DreamGUI/DefaultFont_DistanceField` 是四个真实字面的 Roboto（常规、粗体、斜体、粗斜体），
后面跟着 DroidSansFallback 负责中文、日文和韩文，每个字面都在轮廓多通道场
（`Tools/Fonts/README.md` 说明了它的构建方式）。全部五个资产都指定了一个引擎字体字面（`FontType` Engine Font）：

- 在烹饪时，`BeginCacheForCookedPlatformData` 会把每个字面的字节复制到它的资产中。在编辑器之外，字体只读取
  这些字节，因此不需要额外暂存任何内容。
- `/DreamGUI` 总会被烹饪：插件的 `Config/Game.ini` 会把它放入 `DirectoriesToAlwaysCook`，因此即使是显式包列表
  （`-map=`、块）的烹饪也能得到它。
- DroidSansFallback 会给包增加约 3.9 MB。

**按语言区分的字面。** 字体的 `Fallbacks` 条目可以指定它们所针对的文化（`Cultures`、`"zh-Hans"`、
`"ja"`）：该语言的文本（它的 `Language`，或富文本的 `<lang=xx>`）会先尝试该字面，再尝试字体的其他
回退，并且当条目带有 `bPreferOverPrimary` 时，也会先于字体自身的字面。优先使用这些
条目，而不是 `bCultureFont` 及其 `CultureFontMap`。该开关会在游戏文化
变化时替换字体自身的字面，并且只对使用 Inline 加载策略的引擎字体字面有效：已烹饪的 Lazy Load 或 Stream 字面
没有可供替换的字面数据，因此字体会记录一条警告并保留烹饪时所用的字面。

<a id="2-colour-emoji"></a>

## 2. 彩色 emoji

默认字体有意没有彩色 emoji 字面。引擎的 `NotoColorEmoji.ttf` 位于
`Engine/Content/Editor/Slate/Fonts` 下，那是打包游戏无法获得的编辑器内容（Slate 只在
编辑器中加载它），而将它嵌入插件的字体中会让每个使用 DreamGUI 的项目增加 7.8 MB。没有彩色字面时，emoji
在有条目时会从文本的 emoji 数据（`EmojiData`，每个 emoji 一张图像）绘制，否则从任何
拥有该码位的单色字面绘制，或者绘制为缺字方框。

<a id="pick-a-font-and-mind-its-licence"></a>

### 选择字体，并留意其许可证

DreamGUI 可以从 CBDT/CBLC 和 sbix 位图描边（Noto Color Emoji 就是其中之一）以及 COLRv0 图层中绘制彩色。只有 COLRv1 或 SVG 数据的
字形会被视为缺失，而 WOFF2 字体根本无法加载（引擎的 FreeType 没有
Brotli）。许可证各不相同：Noto Color Emoji 是 OFL——随游戏附带其许可证——Twemoji 的图形是 CC-BY，而 Segoe UI
Emoji 不得再分发。

<a id="make-a-font-asset-for-it-one-of-three-ways"></a>

### 为它创建字体资产，三种方式之一

首先将字体文件复制到你的项目中。然后为它创建一个距离场字体资产（内容浏览器中的 *DreamUI FontData
DistanceField* 资产）。它自己的设置几乎无关紧要：回退字体的字形会被绘制到
文本所用字体的图集中，彩色字形按其自身像素大小绘制，而不是作为场。

1. **自定义字体文件，嵌入**（`FontType` Custom Font File，`bUseExternalFileOrEmbedInToUAsset` 关闭：默认值）。
   编辑器读取文件并将其字节保存到资产中。自 2.1 起，烹饪时如果文件还在，会再次读取它，因此文件更改之前
   保存的资产不会打包过期的字节，而找不到可嵌入内容的烹饪会报错，而不是生成一个静默地没有
   字面的字体。
2. **引擎字体字面**（`FontType` Engine Font）。将文件作为 Font Face 导入并在 `EngineFont` 中指定它；烹饪时
   会像默认字体一样复制其字节。三种方式中最稳健的一种。
3. **外部文件**（`bUseExternalFileOrEmbedInToUAsset` 开启）。游戏在运行时读取该文件，因此它必须被
   暂存——对于 `Content/Fonts` 中的文件，在
   `Config/DefaultGame.ini` 的 `[/Script/UnrealEd.ProjectPackagingSettings]` 下使用 `+DirectoriesToAlwaysStageAsUFS=(Path="Fonts")`——并且 `FontFilePath` 必须相对于项目
   目录（`bUseRelativeFilePath`）。绝对路径在打包游戏中永远无法工作。

<a id="add-it-as-a-fallback-of-the-text-font-for-the-emoji-only"></a>

### 将它添加为文本字体的回退，仅用于 emoji

将它添加到文本字体的 `Fallbacks` 中，作为一个条目，其 `Ranges` 仅覆盖 emoji 而不覆盖其他任何内容，这样它的数字、
空格和符号就永远不会替代文本字体自身的字符：

| 范围 | 内容 |
|---|---|
| `0x23`、`0x2A`、`0x30`-`0x39` | 键帽基础字符（`#`、`*`、数字），用于 `1` U+FE0F U+20E3 之类的序列 |
| `0xA9`、`0xAE` | (c) 和 (r) |
| `0x203C`-`0x3299` | 符号、箭头、装饰符号和带圈表意文字中的 emoji |
| `0x1F000`-`0x1FAFF` | emoji 区块，包括旗帜 |

范围是相对于字簇的第一个码位进行匹配的；其后的连接符、变体选择符、肤色和标签
不需要自己的范围。将 `Cultures` 留空，`Scale` 设为 1。

**文本字体必须能够承载颜色。** 它必须在轮廓场上（`SdfSource` Outline Multi Channel，默认）
或者是位图字体。单通道场上的字体会将其图集保持为 R8——单个通道，没有颜色——并且完全不会绘制
彩色字形：其 emoji 会回退到 `EmojiData` 或单色字面。

要求 emoji 呈现的字簇——默认绘制为 emoji 的象形图、任何后跟 U+FE0F 的字符、旗帜、
键帽、肤色、ZWJ 序列——会先尝试彩色字面（字体的 `bPreferColorEmoji`，默认开启）。
其他所有内容都处于文本呈现，会先尝试单色字面，因此这些范围中的数字和符号
在文本字体拥有它们的地方仍然来自文本字体。针对确切序列的 `EmojiData` 条目仍然优先于
彩色字面。

**你自己的材质。** 彩色字形，与小文本覆盖率字形和渐变绘制一样，由
DreamGUI 自己的着色（`MF_DreamUI_Shade`）解码；当材质带有该函数的
`DreamUI_ShadeMarker` 参数时，就会通过它进行着色，DreamGUI 自己的材质就是这样。自 2.1 起，材质不带该参数的文本
会像字体没有彩色字面一样排布其 emoji，而不是将彩色位图绘制为距离场；它也不会获得覆盖率
字形，并通过顶点颜色绘制其渐变。

<a id="what-it-costs"></a>

### 代价是什么

Noto Color Emoji 的字节约占 7.8 MB 内存，由其字体资产持有，而彩色字形本身则在文本
字体的图集中，每个 emoji 和尺寸分桶各一个。一个图集切片默认是 2048 x 2048 BGRA：GPU 上为 16 MiB，字体在 CPU 上保留的副本
再占同样多，每个字体最多 `MaxFontAtlasSlices` (8) 个切片。控制台命令
`DreamGUI.Memory` 会打印每个字体的图集包含的内容——切片、GPU 和 CPU 字节、单元格、场、彩色和覆盖率
字形——而 `DreamGUI.Memory Json` 以 JSON 打印同样的内容。

<a id="3-what-the-libraries-do-in-a-cooked-game"></a>

## 3. 这些库在烹饪后的游戏中做什么

打包游戏链接的库与编辑器相同：

- **FreeType 2.14.1**（引擎的 Win64 Release 库）：PNG 位图（CBDT、sbix）和 COLRv0 图层可用；没有
  Brotli，所以 WOFF2 字体加载不了；SVG 字形被跳过，只有 COLRv1 的字形算作缺字。
- **HarfBuzz 2.4.0** 在除专用服务器之外的所有目标上做字形整形；专用服务器不画文本。
- **ICU 64** 负责断行、断词，并回答一个文化会退回到哪些名字。`Emoji_Presentation` 和
  `Extended_Pictographic` 编译在库里，所以 emoji 检测在任何打包预设下都可用；更新的
  emoji 来自插件自己的表。

只用过 Win64 的库。这些库在其它平台的构建从没被链接进
DreamGUI。打包后的游戏实际怎样，由打包文本冒烟测试（第 5 节）检查；在发布闸门
为一个版本跑过它之前（见[平台](https://gui.toolchain.64hz.cn/en/docs/guides/platforms)），这一节说的
打包后的游戏，其实是编辑器用同样的库所做的事。

<a id="4-icu-data-package-efigscjk-or-all"></a>

## 4. ICU 数据：打包 EFIGSCJK，或者 All

游戏带多少 ICU 数据由打包预设决定：**Project Settings > Packaging >
Internationalization Support**（`InternationalizationPreset`），默认是 English；`-I18NPreset=` 在 `BuildCookRun`
上覆盖它。编辑器总是有全部数据，所以文本可能在编辑器里一种排法、在包里另一种。

**任何有中文、日文或韩文文本的游戏都用 EFIGSCJK 打包；有泰文、老挝文、高棉文或
缅甸文的用 All。** 在 English 预设下：

- **游戏根本无法把文化切换**到中文或日文。
- **断行失去词典。** English 只带最基本的字符、单词和行规则。CJK 文本于是在任意两个字符之间断开，没有短语词典
  （`PhraseWrap`，即 CSS 的 `word-break: auto-phrase`，退回逐字断行），也没有日文和中文的断行裁剪规则；
  泰文、老挝文、高棉文和缅甸文需要只有 `All` 才带的词典，只能在空格处断行。

**中文按书写系统退回。** 引擎通过 ICU 的 likely subtags 找出一个文化用哪种书写系统，而 English 和 EFIGSCJK 的数据都不带它：在打包后的游戏里 `"zh-CN"` 只退回到 `zh-CN, zh`，而编辑器里是 `zh-Hans-CN, zh-CN, zh-Hans, zh`。从 2.1 起，DreamGUI 给中文名补回了书写系统 —— `zh-CN`、
`zh-SG` 和 `zh` 补 `Hans`，`zh-TW`、`zh-HK` 和 `zh-MO` 补 `Hant` —— 所以给 `"zh-Hans"` 和
`"zh-Hant"` 的后备条目在包里和在编辑器里一样能匹配上。没有这一步，它们从来匹配不到包里的一段 zh-CN 文本，一个同时有
日文和简体中文后备的字体就可能把汉字画成日文字形。其它语言的条目按语言和地区匹配，那是每个预设都有的。

**断行跟随游戏的文化。** 从 2.1 起，行与词的迭代器按当前文化的 locale（FInternationalization 的当前文化）创建，
文化改变时重新创建。早先它们跟的是操作系统的语言，同一个游戏在日文 Windows 和英文 Windows 上断日文行的方式不同。

<a id="5-checking-a-package"></a>

## 5. 检查一个包

- 在打包游戏（Development）的控制台里，`DreamGUI.Memory` 打印字体、精灵图集和画布占了什么；
  `DreamGUI.Memory File=<path>` 把它写成 JSON，相对路径写在 `Saved/` 下。
- 插件测试宿主里的打包文本冒烟测试，把一屏这些情况 —— 小字号文本、混排的一行、按文化后备的中文和日文、彩色 emoji、两端对齐的段落、
  日文段落、安全区 —— 分别放进一个打包游戏和同一构建在未烹饪内容上的运行里，逐字段比对两边 —— 安全区各自对照自己的视口（`Tools/TestHost/README.md`，
  "The packaged text smoke test"）。发布闸门在打标签之前跑它。
