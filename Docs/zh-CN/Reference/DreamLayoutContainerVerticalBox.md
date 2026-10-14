---
title: "UDreamLayoutContainerVerticalBox"
description: "UMG Vertical Box"
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from [UDreamLayoutContainerStackBox](DreamLayoutContainerStackBox.md).

Declared in `Public/Core/Components/DreamPanelLayouts.h`.

UMG Vertical Box

<a id="compared-with-umg"></a>

## Compared with UMG

Every Blueprint-facing member of the UMG class, and where it went. *adopt*: same name, same meaning. *map*: here under another name or on another type. *reject*: deliberately absent, with the reason.

| UMG | Member | Status | Here | Note |
|---|---|---|---|---|
| `UVerticalBox` | `AddChildToVerticalBox` | map | `AddChild` | One slot class here means one verb; AddChild returns the slot. |
