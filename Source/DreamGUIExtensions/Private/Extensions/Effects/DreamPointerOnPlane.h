// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidget;

namespace DreamPointerOnPlane
{
	/**
	 * Where a user's pointer is on InWidget's plane, in InWidget's own space (Y right, Z up): its raycaster's ray met with the
	 * plane. False where there is no such pointer, no raycaster to make its ray, or the ray runs along the plane or away from it.
	 * What a pointer light and a tilting card follow; neither needs the pointer to be over anything that takes input.
	 */
	bool Find(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId, FVector& OutLocal);
}
