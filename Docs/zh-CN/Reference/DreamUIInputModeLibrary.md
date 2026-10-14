---
title: "UDreamUIInputModeLibrary"
description: "Input mode and cursor control, addressed by DreamGUI's user index rather than by a raw controller."
---

**提示：**

本页由 `DreamGUIReferenceDocs` commandlet 从反射生成，正文是头文件注释的原文（英文），所以不翻译——这样它永远和代码一致。概念讲解见[核心概念](../concepts/widgets.md)。



Inherits from `UBlueprintFunctionLibrary`.

Declared in `Public/Event/DreamUIInputModeLibrary.h`.

Input mode and cursor control, addressed by DreamGUI's user index rather than by a raw controller.

Everything here is a thin, honest wrapper over APlayerController: the engine owns input modes and
the cursor, and a UI plugin that tried to own them too would be the thing a project fights with.
What the wrapper adds is the user index -- the same index an event system, a pointer and a
raycaster all speak -- so that a split-screen game does not have to rediscover which controller a
given piece of UI belongs to at every call site.

<a id="functions"></a>

## Functions

| Function | Kind | Description |
|---|---|---|
| `APlayerController* GetPlayerControllerForUser(UObject* WorldContextObject, int32 UserIndex)` | pure | The player controller a DreamGUI user index names. Null when there is no such local player. |
| `bool GetShowMouseCursor(UObject* WorldContextObject, int32 UserIndex)` | pure | Get Show Mouse Cursor |
| `void SetInputModeGameAndUI(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus, int32 UserIndex, EMouseLockMode MouseLockMode, bool bHideCursorDuringCapture, bool bFlushInput)` | callable | Both the game and the UI see input. |
| `void SetInputModeGameOnly(UObject* WorldContextObject, int32 UserIndex, bool bFlushInput)` | callable | Only the game sees input. |
| `void SetInputModeUIOnly(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus, int32 UserIndex, EMouseLockMode MouseLockMode, bool bFlushInput)` | callable | Only the UI sees input, and the cursor is shown -- while the player is on the keyboard and mouse: with UDreamGUISettings::bHideCursorOnGamepad, DreamGUI hides it while they use a pad and shows it again when they go back to the mouse, for as long as this mode holds. |
| `void SetMouseLockMode(UObject* WorldContextObject, EMouseLockMode MouseLockMode, int32 UserIndex)` | callable | Lock the cursor to the viewport (or stop locking it) without changing the input mode. |
| `void SetShowMouseCursor(UObject* WorldContextObject, bool bShowCursor, int32 UserIndex)` | callable | Show or hide the hardware cursor for a player. |
