---
title: "FDreamBorderStyle"
description: "A border: a brush, a padding and one child."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A border: a brush, a padding and one child.

UMG's Border is a panel with a look, and this library already offers that shape as a PANEL entry
(an overlay carrying an image). This is the CONTROL spelling of it -- the one that reads the
project sheet, so every bordered box in a project restyles at once -- and it is the reason a
control class exists beside the panel: a panel's brush is authored per instance, and a control's
is authored once for the whole project.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Background` | `FColor` | Border Style | yes | read / write |  |
| `Padding` | `FMargin` | Border Style | yes | read / write |  |
| `CornerRadius` | `float` | Border Style | yes | read / write |  |
| `BorderThickness` | `float` | Border Style | yes | read / write |  |
| `BorderColor` | `FColor` | Border Style | yes | read / write |  |
| `BackgroundBrush` | `FDreamUIFaceBrush` | Border Style | yes | read / write |  |
