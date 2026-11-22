// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamGUIEngineCompat.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"
#include "UnrealClient.h"

/**
 * Input the tests make up, sent the way each supported engine takes it. From 5.6 on a controller takes an
 * FInputKeyEventArgs and FInputKeyEventArgs::CreateSimulated makes one for input that comes from code; 5.5's controller
 * takes an FInputKeyParams, and its viewport clients take an axis as separate arguments.
 */
namespace DreamDriverSimulatedInput
{
	/** One key event, or one axis sample (IE_Axis), into InController's player input. */
	inline bool SendKey(APlayerController& InController, const FKey& InKey, EInputEvent InEvent, float InAmount
		, int32 InNumSamples = -1, FInputDeviceId InDevice = INPUTDEVICEID_NONE, float InDeltaSeconds = 0.0f)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		FInputKeyEventArgs Args = FInputKeyEventArgs::CreateSimulated(InKey, InEvent, InAmount, InNumSamples, InDevice);
		if (InDeltaSeconds > 0.0f)
		{
			Args.DeltaTime = InDeltaSeconds;
		}
		return InController.InputKey(Args);
#else
		if (InEvent == IE_Axis)
		{
			return InController.InputKey(FInputKeyParams(InKey, (double)InAmount, InDeltaSeconds, InNumSamples < 0 ? 1 : InNumSamples, InKey.IsGamepadKey(), InDevice));
		}
		return InController.InputKey(FInputKeyParams(InKey, InEvent, (double)InAmount, InKey.IsGamepadKey(), InDevice));
#endif
	}

	/** One axis sample into a viewport client, the way the viewport delivers a mouse move. */
	template <typename TViewportClient>
	bool SendViewportAxis(TViewportClient& InClient, FViewport& InViewport, FInputDeviceId InDevice, const FKey& InKey, float InDelta, float InDeltaSeconds)
	{
#if DREAMGUI_ENGINE_AT_LEAST(5, 6)
		return InClient.InputAxis(FInputKeyEventArgs(&InViewport, InDevice, InKey, InDelta, InDeltaSeconds, /*NumSamples*/1, /*Timestamp*/0));
#else
		return InClient.InputAxis(&InViewport, InDevice, InKey, InDelta, InDeltaSeconds, /*NumSamples*/1, /*bGamepad*/false);
#endif
	}
}
