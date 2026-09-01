// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Event/DreamScreenSpaceRaycaster.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUI.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamWidgetPresenterComponentBase.h"

#define LOCTEXT_NAMESPACE "DreamGUIScreenSpaceRaycaster"

UDreamScreenSpaceRaycaster::UDreamScreenSpaceRaycaster()
{
	DragThresholdSquare = DragThreshold * DragThreshold;
}

void UDreamScreenSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
	DragThresholdSquare = DragThreshold * DragThreshold;
	if (RootCanvas.IsValid()
		&& (!RootCanvas->IsRootCanvas() || RootCanvas->GetActualRenderMode() != EDreamRenderMode::ScreenSpaceOverlay))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Assigned RootCanvas is not a ScreenSpaceOverlay root canvas."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		RootCanvas = nullptr;
	}
	if (!RootCanvas.IsValid())
	{
		const AActor* Owner = GetOwner();
		auto WidgetPresenter = Owner ? Owner->FindComponentByClass<UDreamWidgetPresenterComponentBase>() : nullptr;
		if (!WidgetPresenter)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d DreamWidgetPresenterComponent is not valid! DreamUIScreenSpaceRaycaster can only attach to a Actor which contains a UDreamWidgetPresenterComponent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		auto Canvas = WidgetPresenter->GetLoadedCanvas();
		if (!IsValid(Canvas) || Canvas->GetActualRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Canvas is not valid! DreamUIScreenSpaceRaycaster can only attach to ScreenSpaceUI!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		RootCanvas = Canvas;
	}
}

#if WITH_EDITOR
void UDreamScreenSpaceRaycaster::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Recomputed unconditionally rather than only when DragThreshold is the property that changed:
	// the multiply is free next to everything else an editor property change triggers, and a
	// name-matched branch is one rename away from silently going dead again.
	DragThresholdSquare = DragThreshold * DragThreshold;
}
#endif

void UDreamScreenSpaceRaycaster::SetRootCanvas(UDreamCanvas* InRootCanvas)
{
	RootCanvas = InRootCanvas;
}

bool UDreamScreenSpaceRaycaster::GetAffectByGamePause()const
{
#if WITH_EDITOR
	if (GetWorld() && GetWorld()->IsEditorWorld())
	{
		return GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByGamePause;
	}
	else
#endif
	{
		static bool Value = GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByGamePause;
		return Value;
	}
}
bool UDreamScreenSpaceRaycaster::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	if (bHoldToDrag)
	{
		// No world means no clock to measure the hold against, so the hold cannot have elapsed and
		// the question falls through to distance -- which needs nothing but the event data. This is
		// the one branch of this function that was not pure arithmetic, and it is the reason a
		// raycaster reached from an authoring tree or a headless fixture used to be a crash rather
		// than an answer.
		const UWorld* World = DreamUI::GetWorldSafe(this);
		if (World != nullptr && World->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	FVector2D mousePos = FVector2D(InPointerEventData->PointerPosition);
	FVector2D pressMousePos = FVector2D(InPointerEventData->PressPointerPosition);
	return FVector2D::DistSquared(pressMousePos, mousePos) > DragThresholdSquare;
}
bool UDreamScreenSpaceRaycaster::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)
{
	if (!RootCanvas.IsValid())
		return false;

	auto ViewProjectionMatrix = RootCanvas->GetViewProjectionMatrix();
	//Get mouse position, convert to range 0-1
	FVector2D mousePos = FVector2D(InPointerEventData->PointerPosition);
	FVector2D viewportSize = RootCanvas->GetViewportSize();
	FVector2D mousePos01 = mousePos / viewportSize;
	mousePos01.Y = 1.0f - mousePos01.Y;

	DeprojectViewPointToWorld(ViewProjectionMatrix, mousePos01, OutRayOrigin, OutRayDirection);
	OutRayEnd = OutRayOrigin + OutRayDirection * RayLength;
	OutRayLength = RayLength;
	return true;
}

void UDreamScreenSpaceRaycaster::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResult)
{
	if (!RootCanvas.IsValid())return;
	Super::RaycastUI(InPointerEventData, RootCanvas.Get(), OutRayOrigin, OutRayDirection, OutRayEnd, OutHitResult);
}

void UDreamScreenSpaceRaycaster::DeprojectViewPointToWorld(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldLocation, FVector& OutWorldDirection)
{
	FMatrix InvViewProjMatrix = InViewProjectionMatrix.InverseFast();

	const float ScreenSpaceX = (InViewPoint01.X - 0.5f) * 2.0f;
	const float ScreenSpaceY = (InViewPoint01.Y - 0.5f) * 2.0f;

	// The start of the raytrace is defined to be at mousex,mousey,1 in projection space (z=1 is near, z=0 is far - this gives us better precision)
	// To get the direction of the raytrace we need to use any z between the near and the far plane, so let's use (mousex, mousey, 0.5)
	const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0.5f, 1.0f);

	// Projection (changing the W coordinate) is not handled by the FMatrix transforms that work with vectors, so multiplications
	// by the projection matrix should use homogeneous coordinates (i.e. FPlane).
	const FVector4 HGRayStartWorldSpace = InvViewProjMatrix.TransformFVector4(RayStartProjectionSpace);
	const FVector4 HGRayEndWorldSpace = InvViewProjMatrix.TransformFVector4(RayEndProjectionSpace);
	FVector RayStartWorldSpace(HGRayStartWorldSpace.X, HGRayStartWorldSpace.Y, HGRayStartWorldSpace.Z);
	FVector RayEndWorldSpace(HGRayEndWorldSpace.X, HGRayEndWorldSpace.Y, HGRayEndWorldSpace.Z);
	// divide vectors by W to undo any projection and get the 3-space coordinate 
	if (HGRayStartWorldSpace.W != 0.0f)
	{
		RayStartWorldSpace /= HGRayStartWorldSpace.W;
	}
	if (HGRayEndWorldSpace.W != 0.0f)
	{
		RayEndWorldSpace /= HGRayEndWorldSpace.W;
	}
	const FVector RayDirWorldSpace = (RayEndWorldSpace - RayStartWorldSpace).GetSafeNormal();

	// Finally, store the results in the outputs
	OutWorldLocation = RayStartWorldSpace;
	OutWorldDirection = RayDirWorldSpace;
}

void UDreamScreenSpaceRaycaster::SetRayLength(float Value)
{
	RayLength = Value;
}
void UDreamScreenSpaceRaycaster::SetDragThreshold(float Value)
{
	DragThreshold = Value;
	DragThresholdSquare = DragThreshold * DragThreshold;
}
void UDreamScreenSpaceRaycaster::SetHoldToDrag(bool Value)
{
	bHoldToDrag = Value;
}
void UDreamScreenSpaceRaycaster::SetHoldToDragTime(float Value)
{
	HoldToDragTime = Value;
}

#undef LOCTEXT_NAMESPACE
