---
title: "UDreamLyricsLibrary"
description: "Dream Lyrics Library"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from `UBlueprintFunctionLibrary`.

Declared in `Public/Extensions/Lyrics/DreamLyricsData.h`.

Dream Lyrics Library

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `bool ParseLRC(FString Text, FDreamLyrics& OutLyrics)` | callable | Parse LRC: "[mm:ss.xx]text" lines, with inline &lt;mm:ss.xx> word times when present (enhanced LRC). Line ends are the next line's start. |
| `bool ParseTTML(FString Xml, FDreamLyrics& OutLyrics, FString& OutError)` | callable | Parse TTML of the kind Apple Music and AMLL use: &lt;p begin end> lines of &lt;span begin end> words, with ttm:agent for the second voice, ttm:role="x-bg" spans for backing vocals and ttm:role="x-translation" / "x-roman" spans for the secondary texts. A &lt;p> without word spans becomes one word spanning the line. |
