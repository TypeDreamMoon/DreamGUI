// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/RaycasterSource/LexWorldSpaceRaycasterSource_World.h"
#include "GameFramework/Actor.h"

bool ULexWorldSpaceRaycasterSource_World::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd)
{
	OutRayOrigin = this->GetComponentLocation();
	switch (RayDirectionType)
	{
	case ELexUISceneComponentDirection::PositiveX:
		OutRayDirection = this->GetForwardVector();
		break;
	case ELexUISceneComponentDirection::NegativeX:
		OutRayDirection = -this->GetForwardVector();
		break;
	case ELexUISceneComponentDirection::PositiveY:
		OutRayDirection = this->GetRightVector();
		break;
	case ELexUISceneComponentDirection::NegativeY:
		OutRayDirection = -this->GetRightVector();
		break;
	case ELexUISceneComponentDirection::PositiveZ:
		OutRayDirection = this->GetUpVector();
		break;
	case ELexUISceneComponentDirection::NegativeZ:
		OutRayDirection = -this->GetUpVector();
		break;
	}
	OutRayEnd = OutRayOrigin + OutRayDirection * RayLength;
	return true;
}
bool ULexWorldSpaceRaycasterSource_World::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (bHoldToDrag)
	{
		if (GetWorld()->TimeSeconds - InPointerEventData->PressTime > HoldToDragTime)
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
