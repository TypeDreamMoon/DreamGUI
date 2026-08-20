// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/DreamWorldSpaceRaycasterForWorldTrigger.h"

UDreamWorldSpaceRaycasterForWorldTrigger::UDreamWorldSpaceRaycasterForWorldTrigger()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamWorldSpaceRaycasterForWorldTrigger::BeginPlay()
{
	Super::BeginPlay();
}

void UDreamWorldSpaceRaycasterForWorldTrigger::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	return Super::RaycastWorld(InPointerEventData, bRequireFaceIndex, TraceChannel, OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}
