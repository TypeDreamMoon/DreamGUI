---
title: "UDreamLayoutContainerOverlay"
description: "UMG Overlay"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from [UDreamPanelLayoutBase](DreamPanelLayoutBase.md).

Declared in `Public/Core/Components/DreamPanelLayouts.h`.

UMG Overlay

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Padding` | `FMargin` | Overlay | yes | `SetPadding` |  |

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `void SetPadding(FMargin Value)` | callable | Set Padding |

<a id="compared-with-umg"></a>

## Compared with UMG

Every Blueprint-facing member of the UMG class, and where it went. *adopt*: same name, same meaning. *map*: here under another name or on another type. *reject*: deliberately absent, with the reason.

| UMG | Member | Status | Here | Note |
|---|---|---|---|---|
| `UOverlay` | `AddChildToOverlay` | map | `AddChild` | One slot class here means one verb; AddChild returns the slot. |
| `UOverlay` | `ReplaceOverlayChildAt` | map | `ReplaceChildAt` | On the panel base, so every panel has it rather than the two UMG happens to give it to. |
