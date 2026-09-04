// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/DreamWorldSpaceRaycaster.h"
#include "DreamGUI.h"
#include "Core/DreamWidgetPresenterComponentBase.h"

UDreamWorldSpaceRaycaster::UDreamWorldSpaceRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamWorldSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
	if (!RootCanvas.IsValid())
	{
		auto WidgetPresenter = GetOwner()->FindComponentByClass<UDreamWidgetPresenterComponentBase>();
		if (!WidgetPresenter)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d DreamWidgetPresenterComponent is not valid! DreamUIScreenSpaceRaycaster can only attach to a Actor which contains a DreamWidgetPresenterComponent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;//the very next line dereferences it -- the screen-space raycaster returns here too
		}
		auto Canvas = WidgetPresenter->GetLoadedCanvas();
		if (!IsValid(Canvas) || !Canvas->IsRootCanvas())
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Canvas is not valid! DreamWorldSpaceRaycaster can only attach to actor which contains DreamCanvas component!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		RootCanvas = Canvas;
	}
}

void UDreamWorldSpaceRaycaster::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	if (!RootCanvas.IsValid())return;
	if (RootCanvas->GetTraceChannel() != TraceChannel.GetValue())return;
	return Super::RaycastUI(InPointerEventData, RootCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}
