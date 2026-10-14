---
title: "UDreamMultiLineEditableText"
description: "The same field over several lines: UMG's MultiLineEditableText."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from [UDreamEditableText](DreamEditableText.md).

Declared in `Public/Controls/DreamEditableText.h`.

The same field over several lines: UMG's MultiLineEditableText.

A subclass rather than a palette entry that ticks bMultiLine, unlike the boxed pair -- and the
difference is worth stating. `Native.TextInput` plus `bMultiLine = true` IS the multi-line boxed
field, and the palette offers it as a second row of the same class (see the registry). A CLASS
exists here instead because UMG has one, because `.dui` gains a tag that reads the way the UMG
documentation reads, and because a Blueprint that wants to subclass "the multi-line borderless
field" has something to derive from. It sets exactly one property.
