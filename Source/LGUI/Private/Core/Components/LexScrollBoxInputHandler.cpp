// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/LexScrollBoxInputHandler.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Event/LexPointerEventData.h"

bool ULexScrollBoxInputHandler::ApplyScroll(float PrimaryDelta) const
{
	ULexLayoutContainerScrollBox* Layout = TargetLayout.Get();
	return IsValid(Layout) && Layout->ScrollByFromUser(PrimaryDelta);
}

void ULexScrollBoxInputHandler::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (ULexLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
	{
		Layout->TickScrollPhysics(DeltaTime);
	}
}

bool ULexScrollBoxInputHandler::OnPointerBeginDrag_Implementation(ULexPointerEventData* EventData)
{
	if (EventData)
	{
		PrevPointerPosition = EventData->GetWorldPointInPlane();
	}
	DragVelocity = 0.0f;
	// Grabbing the content stops whatever it was doing, including a spring-back in progress, and
	// keeps the physics out of the way until the pointer lets go.
	if (ULexLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
	{
		Layout->SetScrollVelocity(0.0f);
		Layout->SetDragging(true);
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

	// LGUI local space: Y is horizontal, Z is vertical (up). The content follows the pointer, so
	// dragging down moves the content down and reveals what is above -- which means the offset moves
	// WITH the drag, not against it. The signs used to be inverted and the comment above them
	// described the intended behaviour rather than the actual one, so dragging down scrolled up.
	const bool bHorizontal = Layout->Orientation == ELexPanelOrientation::Horizontal;
	const float PrimaryDelta = bHorizontal
		? static_cast<float>(-LocalMoveDelta.Y)
		: static_cast<float>(LocalMoveDelta.Z);

	const UWorld* World = GetWorld();
	const float DeltaTime = FMath::Max(World ? World->GetDeltaSeconds() : 0.0f, KINDA_SMALL_NUMBER);
	DragVelocity = FMath::Lerp(DragVelocity, PrimaryDelta / DeltaTime, DragVelocitySmoothing);

	const bool bWasInRange = FMath::IsNearlyZero(Layout->GetOverscroll());
	Layout->ApplyDragDelta(PrimaryDelta);
	// Bubble up once this box has nothing left to give, so a nested scroll box hands over to its
	// parent. With overscroll on, the rubber band IS somewhere left to give, so the box keeps the
	// event until it is already stretched -- otherwise an outer box would steal the gesture the
	// moment an inner one reached its end.
	const bool bConsumed = !FMath::IsNearlyZero(Layout->GetOverscroll()) || bWasInRange;
	return !bConsumed;
}

bool ULexScrollBoxInputHandler::OnPointerEndDrag_Implementation(ULexPointerEventData* EventData)
{
	if (ULexLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
	{
		// Release the physics first, then hand it the speed: the spring and any momentum only start
		// once the content is actually let go.
		Layout->SetDragging(false);
		if (Layout->bEnableInertia)
		{
			Layout->SetScrollVelocity(DragVelocity);
		}
	}
	DragVelocity = 0.0f;
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
	const float WheelDelta = -Axis * Layout->ScrollSensitivity;
	bool bMoved = false;
	if (Layout->bAnimateWheelScrolling)
	{
		// Accumulate onto wherever the ease is already heading, so spinning the wheel several
		// notches covers several notches instead of restarting the same short glide each time.
		const float Before = Layout->GetAnimatedScrollTarget();
		Layout->SetScrollOffsetAnimated(Before + WheelDelta);
		bMoved = !FMath::IsNearlyEqual(Before, Layout->GetAnimatedScrollTarget());
		if (bMoved)
		{
			Layout->OnUserScrolled.Broadcast(Layout->GetScrollOffset());
		}
	}
	else
	{
		bMoved = ApplyScroll(WheelDelta);
	}
	if (Layout->ConsumeMouseWheel == ELexScrollBoxConsumeMouseWheel::Always)
	{
		return false;//swallow it even at a limit, so an outer box never takes over
	}
	return !bMoved;
}
