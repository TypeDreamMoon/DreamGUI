// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIInputModeLibrary.h"

#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "GameFramework/PlayerController.h"

namespace DreamUIInputModeLibraryLocal
{
	/**
	 * Focus a DreamGUI widget as part of an input-mode change.
	 *
	 * UMG's versions take a Slate widget and hand it to the input mode struct, which then tells Slate
	 * to focus it. DreamGUI widgets are not Slate widgets -- the whole plugin draws itself -- so the
	 * equivalent is its own focus call, which is what navigation, selection and text input all read.
	 */
	void FocusWidget(UDreamWidget* InWidgetToFocus, int32 UserIndex)
	{
		if (!IsValid(InWidgetToFocus))return;
		InWidgetToFocus->SetFocus(UserIndex, 0);
	}
}

APlayerController* UDreamUIInputModeLibrary::GetPlayerControllerForUser(UObject* WorldContextObject, int32 UserIndex)
{
	return UDreamEventSystem::GetPlayerControllerForUser(WorldContextObject, UserIndex);
}

void UDreamUIInputModeLibrary::SetInputModeUIOnly(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus, int32 UserIndex, EMouseLockMode MouseLockMode, bool bFlushInput)
{
	APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex);
	if (PlayerController == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No player controller for user %d; the input mode was not changed."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, UserIndex);
		return;
	}
	FInputModeUIOnly InputMode;
	InputMode.SetLockMouseToViewportBehavior(MouseLockMode);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;
	if (bFlushInput)
	{
		PlayerController->FlushPressedKeys();
	}
	DreamUIInputModeLibraryLocal::FocusWidget(InWidgetToFocus, UserIndex);
}

void UDreamUIInputModeLibrary::SetInputModeGameAndUI(UObject* WorldContextObject, UDreamWidget* InWidgetToFocus, int32 UserIndex, EMouseLockMode MouseLockMode, bool bHideCursorDuringCapture, bool bFlushInput)
{
	APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex);
	if (PlayerController == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No player controller for user %d; the input mode was not changed."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, UserIndex);
		return;
	}
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(MouseLockMode);
	InputMode.SetHideCursorDuringCapture(bHideCursorDuringCapture);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;
	if (bFlushInput)
	{
		PlayerController->FlushPressedKeys();
	}
	DreamUIInputModeLibraryLocal::FocusWidget(InWidgetToFocus, UserIndex);
}

void UDreamUIInputModeLibrary::SetInputModeGameOnly(UObject* WorldContextObject, int32 UserIndex, bool bFlushInput)
{
	APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex);
	if (PlayerController == nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d No player controller for user %d; the input mode was not changed."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, UserIndex);
		return;
	}
	PlayerController->SetInputMode(FInputModeGameOnly());
	PlayerController->bShowMouseCursor = false;
	if (bFlushInput)
	{
		PlayerController->FlushPressedKeys();
	}
}

void UDreamUIInputModeLibrary::SetShowMouseCursor(UObject* WorldContextObject, bool bShowCursor, int32 UserIndex)
{
	if (APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex))
	{
		PlayerController->bShowMouseCursor = bShowCursor;
	}
}

bool UDreamUIInputModeLibrary::GetShowMouseCursor(UObject* WorldContextObject, int32 UserIndex)
{
	const APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex);
	return PlayerController != nullptr && PlayerController->bShowMouseCursor;
}

void UDreamUIInputModeLibrary::SetMouseLockMode(UObject* WorldContextObject, EMouseLockMode MouseLockMode, int32 UserIndex)
{
	APlayerController* PlayerController = GetPlayerControllerForUser(WorldContextObject, UserIndex);
	if (PlayerController == nullptr)return;
	// Applied through GameAndUI, which is the only mode where the lock is a separate question at all:
	// GameOnly already captures the cursor and UIOnly is set with its own lock. The engine exposes no
	// way to read back the mode a controller is in, so this states the mode it sets rather than
	// guessing at the one it found -- guessing wrong would silently move input between the two halves.
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(MouseLockMode);
	PlayerController->SetInputMode(InputMode);
}
