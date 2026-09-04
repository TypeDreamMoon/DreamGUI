// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/RaycasterSource/DreamWorldSpaceRaycasterSource_World.h"
#include "GameFramework/Actor.h"
#include "Core/DreamUIWorldContext.h"

bool UDreamWorldSpaceRaycasterSource_World::GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	OutRayOrigin = this->GetComponentLocation();
	switch (RayDirectionType)
	{
	case EDreamUISceneComponentDirection::PositiveX:
		OutRayDirection = this->GetForwardVector();
		break;
	case EDreamUISceneComponentDirection::NegativeX:
		OutRayDirection = -this->GetForwardVector();
		break;
	case EDreamUISceneComponentDirection::PositiveY:
		OutRayDirection = this->GetRightVector();
		break;
	case EDreamUISceneComponentDirection::NegativeY:
		OutRayDirection = -this->GetRightVector();
		break;
	case EDreamUISceneComponentDirection::PositiveZ:
		OutRayDirection = this->GetUpVector();
		break;
	case EDreamUISceneComponentDirection::NegativeZ:
		OutRayDirection = -this->GetUpVector();
		break;
	}
	OutRayEnd = OutRayOrigin + OutRayDirection * RayLength;
	return true;
}
bool UDreamWorldSpaceRaycasterSource_World::ShouldStartDrag(UDreamPointerEventData* InPointerEventData)
{
	const UWorld* World = DreamUI::GetWorldSafe(this);
	if (bHoldToDrag && World != nullptr)
	{
		if (World->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
		{
			return true;
		}
	}
	auto calculatedThreshold = this->GetDragThresholdSquare();
	if (bDragThresholdRelateToRayDistance)
	{
		calculatedThreshold *= InPointerEventData->PressDistance * RayDistanceMultiply;
	}
	auto dragDistance = (InPointerEventData->GetWorldPointSpherical() - InPointerEventData->PressWorldPoint).Size();
	return dragDistance > calculatedThreshold;
}

ADreamWorldSpaceRaycasterSource_World_Actor::ADreamWorldSpaceRaycasterSource_World_Actor()
{
	RaycasterSource = CreateDefaultSubobject<UDreamWorldSpaceRaycasterSource_World>(TEXT("RaycasterSource"));
	RootComponent = RaycasterSource;
}
