---
title: "FDreamRichTextStyle"
description: "A rich text block: one paragraph that reads markup."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A rich text block: one paragraph that reads markup.

Only the text's own look, because that is all a rich text block is -- no face, no padding, no
states. What makes it RICH is the two data assets the markup resolves against, and those are the
control's properties rather than the style's: they are content (which tag means which sprite),
not theme.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `TextColor` | `FColor` | Rich Text Style | yes | read / write |  |
| `FontSize` | `float` | Rich Text Style | yes | read / write |  |
| `Font` | `TObjectPtr<UDreamUIFontData_BaseObject>` | Rich Text Style | yes | read / write |  |
| `TextStyle` | `FDreamTextStyle` | Rich Text Style | yes | read / write |  |
| `OverrideMaterial` | `TObjectPtr<UMaterialInterface>` | Rich Text Style | yes | read / write |  |
