---
title: "FDreamListStyle"
description: "A list: rows built from a source, in a scrolling viewport."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A list: rows built from a source, in a scrolling viewport.

Row colours are the list's, not the row's: a row is whatever the item template makes it, and the
selection and hover states have to read the same across every template a project writes.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Background` | `FColor` | List Style | yes | read / write |  |
| `RowHeight` | `float` | List Style | yes | read / write | A row's extent along the scroll axis (a tile's height in a tile view). At least one unit: a row no taller than nothing is drawn by nothing, and the window arithmetic falls back to a fixed sixteen lines for a value under that which an old asset or SetStyle can still hand it. |
| `RowSpacing` | `float` | List Style | yes | read / write |  |
| `Padding` | `FMargin` | List Style | yes | read / write |  |
| `RowNormal` | `FColor` | List Style | yes | read / write |  |
| `RowHovered` | `FColor` | List Style | yes | read / write |  |
| `RowSelected` | `FColor` | List Style | yes | read / write |  |
| `RowAlternate` | `FColor` | List Style | yes | read / write |  |
| `RowDisabled` | `FColor` | List Style | yes | read / write |  |
| `RowFocused` | `FColor` | List Style | yes | read / write |  |
| `TransitionDuration` | `float` | List Style | yes | read / write |  |
| `TextColor` | `FColor` | List Style | yes | read / write |  |
| `FontSize` | `float` | List Style | yes | read / write |  |
| `CornerRadius` | `float` | List Style | yes | read / write |  |
| `BackgroundBrush` | `FDreamUIFaceBrush` | List Style | yes | read / write |  |
| `RowBrush` | `FDreamUIFaceBrush` | List Style | yes | read / write |  |
| `StateFaces` | `FDreamUIStateFaces` | List Style | yes | read / write |  |
| `Bar` | `FDreamScrollBarStyle` | List Style | yes | read / write |  |
