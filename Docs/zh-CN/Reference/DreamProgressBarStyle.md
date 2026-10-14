---
title: "FDreamProgressBarStyle"
description: "A progress bar: a track and the filled part of it."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A progress bar: a track and the filled part of it.

No handle colours and no pointer states, because there is no behaviour: what fraction is filled
is the control's one property, and the fill's geometry is the control's own to drive.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Height` | `float` | Progress Bar Style | yes | read / write |  |
| `TrackColor` | `FColor` | Progress Bar Style | yes | read / write |  |
| `FillColor` | `FColor` | Progress Bar Style | yes | read / write |  |
| `CornerRadius` | `float` | Progress Bar Style | yes | read / write |  |
| `TrackBrush` | `FDreamUIFaceBrush` | Progress Bar Style | yes | read / write |  |
| `FillBrush` | `FDreamUIFaceBrush` | Progress Bar Style | yes | read / write |  |
| `RadialSize` | `FVector2D` | Progress Bar Style|Radial | yes | read / write |  |
| `RadialThickness` | `float` | Progress Bar Style|Radial | yes | read / write |  |
| `RadialStartAngle` | `float` | Progress Bar Style|Radial | yes | read / write |  |
| `BorderPadding` | `FMargin` | Progress Bar Style | yes | read / write |  |
