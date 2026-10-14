---
title: "FDreamRadioButtonStyle"
description: "A radio button: the toggle's anatomy -- a box, a mark inside it, a label beside it -- with the"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A radio button: the toggle's anatomy -- a box, a mark inside it, a label beside it -- with the
mark an image dot rather than a glyph, and the corner radius defaulted to HALF the box so the
face reads as a radio with nobody styling anything.

The box and the dot carry separate colour sets for the same reason the toggle's box and tick do:
the pointer transition and the checked transition are two writers, and pointed at one visual they
overwrite each other.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `BoxSize` | `FVector2D` | Radio Button Style | yes | read / write |  |
| `DotSize` | `FVector2D` | Radio Button Style | yes | read / write |  |
| `BoxNormal` | `FColor` | Radio Button Style | yes | read / write |  |
| `BoxHovered` | `FColor` | Radio Button Style | yes | read / write |  |
| `BoxPressed` | `FColor` | Radio Button Style | yes | read / write |  |
| `BoxDisabled` | `FColor` | Radio Button Style | yes | read / write |  |
| `BoxFocused` | `FColor` | Radio Button Style | yes | read / write |  |
| `TransitionDuration` | `float` | Radio Button Style | yes | read / write |  |
| `DotChecked` | `FColor` | Radio Button Style | yes | read / write |  |
| `DotUnchecked` | `FColor` | Radio Button Style | yes | read / write |  |
| `CornerRadius` | `float` | Radio Button Style | yes | read / write |  |
| `BoxBrush` | `FDreamUIFaceBrush` | Radio Button Style | yes | read / write |  |
| `DotBrush` | `FDreamUIFaceBrush` | Radio Button Style | yes | read / write |  |
