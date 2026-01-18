// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/LexWorldSpaceRaycaster.h"

#include "LGUI.h"

ULexWorldSpaceRaycaster::ULexWorldSpaceRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void ULexWorldSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
	if (!RootCanvas.IsValid())
	{
		auto Canvas = GetOwner()->FindComponentByClass<ULexCanvas>();
		if (!IsValid(Canvas) || !Canvas->IsRootCanvas())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d Canvas is not valid! LexWorldSpaceRaycaster can only attach to actor which contains LexCanvas component!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		RootCanvas = Canvas;
	}
}

void ULexWorldSpaceRaycaster::Raycast(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FHitResult>& OutHitResultArray)
{
	if (!RootCanvas.IsValid())return;
	if (RootCanvas->GetTraceChannel() != TraceChannel.GetValue())return;
	return Super::RaycastUI(InPointerEventData, RootCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}
