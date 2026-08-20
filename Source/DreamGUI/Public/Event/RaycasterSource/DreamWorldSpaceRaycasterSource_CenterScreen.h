// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "DreamWorldSpaceRaycasterSource_CenterScreen.generated.h"

/** 
 * Sends trace from the center of the first local player's screen
 */
UCLASS(ClassGroup = DreamGUI, meta=(BlueprintSpawnableComponent))
class DREAMGUI_API UDreamWorldSpaceRaycasterSource_CenterScreen : public UDreamWorldSpaceRaycasterSource
{
	GENERATED_BODY()

public:
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
};

/*
 * This is a preset actor that contains a DreamWorldSpaceRaycasterSource_CenterScreen component
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamWorldSpaceRaycasterSource_CenterScreen_Actor : public ADreamWorldSpaceRaycasterSourceActor
{
	GENERATED_BODY()

public:
	ADreamWorldSpaceRaycasterSource_CenterScreen_Actor();
};
