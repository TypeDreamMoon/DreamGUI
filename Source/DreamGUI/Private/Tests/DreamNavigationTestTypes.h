// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamNavigationTestTypes.generated.h"

/**
 * Something for an action binding to call. A dynamic delegate can only bind a UFUNCTION on a UObject,
 * so counting how often an action fired needs a real object to hang that function off.
 */
UCLASS()
class UDreamActionCallCounter : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void Fire() { ++CallCount; }

	int32 CallCount = 0;
};
