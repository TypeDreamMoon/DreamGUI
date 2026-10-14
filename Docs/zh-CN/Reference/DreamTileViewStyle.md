---
title: "FDreamTileViewStyle"
description: "A tile view: a list whose rows are a grid of tiles."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Declared in `Public/Controls/DreamControlStyles.h`.

A tile view: a list whose rows are a grid of tiles.

Built the way FDreamTreeViewStyle is -- carrying a whole FDreamListStyle rather than restating its
fields -- because a tile view IS a list that wraps, and a project that styles its lists styles the
tiles' faces, hover and selection with them. What it adds is the one thing a row does not have: a
WIDTH of its own, because a tile is not as wide as the view.

FDreamListStyle::RowHeight stays the tile's height, so there is exactly one place a tile's size is
written down; TileWidth is its partner and lives here.

<a id="properties"></a>

## Properties

| Name | Type | Category | In the details panel | From Blueprint | Description |
|---|---|---|---|---|---|
| `List` | `FDreamListStyle` | Tile View Style | yes | read / write |  |
| `TileWidth` | `float` | Tile View Style | yes | read / write |  |
| `TileSpacing` | `float` | Tile View Style | yes | read / write |  |
