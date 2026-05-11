// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexWorldSpaceRaycasterForWorldTrigger.h"

ULexWorldSpaceRaycasterForWorldTrigger::ULexWorldSpaceRaycasterForWorldTrigger()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void ULexWorldSpaceRaycasterForWorldTrigger::BeginPlay()
{
	Super::BeginPlay();
}

void ULexWorldSpaceRaycasterForWorldTrigger::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FLexUIHitResult>& OutHitResultArray)
{
	return Super::RaycastWorld(InPointerEventData, bRequireFaceIndex, TraceChannel, OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}
