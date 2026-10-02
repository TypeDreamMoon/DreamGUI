// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interaction/DreamDragDropOperation.h"
#include "DreamUIDragDropReentryTestTypes.generated.h"

/**
 * A drag-drop broadcast that does something: a test's action, run from inside the handler -- what a drop target's
 * OnDragLeave does when it cancels the drag, or ends another one -- the first time the broadcast arrives.
 *
 * Every drag-drop event is a dynamic multicast, which binds only a UFUNCTION on a UObject; this is that object, with
 * the action a test hands it.
 */
UCLASS()
class UDreamDragDropReentryProbe : public UObject
{
	GENERATED_BODY()

public:
	/** Run from inside the next broadcast, then forgotten. */
	TFunction<void()> Action;
	int32 CallCount = 0;

	UFUNCTION()
	void OnOperation(UDreamDragDropOperation* Operation)
	{
		++CallCount;
		if (Action)
		{
			TFunction<void()> RunOnce = MoveTemp(Action);
			Action = nullptr;
			RunOnce();
		}
	}
};
