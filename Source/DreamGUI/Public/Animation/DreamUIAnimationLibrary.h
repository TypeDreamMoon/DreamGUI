// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "DreamUIAnimationLibrary.generated.h"

class UMovieSceneSequence;

/** The Blueprint-side helpers for FDreamUIAnimationHandle, which a struct cannot carry itself. */
UCLASS()
class DREAMGUI_API UDreamUIAnimationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True while the instance the handle names is still live (playing or paused). */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation", meta = (DisplayName = "Is Valid (Animation Handle)", CompactNodeTitle = "Valid?"))
	static bool IsAnimationHandleValid(const FDreamUIAnimationHandle& Handle);

	/** The animation the handle's instance plays, or null once the instance is gone. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation", meta = (DisplayName = "Get Animation (Animation Handle)"))
	static UMovieSceneSequence* GetAnimationFromHandle(const FDreamUIAnimationHandle& Handle);

	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation", meta = (DisplayName = "Equal (Animation Handle)", CompactNodeTitle = "=="))
	static bool EqualAnimationHandles(const FDreamUIAnimationHandle& A, const FDreamUIAnimationHandle& B);
};
