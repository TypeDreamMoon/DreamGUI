// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "UObject/Object.h"
#include "DreamDragInteractionTestTypes.generated.h"

namespace DreamDragInteraction
{
	/**
	 * Begin play for InWorld's UI manager -- the one step of a game's start the headless rig skips.
	 *
	 * The rig builds a world and never begins play in it, so its UI manager never runs OnWorldBeginPlay,
	 * RegisterDreamWidgetHierarchy (which begins play only once the MANAGER has) never begins play for
	 * what it registers, and no behaviour in any control ever wakes: no Awake, no OnEnable, no Start,
	 * never a tick. Pointer events still reach them, which is why most of an interaction works -- and
	 * why what does not work fails quietly. A scroll bar sized after Initialize (CreateDreamWidget writes
	 * the size in its callback, before the parts' behaviours have subscribed to their widgets) keeps the
	 * handle it drew at the old size, because UUIScrollbar re-places it in OnEnable and Start; a flung
	 * scroll view never coasts, because its inertia is its Tick. A game world has begun play before
	 * anything in it is clicked, so this is the rig catching up with a game, not a test arranging
	 * something a game would not have.
	 *
	 * Called before the controls are made: widgets registered from then on begin play as they register,
	 * which is the order a game gives them. Asked first rather than called twice, because
	 * UWorldSubsystem::OnWorldBeginPlay ensures it runs once.
	 */
	inline bool BeginPlayForUI(UWorld* InWorld)
	{
		UDreamUIManagerWorldSubsystem* Manager = InWorld != nullptr ? UDreamUIManagerWorldSubsystem::GetInstance(InWorld) : nullptr;
		if (Manager == nullptr)
		{
			return false;
		}
		if (!Manager->HasBegunPlay())
		{
			Manager->OnWorldBeginPlay(*InWorld);
		}
		return Manager->HasBegunPlay();
	}
}

/**
 * One event stream of a control, written down as it arrives.
 *
 * The drag, wheel and scrub tests judge a control by what it SAID -- how many value changes a drag
 * produced, what the last one carried, whether a mouse capture began -- and the controls say those
 * things through BlueprintAssignable dynamic delegates. A dynamic delegate can only call a UFUNCTION
 * on a UObject, so this is the smallest thing that can listen: bind one probe per event, then read it.
 *
 * Two entry points cover every event those tests listen to. Each value-carrying event on the
 * controls under test carries exactly one float (a value, a progress, an offset), and each of the
 * others carries nothing (a capture beginning or ending, a click).
 */
UCLASS()
class UDreamDragInteractionProbe : public UObject
{
	GENERATED_BODY()

public:
	/** Every float the bound event carried, in the order it carried them. */
	TArray<float> Floats;

	/** How many times a parameterless event fired. */
	int32 Signals = 0;

	/** For any one-float dynamic delegate. */
	UFUNCTION()
	void RecordFloat(float InValue)
	{
		Floats.Add(InValue);
	}

	/** For any parameterless dynamic delegate. */
	UFUNCTION()
	void RecordSignal()
	{
		++Signals;
	}

	int32 NumFloats() const
	{
		return Floats.Num();
	}

	/** The float that arrived last, or InFallback when none did -- a fallback the caller picks to be wrong. */
	float LastFloat(float InFallback) const
	{
		return Floats.Num() > 0 ? Floats.Last() : InFallback;
	}

	/** Whether every float that arrived lies inside [InLow, InHigh], give or take InTolerance. */
	bool AllFloatsWithin(float InLow, float InHigh, float InTolerance) const
	{
		for (const float Value : Floats)
		{
			if (Value < InLow - InTolerance || Value > InHigh + InTolerance)
			{
				return false;
			}
		}
		return true;
	}
};
