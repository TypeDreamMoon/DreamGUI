// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamRingMenuTestTypes.generated.h"

class UDreamWidget;

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

	/**
	 * One wedge was generated. Counted PER INDEX rather than in total, because the claim worth
	 * pinning is "each wedge is announced exactly once per rebuild" -- a total would pass just as
	 * happily for a menu that announced the first wedge twice and the second never.
	 */
	UFUNCTION()
	void RecordWedge(int32 Index, UDreamWidget* Wedge)
	{
		++WedgeCallsByIndex.FindOrAdd(Index);
		++WedgeCalls;
		LastWedge = Wedge;
	}

	int32 WedgeCallsFor(int32 Index) const
	{
		const int32* Found = WedgeCallsByIndex.Find(Index);
		return Found != nullptr ? *Found : 0;
	}

	int32 LastIndex = INDEX_NONE;
	FName LastTag = NAME_None;
	int32 IndexCalls = 0;
	int32 ActivationCalls = 0;
	int32 WedgeCalls = 0;
	TMap<int32, int32> WedgeCallsByIndex;

	/** Reflected, because a probe that keeps a widget alive only by luck is a probe that crashes. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> LastWedge = nullptr;
};
