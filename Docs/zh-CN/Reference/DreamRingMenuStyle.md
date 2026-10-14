---
title: "FDreamRingMenuStyle"
description: "A ring menu: wedges around a hub, and the geometry that decides where they sit."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A ring menu: wedges around a hub, and the geometry that decides where they sit.

Bigger than the other style structs in this file, and it has to be: a ring menu has no natural
size the way a button has a height. Where the ring begins and ends, how far round it runs, how
wide the gaps are and where the labels ride are all APPEARANCE -- a project themes its wheels
once, and a designer who wants a half-ring on the left edge of the screen gets there without a
subclass. What the menu CONTAINS, and how it answers a pointer, stays on the control.

Every radius is in local units, measured from the ring's centre.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `OuterRadius` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `InnerRadius` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `StartAngle` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `SweepAngle` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `ItemGapAngle` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `HighlightGrowth` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `ContentRadius` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `ContentWidth` | `float` | Ring Menu Style|Geometry | yes | read / write |  |
| `WedgeNormal` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `WedgeHovered` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `WedgePressed` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `WedgeSelected` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `WedgeDisabled` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `WedgeFocused` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `TransitionDuration` | `float` | Ring Menu Style|Colors | yes | read / write |  |
| `BackdropColor` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `HubColor` | `FColor` | Ring Menu Style|Colors | yes | read / write |  |
| `LabelColor` | `FColor` | Ring Menu Style|Text | yes | read / write |  |
| `LabelSelectedColor` | `FColor` | Ring Menu Style|Text | yes | read / write |  |
| `LabelDisabledColor` | `FColor` | Ring Menu Style|Text | yes | read / write |  |
| `FontSize` | `float` | Ring Menu Style|Text | yes | read / write |  |
| `HubTextColor` | `FColor` | Ring Menu Style|Text | yes | read / write |  |
| `HubFontSize` | `float` | Ring Menu Style|Text | yes | read / write |  |
| `IconSize` | `FVector2D` | Ring Menu Style|Text | yes | read / write |  |
| `IconSpacing` | `float` | Ring Menu Style|Text | yes | read / write |  |
| `LabelHeight` | `float` | Ring Menu Style|Text | yes | read / write |  |
| `OpenDuration` | `float` | Ring Menu Style|Open | yes | read / write |  |
| `OpenScaleFrom` | `float` | Ring Menu Style|Open | yes | read / write |  |
