// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Event/RaycasterSource/LexUIWorldSpaceRaycasterSource_World.h"
#include "LGUI.h"
#include "GameFramework/Actor.h"

bool ULexUIWorldSpaceRaycasterSource_World::GenerateRay(ULexPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection)
{
	if (auto IterObj = GetRaycasterObject())
	{
		OutRayOrigin = IterObj->GetComponentLocation();
		switch (RayDirectionType)
		{
		case ELexUISceneComponentDirection::PositiveX:
			OutRayDirection = IterObj->GetForwardVector();
			break;
		case ELexUISceneComponentDirection::NagtiveX:
			OutRayDirection = -IterObj->GetForwardVector();
			break;
		case ELexUISceneComponentDirection::PositiveY:
			OutRayDirection = IterObj->GetRightVector();
			break;
		case ELexUISceneComponentDirection::NagtiveY:
			OutRayDirection = -IterObj->GetRightVector();
			break;
		case ELexUISceneComponentDirection::PositiveZ:
			OutRayDirection = IterObj->GetUpVector();
			break;
		case ELexUISceneComponentDirection::NagtiveZ:
			OutRayDirection = -IterObj->GetUpVector();
			break;
		}
		return true;
	}
	return false;
}
bool ULexUIWorldSpaceRaycasterSource_World::ShouldStartDrag(ULexPointerEventData* InPointerEventData)
{
	if (auto IterObj = GetRaycasterObject())
	{
		auto calculatedThreshold = IterObj->GetClickThresholdSquare();
		if (ClickThresholdRelateToRayDistance)
		{
			calculatedThreshold *= InPointerEventData->pressDistance * RayDistanceMultiply;
		}
		auto dragDistance = (InPointerEventData->GetWorldPointSpherical() - InPointerEventData->pressWorldPoint).Size();
		return dragDistance > calculatedThreshold;
	}
	return false;
}
