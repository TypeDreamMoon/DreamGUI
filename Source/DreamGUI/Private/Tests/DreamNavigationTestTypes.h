// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Interaction/DreamUINavigationScope.h"
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

/** A screen that can be told to intercept Back, and counts how often it was offered it. */
UCLASS()
class UDreamBackHandlingScope : public UDreamUINavigationScope
{
	GENERATED_BODY()

public:
	bool bHandleBack = false;
	int32 BackOfferCount = 0;

protected:
	virtual bool HandleBackAction_Implementation() override
	{
		++BackOfferCount;
		return bHandleBack;
	}
};
