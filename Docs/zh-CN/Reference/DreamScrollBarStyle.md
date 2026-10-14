---
title: "FDreamScrollBarStyle"
description: "A scroll box: a clipped viewport, the content that slides inside it, and the bars beside it."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A scroll box: a clipped viewport, the content that slides inside it, and the bars beside it.

The bar's own look is FDreamScrollBarStyle -- the same struct the standalone scroll bar uses, so
a project styles its bars once and both wear it.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Thickness` | `float` | Scroll Bar Style | yes | read / write |  |
| `HandlePadding` | `float` | Scroll Bar Style | yes | read / write |  |
| `BarPadding` | `FMargin` | Scroll Bar Style | yes | read / write |  |
| `TrackColor` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `HandleNormal` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `HandleHovered` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `HandlePressed` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `HandleDisabled` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `HandleFocused` | `FColor` | Scroll Bar Style | yes | read / write |  |
| `TransitionDuration` | `float` | Scroll Bar Style | yes | read / write |  |
| `CornerRadius` | `float` | Scroll Bar Style | yes | read / write |  |
| `TrackBrush` | `FDreamUIFaceBrush` | Scroll Bar Style | yes | read / write |  |
| `HandleBrush` | `FDreamUIFaceBrush` | Scroll Bar Style | yes | read / write |  |
