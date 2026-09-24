// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/DreamWorldSpaceRaycaster.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIWorldContext.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"

UDreamWorldSpaceRaycaster::UDreamWorldSpaceRaycaster()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	TraceChannel = TraceTypeQuery1;
	DragThresholdSquare = DragThreshold * DragThreshold;
}

void UDreamWorldSpaceRaycaster::BeginPlay()
{
	Super::BeginPlay();
	DragThresholdSquare = DragThreshold * DragThreshold;
}

#if WITH_EDITOR
void UDreamWorldSpaceRaycaster::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Recomputed unconditionally rather than only when DragThreshold is the property that changed:
	// the multiply is free next to everything else an editor property change triggers, and a
	// name-matched branch is one rename away from silently going dead again.
	DragThresholdSquare = DragThreshold * DragThreshold;
}
#endif

bool UDreamWorldSpaceRaycaster::GetAffectByGamePause()const
{
	return GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByGamePause;
}

bool UDreamWorldSpaceRaycaster::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)
{
	OutRayLength = RayLength;
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return false;

	// The pointer says whose it is, and deprojection has to happen through THAT player's view: on a
	// split screen the first controller's viewport is the wrong rectangle and the wrong camera. With
	// no pointer to ask, this raycaster's own player is the answer -- it only ever speaks for one.
	const int32 PointerUserIndex = InPointerEventData != nullptr ? InPointerEventData->UserIndex : GetUserIndex();
	APlayerController* PlayerController = UDreamEventSystem::GetPlayerControllerForUser(this, PointerUserIndex);
	if (PlayerController == nullptr)return false;
	ULocalPlayer* const LocalPlayer = PlayerController->GetLocalPlayer();
	if (LocalPlayer == nullptr || LocalPlayer->ViewportClient == nullptr)return false;

	FSceneViewProjectionData ProjectionData;
	if (!LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))return false;

	FVector2D ScreenPosition = FVector2D::ZeroVector;
	switch (PointerSource)
	{
	case EDreamWorldPointerSource::ScreenCenter:
	{
		FVector2D ViewportSize = FVector2D::ZeroVector;
		LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);
		ScreenPosition = ViewportSize * 0.5f;
	}
	break;
	case EDreamWorldPointerSource::Mouse:
	default:
		if (InPointerEventData == nullptr)return false;//a mouse ray is a position, and there is none
		ScreenPosition = FVector2D(InPointerEventData->PointerPosition);
		break;
	}

	auto ViewProMatrix = ProjectionData.ViewRotationMatrix * ProjectionData.ProjectionMatrix;//ViewProjectionMatrix without position
	FMatrix const InvViewProjMatrix = ViewProMatrix.InverseFast();
	FSceneView::DeprojectScreenToWorld(ScreenPosition, ProjectionData.GetConstrainedViewRect(), InvViewProjMatrix, /*out*/ OutRayOrigin, /*out*/ OutRayDirection);
	OutRayOrigin += ProjectionData.ViewOrigin;//take position out from ViewProjectionMatrix, after de-project calculation, add position to result, this can avoid float precision issue. otherwise result ray will have some obvious bias
	OutRayEnd = OutRayOrigin + OutRayDirection * RayLength;
	return true;
}

void UDreamWorldSpaceRaycaster::Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)
{
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	if (Manager == nullptr)return;

	// Collected per canvas and appended, because RaycastUI both APPENDS to and SORTS the array it is
	// given: handing it the accumulating array would re-sort everything found so far by the hit order
	// of whichever canvas happened to be visited last. Its sort is a within-canvas answer -- canvas
	// sort order, then hierarchy index -- and it has nothing to say about two separate panels, which
	// is what the distance sort below is for.
	TArray<FDreamUIHitResult> CanvasHitResultArray;
	for (const TWeakObjectPtr<UDreamCanvas>& CanvasPtr : Manager->GetAllCanvasArray())
	{
		UDreamCanvas* Canvas = CanvasPtr.Get();
		if (!IsValid(Canvas))continue;
		if (!Canvas->IsRootCanvas())continue;
		if (!Canvas->IsRenderToWorldSpace())continue;
		if (Canvas->GetTraceChannel() != TraceChannel.GetValue())continue;
		CanvasHitResultArray.Reset();
		RaycastUI(InPointerEventData, Canvas, OutRayOrigin, OutRayDirection, OutRayEnd, CanvasHitResultArray);
		OutHitResultArray.Append(CanvasHitResultArray);
	}

	if (bOccludeByWorld)
	{
		// Its own array as well, for a blunter reason: RaycastWorld resets whatever it is handed
		// before tracing, so passing the collected hits in would throw every one of them away.
		TArray<FDreamUIHitResult> WorldHitResultArray;
		RaycastWorld(InPointerEventData, false, TraceChannel, OutRayOrigin, OutRayDirection, OutRayEnd, WorldHitResultArray);
		OutHitResultArray.Append(WorldHitResultArray);
	}

	// Nearest first: the input module reads element 0 as the hit and treats the rest as hover, so
	// this ordering is what decides which of two overlapping panels answers a click, and what lets a
	// world occluder in front of a panel take the pointer away from it.
	//
	// STABLE, and that is not a nicety. Everything on one flat panel is at one distance -- a button
	// face and the panel background it sits on are coplanar, so their distances are the same float --
	// and for those the only right answer is the one RaycastUI already gave: canvas sort order, then
	// hierarchy, i.e. what is drawn on top. TArray::Sort is not stable; on the handful of elements a
	// pointer hits it is a selection sort that moves the first of two equal elements behind the
	// second, so the background came out ahead of the button and took its clicks. A stable sort only
	// moves hits whose distances differ, which is exactly the question it is here for: one panel in
	// front of another, a wall in front of a panel. Equal distances keep the order they were
	// collected in, which also puts a panel's own hits ahead of a world occluder at the same depth --
	// the world trace is appended last.
	if (OutHitResultArray.Num() > 1)
	{
		OutHitResultArray.StableSort([](const FDreamUIHitResult& A, const FDreamUIHitResult& B)
		{
			return A.Distance < B.Distance;
		});
	}
}

