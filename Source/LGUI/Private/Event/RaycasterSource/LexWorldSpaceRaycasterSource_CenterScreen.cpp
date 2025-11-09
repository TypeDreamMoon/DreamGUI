// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/RaycasterSource/LexWorldSpaceRaycasterSource_CenterScreen.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "SceneView.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"

bool ULexWorldSpaceRaycasterSource_CenterScreen::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	if (auto playerController = this->GetWorld()->GetFirstPlayerController())
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
bool ULexWorldSpaceRaycasterSource_CenterScreen::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (bHoldToDrag)
	{
		if (GetWorld()->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	FVector2D mousePos = FVector2D(InPointerEventData->PointerPosition);
	FVector2D pressMousePos = FVector2D(InPointerEventData->PressPointerPosition);
	return FVector2D::DistSquared(pressMousePos, mousePos) > this->GetDragThresholdSquare();
}

ALexWorldSpaceRaycasterSource_CenterScreen_Actor::ALexWorldSpaceRaycasterSource_CenterScreen_Actor()
{
	RaycasterSource = CreateDefaultSubobject<ULexWorldSpaceRaycasterSource_CenterScreen>(TEXT("RaycasterSource"));
	RootComponent = RaycasterSource;
}
