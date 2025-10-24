// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexWorldSpaceRaycaster.h"

ULexWorldSpaceRaycaster::ULexWorldSpaceRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void ULexWorldSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
}

void ULexWorldSpaceRaycaster::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FHitResult>& OutHitResultArray)
{
	return Super::RaycastUI(InPointerEventData, RootCanvas.Get(), TraceChannel.GetValue(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}
