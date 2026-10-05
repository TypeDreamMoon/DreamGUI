// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamRouteOperatorTestTypes.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamRouteOperatorTestSingleCast, int32, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRouteOperatorTestMultiCast, int32, Index);

/**
 * One event of each kind a route operator is held to (EDreamUIRouteOperator), on a behaviour so a `.dui` can reach it
 * with `+ /Script/DreamGUITests.DreamRouteOperatorTestBehaviour { ... }`: none of the shipping widgets has a single-cast
 * delegate, and `=` is the operator that exists for one.
 *
 * Both carry the same signature, so a test that swaps one for the other changes the KIND of event and nothing else.
 */
UCLASS(NotBlueprintable, HideDropdown)
class UDreamRouteOperatorTestBehaviour : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	/** Holds one listener: `OnPicked = Handler` and `OnPicked -> Handler` take it, `OnPicked += Handler` is RouteOperatorMismatch. */
	UPROPERTY()
	FDreamRouteOperatorTestSingleCast OnPicked;

	/** Holds many: `OnChosen += Handler` and `OnChosen -> Handler` take it, `OnChosen = Handler` is RouteOperatorMismatch. */
	UPROPERTY(BlueprintAssignable)
	FDreamRouteOperatorTestMultiCast OnChosen;
};