bool UDreamWorldSpaceRaycaster::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	if (InPointerEventData == nullptr)return false;
	if (bHoldToDrag)
	{
		// No world means no clock to measure the hold against, so the hold cannot have elapsed and
		// the question falls through to distance -- which needs nothing but the event data. Measured on
		// the pointer clock PressTime was stamped with, not the game clock a pause would stop.
		const UWorld* World = DreamUI::GetWorldSafe(this);
		if (World != nullptr && UDreamEventSystem::GetPointerClockSeconds(World) - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	if (PointerSource == EDreamWorldPointerSource::ScreenCenter)
	{
		// Measured where this pointer actually moves. A centre-screen ray IS the middle of the
		// screen, so its screen position and its press position are the same point forever and no
		// aim, however wide, could ever start a drag. What does move when the player turns is where
		// the ray lands, so the threshold is held against that, in world units.
		const double DragDistanceSquared = (InPointerEventData->GetWorldPointSpherical() - InPointerEventData->PressWorldPoint).SizeSquared();
		return DragDistanceSquared > (double)DragThresholdSquare;
	}
	const FVector2D PointerPos = FVector2D(InPointerEventData->PointerPosition);
	const FVector2D PressPointerPos = FVector2D(InPointerEventData->PressPointerPosition);
	return FVector2D::DistSquared(PressPointerPos, PointerPos) > DragThresholdSquare;
}

bool UDreamWorldSpaceRaycaster::IsWithinDoubleClickDistance(const UDreamPointerEventData* InPointerEventData) const
{
	if (InPointerEventData == nullptr)
	{
		return true;
	}
	// ShouldStartDrag's two measures, applied to the last click's press and this one.
	if (PointerSource == EDreamWorldPointerSource::ScreenCenter)
	{
		const double PressDistanceSquared = (InPointerEventData->PressWorldPoint - InPointerEventData->LastClickPressWorldPoint).SizeSquared();
		return PressDistanceSquared <= (double)DragThresholdSquare;
	}
	const FVector2D LastClickPress = FVector2D(InPointerEventData->LastClickPressPointerPosition);
	const FVector2D ThisPress = FVector2D(InPointerEventData->PressPointerPosition);
	return FVector2D::DistSquared(LastClickPress, ThisPress) <= DragThresholdSquare;
}

void UDreamWorldSpaceRaycaster::SetPointerSource(EDreamWorldPointerSource Value)
{
	PointerSource = Value;
}
void UDreamWorldSpaceRaycaster::SetOccludeByWorld(bool Value)
{
	bOccludeByWorld = Value;
}
void UDreamWorldSpaceRaycaster::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value)
{
	TraceChannel = Value;
}
void UDreamWorldSpaceRaycaster::SetRayLength(float Value)
{
	RayLength = Value;
}
void UDreamWorldSpaceRaycaster::SetDragThreshold(float Value)
{
	DragThreshold = Value;
	DragThresholdSquare = DragThreshold * DragThreshold;
}
void UDreamWorldSpaceRaycaster::SetHoldToDrag(bool Value)
{
	bHoldToDrag = Value;
}
void UDreamWorldSpaceRaycaster::SetHoldToDragTime(float Value)
{
	HoldToDragTime = Value;
}
