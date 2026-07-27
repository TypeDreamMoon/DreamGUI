// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexScrollBoxInputHandler.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Event/LexPointerEventData.h"

bool ULexScrollBoxInputHandler::ApplyScroll(float PrimaryDelta) const
{
	ULexLayoutContainerScrollBox* Layout = TargetLayout.Get();
	return IsValid(Layout) && Layout->ScrollByFromUser(PrimaryDelta);
}

bool ULexScrollBoxInputHandler::OnPointerBeginDrag_Implementation(ULexPointerEventData* EventData)
{
	if (EventData)
	{
		PrevPointerPosition = EventData->GetWorldPointInPlane();
	}
	return false;
}

bool ULexScrollBoxInputHandler::OnPointerDrag_Implementation(ULexPointerEventData* EventData)
{
	ULexLayoutContainerScrollBox* Layout = TargetLayout.Get();
	if (!EventData || !IsValid(Layout))
	{
		return true;
	}
	const FVector CurrentPointerPosition = EventData->GetWorldPointInPlane();
	const FVector LocalMoveDelta =
		EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
	PrevPointerPosition = CurrentPointerPosition;

	// LGUI local space: Y is horizontal, Z is vertical. Content should follow the finger, so dragging towards
	// positive axis reduces the offset.
	const bool bHorizontal = Layout->Orientation == ELexPanelOrientation::Horizontal;
	const bool bMoved = ApplyScroll(bHorizontal
		? static_cast<float>(LocalMoveDelta.Y)
		: static_cast<float>(-LocalMoveDelta.Z));
	// Bubble up once this box has nothing left to give, so a nested scroll box hands over to its parent.
	return !bMoved;
}

bool ULexScrollBoxInputHandler::OnPointerEndDrag_Implementation(ULexPointerEventData* EventData)
{
	return false;
}

bool ULexScrollBoxInputHandler::OnPointerScroll_Implementation(ULexPointerEventData* EventData)
{
	ULexLayoutContainerScrollBox* Layout = TargetLayout.Get();
	if (!EventData || !IsValid(Layout) || EventData->ScrollAxisValue.IsZero())
	{
		return true;
	}
	if (Layout->ConsumeMouseWheel == ELexScrollBoxConsumeMouseWheel::Never)
	{
		return true;//hand the wheel straight on without scrolling
	}
	const bool bHorizontal = Layout->Orientation == ELexPanelOrientation::Horizontal;
	const float Axis = static_cast<float>(bHorizontal
		? EventData->ScrollAxisValue.X
		: EventData->ScrollAxisValue.Y);
	const bool bMoved = ApplyScroll(-Axis * Layout->ScrollSensitivity);
	if (Layout->ConsumeMouseWheel == ELexScrollBoxConsumeMouseWheel::Always)
	{
		return false;//swallow it even at a limit, so an outer box never takes over
	}
	return !bMoved;
}
