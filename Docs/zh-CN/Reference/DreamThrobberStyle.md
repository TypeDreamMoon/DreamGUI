---
title: "FDreamThrobberStyle"
description: "A throbber: N identical pieces, animating, saying only \"something is happening\"."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A throbber: N identical pieces, animating, saying only "something is happening".

One control and one style for UMG's two, the call this library already made for the progress bar's
Bar and Radial and the slider's Direction: the pieces, the period and the colours are identical,
and the only thing that differs is where the pieces are put. The palette still offers two rows.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `NumberOfPieces` | `int32` | Throbber Style | yes | read / write |  |
| `PieceSize` | `FVector2D` | Throbber Style | yes | read / write |  |
| `PieceSpacing` | `float` | Throbber Style | yes | read / write |  |
| `Radius` | `float` | Throbber Style | yes | read / write |  |
| `Period` | `float` | Throbber Style | yes | read / write |  |
| `PieceColor` | `FColor` | Throbber Style | yes | read / write |  |
| `MinOpacity` | `float` | Throbber Style | yes | read / write |  |
| `CornerRadius` | `float` | Throbber Style | yes | read / write |  |
| `PieceBrush` | `FDreamUIFaceBrush` | Throbber Style | yes | read / write |  |
