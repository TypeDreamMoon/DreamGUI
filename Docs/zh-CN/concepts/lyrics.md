---
title: 歌词
description: UDreamLyricsView —— 逐词点亮、弹簧驱动的歌词视图，LRC 与 TTML 两种输入，以及它的分档参数。
---

`UDreamLyricsView`（`DreamGUIExtensions`）是一个**逐词点亮**的歌词控件，插件自己的自述是 **Apple Music 式**。

<a id="它长什么样"></a>

## 它长什么样

- 每行歌词是一个**文本行**；
- **当前行被固定在一个高度上**，其它行在**弹簧**驱动下从上下滑进滑出；
- **被唱到的词通过文本的 fill 通道点亮**；
- **离当前行越远的行越柔、越淡**。

它挂在**当作歌词框的那个控件**上 —— 宽度收拢歌词行、高度就是可见区域。

<a id="怎么驱动"></a>

## 怎么驱动

正规用法是用 `SetTime` 让**音频时钟**驱动它；预览时用 `Play()` 让它自己跑。

| 入口 | 作用 |
| --- | --- |
| `LoadLRC(Text)` / `LoadTTML(Xml)` | 两种歌词文本格式的解析入口 —— 这两个在 **`UDreamLyricsLibrary`**（一个 `UBlueprintFunctionLibrary`）上，不只给视图用 |
| `SetLyrics` / `RebuildLines` | 换歌词、按歌词重建行控件（`Awake` 时也会调） |
| `SetTime` / `GetTime` / `Seek` | 外部时钟驱动；`Seek` 是**不带动画**的跳转 |
| `Play` / `Pause` / `bPlaying` / `PlaybackRate` | 自走模式 —— `bPlaying` 默认 **false**，也就是**默认由外部时钟驱动** |
| `OnActiveLineChanged` | 当前行变化的事件，带行号 |
| `GetActiveLineIndex` / `GetLineCount` / `GetLineText` | 查询 |

<a id="分档参数"></a>

## 分档参数

括号里是默认值。

| 组 | 参数 |
| --- | --- |
| 文字 | 字号 **36**；和声行按 `BackgroundLineScale` **0.7** 缩；被唱够久的词（`EmphasisMinDuration` **1 秒**）加 `EmphasisGlowBoost` **3 倍**辉光；译文按 `TranslationScale` **0.6** 缩且默认开（`bShowTranslation`） |
| 布局 | 当前行固定在 `ActiveLineAnchor` **0.25**（从顶往下 25%）；行距 **24** |
| 运动 | 位置与缩放**各一套弹簧参数**；行间按 `StaggerSeconds` **0.04 秒**错开、最多累到 **8 行** |
| 非当前行 | 缩放 **0.92**、透明度 **0.55**，**唱过的行 0.35**；每远一行加 **0.04 em** 的柔化（最多 4 行） |

它建在**弹簧**上（见[动画](animation.md)的弹簧一节）—— 这正是「重定目标保留速度」那条性质的用武之地：一行歌词滑向新位置时**接着用原来的速度**，而不是从头再来一遍。

<a id="它是-2026-年新写的"></a>

## 它是 2026 年新写的

文件头版权是 `Copyright 2026-Present TypeDreamMoon`，**没有 `Lex Liu` 那一行** —— 在插件里这是少数几个代码注释与版权都出自新作者的模块。
