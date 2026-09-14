// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/DreamWidgetNavigation.h"
#include "Interaction/DreamUIActionBar.h"
#include "Interaction/DreamUINavigationScope.h"
#include "DreamNavigationTestTypes.generated.h"

class UDreamWidget;

/**
 * Somewhere for a Custom navigation rule to ask.
 *
 * FDreamCustomWidgetNavigationDelegate is a dynamic delegate with a return value, so binding one
 * needs a UFUNCTION on a real UObject -- and the direction it was asked about is worth recording,
 * because "the delegate ran" and "the delegate was told which way the player pressed" are different
 * claims and only the second one makes a grid's own neighbour arithmetic possible.
 */
UCLASS()
class UDreamNavigationTargetProvider : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	UDreamWidget* Provide(EDreamUINavigationDirection Direction)
	{
		LastDirection = Direction;
		++CallCount;
		return Target;
	}

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> Target = nullptr;

	EDreamUINavigationDirection LastDirection = EDreamUINavigationDirection::None;
	int32 CallCount = 0;
};

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

/** A prompt bar that counts its rebuilds, so a test can see that it heard about a change at all. */
UCLASS()
class UDreamCountingActionBar : public UDreamUIActionBar
{
	GENERATED_BODY()

public:
	int32 RebuildCount = 0;

	virtual void Rebuild() override
	{
		++RebuildCount;
		Super::Rebuild();
	}

	/** OnEnable is where the bar subscribes, and a bare test world never fires it. */
	void ForceEnable() { OnEnable(); }
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
