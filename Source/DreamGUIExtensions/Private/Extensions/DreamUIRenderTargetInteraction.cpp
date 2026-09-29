// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Extensions/DreamUIRenderTargetInteraction.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIWorldContext.h"
#include "DreamGUI.h"
#include "Extensions/DreamUIRenderTargetGeometrySource.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Engine/World.h"

#define LOCTEXT_NAMESPACE "DreamGUIRenderTargetInteraction"

UDreamUIRenderTargetInteraction::UDreamUIRenderTargetInteraction()
{
	// Nothing to tick: the input pipeline asks this component where a ray lands when one does.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamUIRenderTargetInteraction::ActivateRaycaster()
{
	// Not enrolled with the manager as a raycaster of its own: it is reached through the world raycaster whose ray
	// lands on its actor, as a nested surface.
}
void UDreamUIRenderTargetInteraction::DeactivateRaycaster()
{

}

bool UDreamUIRenderTargetInteraction::ResolveSource()
{
	if (!IsValid(LineTraceSource))
	{
		AActor* Owner = GetOwner();
		LineTraceSource = Owner != nullptr ? Owner->FindComponentByInterface(UDreamUIRenderTargetInteractionSourceInterface::StaticClass()) : nullptr;
		if (!IsValid(LineTraceSource))
		{
			if (!bReportedMissingSource)
			{
				bReportedMissingSource = true;
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d InteractionSource is not valid! DreamGUIRenderTargetInteraction need a valid component which inherit %s on the same actor!")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(UDreamUIRenderTargetInteractionSourceInterface::StaticClass()->GetName()));
			}
			return false;
		}
	}
	if (!TargetCanvas.IsValid())
	{
		TargetCanvas = IDreamUIRenderTargetInteractionSourceInterface::Execute_GetTargetCanvas(LineTraceSource);
		if (!TargetCanvas.IsValid())
		{
			if (!bReportedMissingSource)
			{
				bReportedMissingSource = true;
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d TargetCanvas is not valid! DreamGUIRenderTargetInteraction need to get a vaild DreamGUICanvas from InteractionSource!")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
			return false;
		}
	}
	return true;
}

bool UDreamUIRenderTargetInteraction::ResolveNestedHit(const FDreamUIHitResultContainer& InOuterHit, UDreamPointerEventData* InPointer,
	FDreamUIHitResultContainer& OutInnerHit)
{
	if (InPointer == nullptr || !ResolveSource())
	{
		return false;
	}
	// The WORLD ray, against the surface: where on the texture it lands.
	const FVector WorldRayOrigin = InOuterHit.RayOrigin;
	const FVector WorldRayEnd = InOuterHit.RayOrigin + InOuterHit.RayDirection * RayLength;
	FVector2D HitUV;
	if (!IDreamUIRenderTargetInteractionSourceInterface::Execute_PerformLineTrace(LineTraceSource,
		InOuterHit.HitResult.FaceIndex, InOuterHit.HitResult.Location, WorldRayOrigin, WorldRayEnd, HitUV))
	{
		return false;
	}

	// The CANVAS ray, from the target canvas's own eye through that spot, with its own end: a canvas segment run
	// toward the world ray's end would bend toward the world ray's direction, off by more the further from the middle
	// of the surface the pointer is.
	FVector CanvasRayOrigin, CanvasRayDirection;
	UDreamScreenSpaceRaycaster::DeprojectViewPointToWorld(TargetCanvas->GetViewProjectionMatrix(), HitUV, CanvasRayOrigin, CanvasRayDirection);
	FVector CanvasRayEnd = CanvasRayOrigin + CanvasRayDirection * RayLength;

	TArray<FDreamUIHitResult> HitResultArray;
	this->Raycast(InPointer, CanvasRayOrigin, CanvasRayDirection, CanvasRayEnd, HitResultArray);
	OutInnerHit.Raycaster = this;
	OutInnerHit.RayOrigin = CanvasRayOrigin;
	OutInnerHit.RayDirection = CanvasRayDirection;
	OutInnerHit.RayEnd = CanvasRayEnd;
	// The occlusion rule a raycaster's own hits keep: an uninteractable widget in front hides what is behind it on
	// this canvas, and the surface itself is then what the pointer is over.
	if (HitResultArray.Num() > 0 && HitResultArray[0].Widget.IsValid() && HitResultArray[0].Widget->GetInteractableInHierarchy())
	{
		OutInnerHit.HitResult = HitResultArray[0];
		for (const FDreamUIHitResult& HitItem : HitResultArray)
		{
			if (UDreamWidget* HoveredWidget = HitItem.Widget.Get())
			{
				OutInnerHit.HoverArray.Add(HoveredWidget);
			}
		}
	}
	return true;
}

void UDreamUIRenderTargetInteraction::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	if (!ResolveSource())
	{
		return;
	}
	Super::RaycastUI(InPointerEventData, TargetCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResultArray);
}

// Over a part of the surface with no widget, the actor is what the pointer is over; these only let its events
// bubble, or not, as they always have. The UI on the texture is served by the pipeline itself.
bool UDreamUIRenderTargetInteraction::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}
bool UDreamUIRenderTargetInteraction::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	return bAllowEventBubbleUp;
}

#undef LOCTEXT_NAMESPACE
