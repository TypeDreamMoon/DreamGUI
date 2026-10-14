---
title: "UDreamLayoutContainerUniformGridPanel"
description: "UMG Uniform Grid Panel"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from [UDreamPanelLayoutBase](DreamPanelLayoutBase.md).

Declared in `Public/Core/Components/DreamPanelLayouts.h`.

UMG Uniform Grid Panel

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `Padding` | `FMargin` | UniformGridPanel | yes | `SetPadding` |  |
| `Spacing` | `FVector2D` | UniformGridPanel | yes | `SetSpacing` |  |
| `MinDesiredSlotWidth` | `float` | UniformGridPanel | yes | `SetMinDesiredSlotWidth` |  |
| `MinDesiredSlotHeight` | `float` | UniformGridPanel | yes | `SetMinDesiredSlotHeight` |  |
| `SlotPadding` | `FMargin` | UniformGridPanel | yes | `SetSlotPadding` | UMG's SlotPadding: breathing room INSIDE every cell, around whatever that cell holds, and part of how big a cell has to be. Spacing next to it is the gap BETWEEN cells and no part of a cell -- the two look alike on screen and differ the moment a cell has a background. |

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `void SetMinDesiredSlotHeight(float Value)` | callable | Set Min Desired Slot Height |
| `void SetMinDesiredSlotWidth(float Value)` | callable | Set Min Desired Slot Width |
| `void SetPadding(FMargin Value)` | callable | Set Padding |
| `void SetSlotPadding(FMargin Value)` | callable | Set Slot Padding |
| `void SetSpacing(FVector2D Value)` | callable | Set Spacing |

<a id="compared-with-umg"></a>

## Compared with UMG

Every Blueprint-facing member of the UMG class, and where it went. *adopt*: same name, same meaning. *map*: here under another name or on another type. *reject*: deliberately absent, with the reason.

| UMG | Member | Status | Here | Note |
|---|---|---|---|---|
| `UUniformGridPanel` | `SlotPadding` | adopt | `SlotPadding` | Room inside every cell and part of how big a cell must be; the panel's own Spacing next to it is the gap between cells and no part of one. |
| `UUniformGridPanel` | `MinDesiredSlotWidth` | adopt | `MinDesiredSlotWidth` |  |
| `UUniformGridPanel` | `MinDesiredSlotHeight` | adopt | `MinDesiredSlotHeight` |  |
| `UUniformGridPanel` | `SetSlotPadding` | adopt | `SetSlotPadding` |  |
| `UUniformGridPanel` | `SetMinDesiredSlotWidth` | adopt | `SetMinDesiredSlotWidth` |  |
| `UUniformGridPanel` | `SetMinDesiredSlotHeight` | adopt | `SetMinDesiredSlotHeight` |  |
| `UUniformGridPanel` | `AddChildToUniformGrid` | map | `AddChild` | One slot class here means one verb; the row and column are set on the slot it returns. |
