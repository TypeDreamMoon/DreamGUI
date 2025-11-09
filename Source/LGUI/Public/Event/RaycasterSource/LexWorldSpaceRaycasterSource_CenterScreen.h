// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "LexWorldSpaceRaycasterSource_CenterScreen.generated.h"

/** 
 * Sends trace from the center of the first local player's screen
 */
UCLASS(ClassGroup = LGUI, meta=(BlueprintSpawnableComponent))
class LGUI_API ULexWorldSpaceRaycasterSource_CenterScreen : public ULexWorldSpaceRaycasterSource
{
	GENERATED_BODY()

public:
	virtual bool GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)override;
	virtual bool ShouldStartDrag(ULexPointerEventData* InPointerEventData)override;
};

/*
 * This is a preset actor that contains a LexWorldSpaceRaycasterSource_CenterScreen component
 */
UCLASS(ClassGroup = LGUI)
class LGUI_API ALexWorldSpaceRaycasterSource_CenterScreen_Actor : public ALexWorldSpaceRaycasterSourceActor
{
	GENERATED_BODY()

public:
	ALexWorldSpaceRaycasterSource_CenterScreen_Actor();
};
