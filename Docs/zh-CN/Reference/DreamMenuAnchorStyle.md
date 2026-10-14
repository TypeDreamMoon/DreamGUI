---
title: "FDreamMenuAnchorStyle"
description: "A menu anchor: the popup's look, and how it is placed against whatever opened it."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A menu anchor: the popup's look, and how it is placed against whatever opened it.

Where UMG's MenuAnchor keeps Placement as a widget property, it lives here because it is the same
kind of decision as a corner radius -- a project wants every menu to open the same way -- and
because the control still exposes it per instance through the style's override bits.

The placement ENUM is not declared here: it is EDreamMenuPlacement, which belongs to
UDreamLayoutContainerMenuAnchor along with the arithmetic that reads it. One enum and one set of
placement maths for the layout panel and the control, so a menu opened by either lands in the same
place -- and so there is nothing to keep in step when a placement is added.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Placement` | `EDreamMenuPlacement` | Menu Anchor Style | yes | read / write |  |
| `Offset` | `float` | Menu Anchor Style | yes | read / write |  |
| `Background` | `FColor` | Menu Anchor Style | yes | read / write |  |
| `CornerRadius` | `float` | Menu Anchor Style | yes | read / write |  |
| `BackgroundBrush` | `FDreamUIFaceBrush` | Menu Anchor Style | yes | read / write |  |
| `TransitionDuration` | `float` | Menu Anchor Style | yes | read / write |  |
