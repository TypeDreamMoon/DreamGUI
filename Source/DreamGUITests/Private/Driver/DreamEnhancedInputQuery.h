// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamGUIEngineCompat.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"

/**
 * What the tests ask of a player's Enhanced Input mappings, on every supported engine. The mappings array is protected;
 * 5.8 offers a public read-only view of it (GetEnhancedActionMappingsView), and before that the local player's subsystem
 * answers which keys an action is mapped to.
 */
namespace DreamEnhancedInputQuery
{
	/** Whether InPlayerInput's rebuilt mappings map InKey to InAction. */
	inline bool MapsKeyToAction(const UEnhancedPlayerInput& InPlayerInput, const FKey& InKey, const UInputAction* InAction)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		for (const FEnhancedActionKeyMapping& Mapping : InPlayerInput.GetEnhancedActionMappingsView())
		{
			if (Mapping.Key == InKey && Mapping.Action == InAction)
			{
				return true;
			}
		}
		return false;
#else
		const APlayerController* Controller = InPlayerInput.GetOuterAPlayerController();
		const ULocalPlayer* Player = Controller != nullptr ? Controller->GetLocalPlayer() : nullptr;
		const UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player);
		return Subsystem != nullptr && InAction != nullptr && Subsystem->QueryKeysMappedToAction(InAction).Contains(InKey);
#endif
	}

	/** How many mappings InPlayerInput holds; -1 where the engine does not say (before 5.8). */
	inline int32 NumMappings(const UEnhancedPlayerInput& InPlayerInput)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
		return InPlayerInput.GetEnhancedActionMappingsView().Num();
#else
		return -1;
#endif
	}
}
