// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamRingMenuTestTypes.generated.h"

/**
 * Somewhere a ring menu's events can land.
 *
 * A dynamic delegate can only bind a UFUNCTION on a UObject, so asserting WHAT a control announced
 * needs a real object to hang that function off -- the same reason UDreamDialogResultProbe exists.
 * The counts are here beside the values because "fired once with the right index" and "fired twice,
 * the second time correctly" are different claims, and activation is the event where the difference
 * is the whole point: choosing the same item twice must speak twice.
 */
UCLASS()
class UDreamRingMenuProbe : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void RecordIndex(int32 Index)
	{
		LastIndex = Index;
		++IndexCalls;
	}

	UFUNCTION()
	void RecordActivation(int32 Index, FName Tag)
	{
		LastIndex = Index;
		LastTag = Tag;
		++ActivationCalls;
	}

	int32 LastIndex = INDEX_NONE;
	FName LastTag = NAME_None;
	int32 IndexCalls = 0;
	int32 ActivationCalls = 0;
};
