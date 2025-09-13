// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/LexWorldSpaceRaycaster.h"
#include "LexUIWorldSpaceRaycasterSource_CenterScreen.generated.h"

/** 
 * Sends trace from the center of the first local player's screen
 */
UCLASS(ClassGroup = LGUI, Blueprintable, meta = (DisplayName = "Center Screen"))
class LGUI_API ULexUIWorldSpaceRaycasterSource_CenterScreen : public ULexUIWorldSpaceRaycasterSource
{
	GENERATED_BODY()

public:
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)override;
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData)override;
};
