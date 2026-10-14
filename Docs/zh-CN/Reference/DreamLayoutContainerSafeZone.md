---
title: "UDreamLayoutContainerSafeZone"
description: "UMG Safe Zone"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from [UDreamPanelLayoutBase](DreamPanelLayoutBase.md).

Declared in `Public/Core/Components/DreamPanelLayouts.h`.

UMG Safe Zone

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `bUsePlatformSafeZone` | `bool` | SafeZone | yes | `SetUsePlatformSafeZone` |  |
| `bPadLeft` | `bool` | SafeZone | yes | `SetPadLeft` |  |
| `bPadTop` | `bool` | SafeZone | yes | `SetPadTop` |  |
| `bPadRight` | `bool` | SafeZone | yes | `SetPadRight` |  |
| `bPadBottom` | `bool` | SafeZone | yes | `SetPadBottom` |  |
| `SafePadding` | `FMargin` | SafeZone | yes | `SetSafePadding` |  |
| `NormalizedSafePadding` | `FMargin` | SafeZone | yes | `SetNormalizedSafePadding` | Per-side fraction of this widget's size, useful for device profiles and previewing notches. |
| `SafeAreaScale` | `FMargin` | SafeZone | yes | `SetSafeAreaScale` | Per-side multiplier over the padding the PLATFORM reports, matching SSafeZone's SafeAreaScale: one is the whole reported inset, zero ignores that side entirely. SafePadding is added on top of the scaled result and is not touched by it, as in Slate. |

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `void SetNormalizedSafePadding(FMargin Value)` | callable | Set Normalized Safe Padding |
| `void SetPadBottom(bool Value)` | callable | Set Pad Bottom |
| `void SetPadLeft(bool Value)` | callable | Set Pad Left |
| `void SetPadRight(bool Value)` | callable | Set Pad Right |
| `void SetPadTop(bool Value)` | callable | Set Pad Top |
| `void SetSafeAreaScale(FMargin Value)` | callable | Set Safe Area Scale |
| `void SetSafePadding(FMargin Value)` | callable | Set Safe Padding |
| `void SetSidesToPad(bool bInPadLeft, bool bInPadRight, bool bInPadTop, bool bInPadBottom)` | callable | USafeZone::SetSidesToPad: the four side switches in one call, in UMG's argument order (left, right, top, bottom -- not the order an FMargin lists them in). |
| `void SetUsePlatformSafeZone(bool Value)` | callable | Set Use Platform Safe Zone |

<a id="compared-with-umg"></a>

## Compared with UMG

Every Blueprint-facing member of the UMG class, and where it went. *adopt*: same name, same meaning. *map*: here under another name or on another type. *reject*: deliberately absent, with the reason.

| UMG | Member | Status | Here | Note |
|---|---|---|---|---|
| `USafeZone` | `SetSidesToPad` | adopt | `SetSidesToPad` | UMG's argument order, left/right/top/bottom, not the order an FMargin lists them in. |
| `USafeZone` | `PadLeft` | map | `bPadLeft` | Same name with this plugin's boolean prefix. |
| `USafeZone` | `PadRight` | map | `bPadRight` |  |
| `USafeZone` | `PadTop` | map | `bPadTop` |  |
| `USafeZone` | `PadBottom` | map | `bPadBottom` |  |
