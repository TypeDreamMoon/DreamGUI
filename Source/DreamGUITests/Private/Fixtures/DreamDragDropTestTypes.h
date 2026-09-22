// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interaction/DreamDragDropOperation.h"
#include "DreamDragDropTestTypes.generated.h"

/**
 * Somewhere for a drag-drop broadcast to land.
 *
 * Every event in this area is a dynamic multicast, and a dynamic delegate can only bind a UFUNCTION
 * on a UObject -- so counting how often a cancel, an enter or a leave actually fired needs a real
 * object to hang that function off, exactly as UDreamActionCallCounter does for the action router.
 * The count is as much the point as the operation: "cancelled once" and "cancelled twice" are the
 * difference between a latch that works and one that does not.
 */
UCLASS()
class UDreamDragDropCallProbe : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnOperation(UDreamDragDropOperation* Operation)
	{
		LastOperation = Operation;
		++CallCount;
	}

	UPROPERTY(Transient)
	TObjectPtr<UDreamDragDropOperation> LastOperation = nullptr;

	int32 CallCount = 0;
};
