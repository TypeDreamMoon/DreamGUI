---
title: "UDreamUIAnimationLibrary"
description: "The Blueprint-side helpers for FDreamUIAnimationHandle, which a struct cannot carry itself."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from `UBlueprintFunctionLibrary`.

Declared in `Public/Animation/DreamUIAnimationLibrary.h`.

The Blueprint-side helpers for FDreamUIAnimationHandle, which a struct cannot carry itself.

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `bool EqualAnimationHandles(FDreamUIAnimationHandle A, FDreamUIAnimationHandle B)` | pure | Equal (Animation Handle) |
| `UMovieSceneSequence* GetAnimationFromHandle(FDreamUIAnimationHandle Handle)` | pure | The animation the handle's instance plays, or null once the instance is gone. |
| `bool IsAnimationHandleValid(FDreamUIAnimationHandle Handle)` | pure | True while the instance the handle names is still live (playing or paused). |
