// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineBaseTypes.h"
#include "DreamUIInputModeLibrary.generated.h"

class UDreamWidget;
class APlayerController;

/**
 * Who is listening to the mouse and the keys: the game, the UI, or both.
 *
 * The same three states UMG offers through UWidgetBlueprintLibrary::SetInputMode_*, because a project
 * using DreamGUI needs them for exactly the same reasons -- open a menu and the camera should stop
 * following the mouse; close it and it should start again -- and having to reach for the UMG library
 * to get them is how a project ends up with two half-answers.
 */
UENUM(BlueprintType)
enum class EDreamUIInputMode : uint8
{
	/** Only the game sees input. The cursor is hidden and captured. */
	GameOnly,
	/** Both see it. The usual mode for a HUD with clickable parts. */
	GameAndUI,
	/** Only the UI sees it. The usual mode for a full-screen menu. */
	UIOnly,
};

/**
 * Input mode and cursor control, addressed by DreamGUI's user index rather than by a raw controller.
 *
 * Everything here is a thin, honest wrapper over APlayerController: the engine owns input modes and
 * the cursor, and a UI plugin that tried to own them too would be the thing a project fights with.
 * What the wrapper adds is the user index -- the same index an event system, a pointer and a
 * raycaster all speak -- so that a split-screen game does not have to rediscover which controller a
 * given piece of UI belongs to at every call site.
 */
UCLASS()
class DREAMGUI_API UDreamUIInputModeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Only the UI sees input, and the cursor is shown.
	 * @param InWidgetToFocus	Optional. Focused through UDreamWidget::SetFocus, which is DreamGUI's
	 *							equivalent of the Slate widget UMG's version takes.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "3"))
	static void SetInputModeUIOnly(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus = nullptr, int32 UserIndex = 0, EMouseLockMode MouseLockMode = EMouseLockMode::LockInFullscreen, bool bFlushInput = false);

	/** Both the game and the UI see input. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "3"))
	static void SetInputModeGameAndUI(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus = nullptr, int32 UserIndex = 0, EMouseLockMode MouseLockMode = EMouseLockMode::DoNotLock, bool bHideCursorDuringCapture = true, bool bFlushInput = false);

	/** Only the game sees input. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "2"))
	static void SetInputModeGameOnly(UObject* WorldContextObject, int32 UserIndex = 0, bool bFlushInput = false);

	/** Show or hide the hardware cursor for a player. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject"))
	static void SetShowMouseCursor(UObject* WorldContextObject, bool bShowCursor, int32 UserIndex = 0);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject"))
	static bool GetShowMouseCursor(UObject* WorldContextObject, int32 UserIndex = 0);

	/**
	 * Lock the cursor to the viewport (or stop locking it) without changing the input mode.
	 *
	 * Separate because the two are separate decisions: a game can want the cursor confined while still
	 * routing input to both halves, and re-setting the whole input mode to change only the lock is how
	 * a project accidentally takes focus away from its own menu.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject"))
	static void SetMouseLockMode(UObject* WorldContextObject, EMouseLockMode MouseLockMode, int32 UserIndex = 0);

	/** The player controller a DreamGUI user index names. Null when there is no such local player. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Input", meta = (WorldContext = "WorldContextObject"))
	static APlayerController* GetPlayerControllerForUser(UObject* WorldContextObject, int32 UserIndex = 0);
};
