// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UMG/DreamUMGWidgetInteraction.h"
#include "DreamUMGHostInteractionTestTypes.generated.h"

/**
 * UDreamUMGWidgetInteraction, with what it decides made readable from a test.
 *
 * Everything the interaction SENDS goes to Slate through a virtual user, onto a UMG widget whose hit grid
 * is only filled by drawing it -- which a headless world never does. What it DECIDES is its own: whether
 * it still follows the pointer that hovered it, whether it still holds the cursor its virtual user
 * shares, and whether it is still ticking the moves through. Those are exactly what a drag starting on a
 * hosted widget depends on, and they live in protected state, which is the reason for a subclass rather
 * than reflection.
 */
UCLASS()
class UDreamUMGDragInteractionProbe : public UDreamUMGWidgetInteraction
{
	GENERATED_BODY()

public:
	/** Every tick, which is every frame the interaction forwards the pointer's moves through. */
	int32 TickCount = 0;

	/** Every exit the event system delivered, whether or not the interaction acted on it. */
	int32 ExitCount = 0;

	virtual void Tick(float DeltaTime) override
	{
		++TickCount;
		Super::Tick(DeltaTime);
	}

	/** Whether the interaction has a virtual Slate user to forward through, and a manager arbitrating it. */
	bool IsEnrolled()
	{
		return FindEnrolledInteractions() != nullptr;
	}

	/** Whether the interaction still follows the pointer that hovered it. */
	bool IsFollowingPointer() const
	{
		return CurrentPointerEventData.IsValid();
	}

	/** Whether the cursor its virtual user shares with other surfaces is this interaction's. */
	bool HoldsSharedCursor()
	{
		const UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions();
		return Interactions != nullptr && Interactions->CurrentInteraction.Get() == this;
	}

protected:
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override
	{
		++ExitCount;
		return Super::OnPointerExit_Implementation(EventData);
	}
};
