// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamGameViewportClient.h"

#include "Engine/Console.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamUIInputSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/DreamUITextInputTarget.h"

namespace DreamGameViewportClientLocal
{
	/**
	 * One of the base class's input handlers, run as if the client were not ignoring input: the base
	 * class's whole routing -- the console first, the play-in-editor absorption, the player the device
	 * belongs to -- rather than a copy of it that would drift.
	 */
	template <typename RouteType>
	bool RouteAsIfListening(UGameViewportClient& InClient, RouteType&& InRoute)
	{
		InClient.SetIgnoreInput(false);
		const bool bResult = InRoute();
		InClient.SetIgnoreInput(true);
		return bResult;
	}
}

bool UDreamGameViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
	if (IgnoreInput() && bDreamUIOnlyInput)
	{
		return DreamGameViewportClientLocal::RouteAsIfListening(*this, [&]() { return Super::InputKey(EventArgs); });
	}
	return Super::InputKey(EventArgs);
}

bool UDreamGameViewportClient::InputAxis(const FInputKeyEventArgs& EventArgs)
{
	if (IgnoreInput() && bDreamUIOnlyInput)
	{
		return DreamGameViewportClientLocal::RouteAsIfListening(*this, [&]() { return Super::InputAxis(EventArgs); });
	}
	return Super::InputAxis(EventArgs);
}

bool UDreamGameViewportClient::InputTouch(FViewport* const InViewport, const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp)
{
	if (IgnoreInput() && bDreamUIOnlyInput)
	{
		return DreamGameViewportClientLocal::RouteAsIfListening(*this, [&]() { return Super::InputTouch(InViewport, TouchId, Type, TouchLocation, Force, Timestamp); });
	}
	return Super::InputTouch(InViewport, TouchId, Type, TouchLocation, Force, Timestamp);
}

bool UDreamGameViewportClient::HandleNavigation(const uint32 InUserIndex, TSharedPtr<SWidget> InDestination)
{
	// Asked before the base class's OnNavigationOverride, which a project may have rebound since the input subsystem
	// chained its guard there: the guard holds for this client whatever the delegate holds now.
	const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(GetWorld());
	if (Input != nullptr && Input->ShouldSwallowSlateNavigation(InUserIndex))
	{
		return true;
	}
	return Super::HandleNavigation(InUserIndex, InDestination);
}

bool UDreamGameViewportClient::IsDreamUIOnlyInputFor(const APlayerController* InPlayerController)
{
	const ULocalPlayer* LocalPlayer = InPlayerController != nullptr ? InPlayerController->GetLocalPlayer() : nullptr;
	const UDreamGameViewportClient* Client = LocalPlayer != nullptr ? Cast<UDreamGameViewportClient>(LocalPlayer->ViewportClient.Get()) : nullptr;
	return Client != nullptr && Client->IsDreamUIOnlyInput();
}

void UDreamGameViewportClient::SetDreamUIOnlyInput(APlayerController* InPlayerController, bool bInDreamUIOnly)
{
	bDreamUIOnlyInput = bInDreamUIOnly;
	if (bInDreamUIOnly)
	{
		// The player controller hears what DreamGUI hears now, and the pawn on its input stack with it.
		// Movement and look are what a menu must not drive; the controller's ignore counters stack, so
		// this takes one of each and leaving the mode gives back exactly that.
		if (!PlayerHeldStill.IsValid() && IsValid(InPlayerController))
		{
			InPlayerController->SetIgnoreMoveInput(true);
			InPlayerController->SetIgnoreLookInput(true);
			PlayerHeldStill = InPlayerController;
		}
	}
	else if (APlayerController* Held = PlayerHeldStill.Get())
	{
		Held->SetIgnoreMoveInput(false);
		Held->SetIgnoreLookInput(false);
		PlayerHeldStill.Reset();
	}
}

bool UDreamGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	/*
	 * The console, then the DreamGUI field being edited, then the base class -- in that order, rather
	 * than "the base class first and the field with whatever it leaves".
	 *
	 * The base class's answer cannot tell the field whether the console took a character.
	 * UGameViewportClient::InputChar offers the character to the console and, when the console declines
	 * and input is not being ignored, falls back to FGameplayViewportClient::InputChar -- which, in a
	 * play-in-editor viewport, answers true for EVERY character, so that game input is not routed on to
	 * the editor's own frame. Returning on the base's true therefore meant never reaching the field in a
	 * play session: typing there fell back to the field's key table, while a packaged game, where that
	 * answer is false, got real characters.
	 *
	 * So the base's own steps are taken one at a time, with the field between the console and the
	 * catch-all:
	 *  1. The console, asked exactly as the base asks it. An open console takes every character, and a
	 *     closed one takes the character its own toggle key produces (UConsole::InputChar answers
	 *     bCaptureKeyInput then); only its InputChar knows which, so it is asked, not inspected.
	 *  2. Nothing more while the client ignores input: the base stops after the console then, and so does
	 *     UGameViewportClient::InputKey, so a field whose keys are shut off does not type either -- unless
	 *     DreamGUI's own UI-only mode is what shut them off, in which case its keys reach the field too.
	 *  3. The DreamGUI field the typing player -- the player the controller id is -- is editing, and when no
	 *     field takes it, what that player has focused, as a KeyChar. A character either takes has been
	 *     consumed, and a consumed character goes no further -- the editor's frame included -- which is all
	 *     the base's absorption is for. It is a REAL character, resolved by the platform on the player's own
	 *     layout, which is what makes the field stop guessing one from the key code.
	 *  4. Anything nobody took goes to the base class unchanged, absorption and all. That asks the console
	 *     a second time, which is harmless: a console that declined is closed, and a closed console only
	 *     reads bCaptureKeyInput.
	 */
	FString CharacterString;
	CharacterString += Character;
	if (ViewportConsole != nullptr && ViewportConsole->InputChar(FInputDeviceId::CreateFromInternalId(ControllerId), CharacterString))
	{
		return true;
	}
	if ((!IgnoreInput() || bDreamUIOnlyInput) && DreamUITextInputRouter::RouteViewportCharacter(this, ControllerId, Character))
	{
		return true;
	}
	return Super::InputChar(InViewport, ControllerId, Character);
}
