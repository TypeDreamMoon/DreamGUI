// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Event/DreamWorldSpaceRaycaster.h"
#include "DreamWorldRaycastTestTypes.generated.h"

/**
 * A world-space raycaster whose ray is given rather than deprojected.
 *
 * The real GenerateRay needs a player controller, a local player, a viewport and a projection
 * matrix, none of which a headless world has -- so without this the raycaster would refuse to
 * produce a ray and every hit test would come back empty for a reason that has nothing to do with
 * what is being tested. Fixing the ray leaves the parts that ARE under test intact: which canvases
 * Raycast visits, how it filters them, and the order it puts their hits in.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamWorldSpaceRaycasterFixedRay : public UDreamWorldSpaceRaycaster
{
	GENERATED_BODY()
public:
	/** Fired from the world origin down +X, which is where the fixtures put their panels. */
	FVector Origin = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;

	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin,
		FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength) override
	{
		OutRayOrigin = Origin;
		OutRayDirection = Direction.GetSafeNormal();
		OutRayLength = GetRayLength();
		OutRayEnd = OutRayOrigin + OutRayDirection * OutRayLength;
		return true;
	}
};
