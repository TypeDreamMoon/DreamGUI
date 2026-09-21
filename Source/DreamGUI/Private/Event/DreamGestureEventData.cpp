// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Event/DreamGestureEventData.h"

#include "Core/Components/DreamWidget.h"

FString UDreamGestureEventData::ToString()const
{
	FString Result;
	switch (GestureType)
	{
	case EDreamUIGestureType::Pinch:
		Result += FString::Printf(TEXT("\n		gesture:Pinch, scale:%f, distance:%f, delta:%f"),
			PinchScale, PinchDistance, PinchDistanceDelta);
		break;
	case EDreamUIGestureType::Swipe:
		Result += FString::Printf(TEXT("\n		gesture:Swipe, direction:%d, delta:(%f,%f), duration:%f"),
			(int32)SwipeDirection, SwipeDelta.X, SwipeDelta.Y, SwipeDuration);
		break;
	default:
		Result += TEXT("\n		gesture:None");
		break;
	}
	Result += FString::Printf(TEXT("\n		user:%d, pointers:%d, position:(%f,%f)"),
		UserIndex, PointerIDs.Num(), ScreenPosition.X, ScreenPosition.Y);
	Result += IsValid(Widget)
		? FString::Printf(TEXT("\n		widget:%s"), *(Widget->GetDisplayName()))
		: FString(TEXT("\n		widget is null"));
	return Result;
}
