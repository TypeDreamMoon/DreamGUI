// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/RaycasterSource/DreamWorldSpaceRaycasterSource_CenterScreen.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Core/DreamUIWorldContext.h"
#include "Event/DreamEventSystem.h"

bool UDreamWorldSpaceRaycasterSource_CenterScreen::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (World == nullptr)return false;
	// The pointer carries the player it belongs to; the first controller is only the same thing when
	// there is one player. A centre-screen ray is aimed by a camera, and on a split screen each player
	// has their own.
	if (auto playerController = UDreamEventSystem::GetPlayerControllerForUser(
		this, InPointerEventData != nullptr ? InPointerEventData->UserIndex : 0))
	{
		ULocalPlayer* const LocalPlayer = playerController->GetLocalPlayer();
		if (LocalPlayer && LocalPlayer->ViewportClient)
		{
			FVector2D ViewportSize;
			LocalPlayer->ViewportClient->GetViewportSize(ViewportSize);
			// get the projection data
			FSceneViewProjectionData ProjectionData;
			if (LocalPlayer->GetProjectionData(LocalPlayer->ViewportClient->Viewport, ProjectionData))
			{
				auto ViewProMatrix = ProjectionData.ViewRotationMatrix * ProjectionData.ProjectionMatrix;//VieProjectionMatrix without position
				FMatrix const InvViewProjMatrix = ViewProMatrix.InverseFast();
				FSceneView::DeprojectScreenToWorld(ViewportSize * 0.5f, ProjectionData.GetConstrainedViewRect(), InvViewProjMatrix, /*out*/ OutRayOrigin, /*out*/ OutRayDirection);
				OutRayOrigin += ProjectionData.ViewOrigin;//take position out from ViewProjectionMatrix, after deproject calculation, add position to result, this can avoid float precition issue. otherwise result ray will have some obvious bias
				OutRayEnd = OutRayOrigin + OutRayDirection * RayLength;
				return true;
			}
		}
	}
	return false;
}
bool UDreamWorldSpaceRaycasterSource_CenterScreen::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (bHoldToDrag && World != nullptr)
	{
		if (World->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	// Measured where this source's pointer actually moves. This was a line-for-line copy of the mouse
	// source, comparing PointerPosition against PressPointerPosition -- but a centre-screen ray IS the
	// middle of the screen, so those two are the same point forever and the distance is always zero. No
	// aim, however wide, could start a drag; only the hold timer above ever could. What does move when
	// the player turns is where the ray lands, so the threshold is held against that, in world units.
	const double DragDistanceSquared = (InPointerEventData->GetWorldPointSpherical() - InPointerEventData->PressWorldPoint).SizeSquared();
	return DragDistanceSquared > (double)this->GetDragThresholdSquare();
}

ADreamWorldSpaceRaycasterSource_CenterScreen_Actor::ADreamWorldSpaceRaycasterSource_CenterScreen_Actor()
{
	RaycasterSource = CreateDefaultSubobject<UDreamWorldSpaceRaycasterSource_CenterScreen>(TEXT("RaycasterSource"));
	RootComponent = RaycasterSource;
}
