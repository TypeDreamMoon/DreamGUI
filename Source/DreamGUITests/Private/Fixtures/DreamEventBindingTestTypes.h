// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "UObject/Object.h"
#include "DreamEventBindingTestTypes.generated.h"

/**
 * A struct with a heap-owning member, which is the whole reason a struct parameter cannot travel as
 * raw bytes: a byte copy of this is a copy of the FString's pointer, and the callee's destructor
 * would free memory the source still owns. Anything asserting about struct parameters needs one.
 */
USTRUCT()
struct FDreamEventBindingTestPayload
{
	GENERATED_BODY()

	UPROPERTY()
	int32 Count = 0;

	UPROPERTY()
	FString Label;

	UPROPERTY()
	FVector2D Offset = FVector2D::ZeroVector;
};

/**
 * Somewhere a route can land.
 *
 * A `->` route calls a UFUNCTION on the USER WIDGET, and FDreamUIEventDelegate::AddRuntimeRoute takes
 * any UObject, so a plain object is enough to say whether the call arrived and with what.
 */
UCLASS()
class UDreamEventBindingTestHandler : public UObject
{
	GENERATED_BODY()

public:
	int32 EmptyCallCount = 0;
	int32 IntCallCount = 0;
	int32 LastInt = 0;

	UFUNCTION()
	void HandleEmpty() { ++EmptyCallCount; }

	UFUNCTION()
	void HandleInt(int32 InValue) { LastInt = InValue; ++IntCallCount; }
};

/**
 * A behaviour an authored binding can name. TWO of these on one widget is the shape the position key
 * exists for: they share a class, so only their order tells them apart.
 */
UCLASS()
class UDreamEventBindingTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	int32 TouchCount = 0;
	int32 PayloadCallCount = 0;
	FDreamEventBindingTestPayload LastPayload;

	UFUNCTION()
	void Touch() { ++TouchCount; }

	UFUNCTION()
	void TakePayload(FDreamEventBindingTestPayload InPayload) { LastPayload = InPayload; ++PayloadCallCount; }
};
