---
title: "FDreamButtonStyle"
description: "A button: one face, three pointer states, a label."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A button: one face, three pointer states, a label.

The face colours are absolute -- the brush stays white and the selectable writes these onto it --
rather than the gallery's habit of a coloured brush multiplied by near-white transition tints.
Absolute colours are the ones a style sheet can actually reason about; a product of two sources
is a look nobody can name.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Height` | `float` | Button Style | yes | read / write |  |
| `Normal` | `FColor` | Button Style | yes | read / write |  |
| `Hovered` | `FColor` | Button Style | yes | read / write |  |
| `Pressed` | `FColor` | Button Style | yes | read / write |  |
| `Disabled` | `FColor` | Button Style | yes | read / write |  |
| `Focused` | `FColor` | Button Style | yes | read / write |  |
| `TransitionDuration` | `float` | Button Style | yes | read / write |  |
| `ContentPadding` | `FMargin` | Button Style | yes | read / write |  |
| `CornerRadius` | `float` | Button Style | yes | read / write |  |
| `FaceBrush` | `FDreamUIFaceBrush` | Button Style | yes | read / write |  |
| `StateFaces` | `FDreamUIStateFaces` | Button Style | yes | read / write |  |
