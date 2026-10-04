// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamUIInputModeLibrary.h"

#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamGameViewportClient.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
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

	/**
	 * Tell the player's viewport client whether DreamGUI's UI-only mode holds. The engine's UI-only mode
	 * makes the client ignore every key, click and touch, and all of DreamGUI's input comes through the
	 * player controller behind it: UDreamGameViewportClient lets that input through while DreamGUI holds
	 * the mode. Any other client leaves DreamGUI deaf in it, which is said once.
	 */
	void SetDreamUIOnly(APlayerController* InPlayerController, bool bInUIOnly)
	{
		const ULocalPlayer* LocalPlayer = InPlayerController != nullptr ? InPlayerController->GetLocalPlayer() : nullptr;
		UGameViewportClient* ViewportClient = LocalPlayer != nullptr ? LocalPlayer->ViewportClient.Get() : nullptr;
		if (ViewportClient == nullptr)
		{
			return;
		}
		if (UDreamGameViewportClient* DreamClient = Cast<UDreamGameViewportClient>(ViewportClient))
		{
			DreamClient->SetDreamUIOnlyInput(InPlayerController, bInUIOnly);
			return;
		}
		static bool bWarnedAboutViewportClient = false;
		if (bInUIOnly && !bWarnedAboutViewportClient)
		{
			bWarnedAboutViewportClient = true;
			UE_LOG(DreamGUI, Warning, TEXT("UI-only input mode: the game viewport client is %s, not UDreamGameViewportClient, so DreamGUI hears no clicks, keys or touches while the mode holds -- the engine's UI-only mode ignores them at the viewport client, and DreamGUI's input arrives behind it. Set [/Script/Engine.Engine] GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient, or derive the project's client from it."),
				*ViewportClient->GetClass()->GetName());
		}
	}

	/**
	 * Whether DreamGUI's UI-only mode, just entered for InPlayerController, starts with the cursor hidden: DreamGUI holds
	 * the mode (its viewport client), the settings hide the cursor on a pad, and the player's latest device is one. The
	 * player's next device change shows or hides it from then on (UDreamUIInputUser::ReportInputDevice).
	 */
	bool StartsWithCursorHidden(const UObject* InWorldContext, const APlayerController* InPlayerController, int32 InUserIndex)
	{
		if (!UDreamGUISettings::Get()->bHideCursorOnGamepad || !UDreamGameViewportClient::IsDreamUIOnlyInputFor(InPlayerController))
		{
			return false;
		}
		const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InWorldContext);
		const UDreamUIInputUser* User = Input != nullptr ? Input->GetUser(InUserIndex) : nullptr;
		return User != nullptr && User->GetCurrentInputDevice() == EDreamUIInputDevice::Gamepad;
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
	DreamUIInputModeLibraryLocal::SetDreamUIOnly(PlayerController, true);
	// DreamGUI shows the cursor from now on: a pad hides it, the keyboard and mouse bring it back.
	PlayerController->bShowMouseCursor = !DreamUIInputModeLibraryLocal::StartsWithCursorHidden(WorldContextObject, PlayerController, UserIndex);
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
	DreamUIInputModeLibraryLocal::SetDreamUIOnly(PlayerController, false);
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
	DreamUIInputModeLibraryLocal::SetDreamUIOnly(PlayerController, false);
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
	DreamUIInputModeLibraryLocal::SetDreamUIOnly(PlayerController, false);
}
