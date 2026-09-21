// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamScrollBoxInputHandler.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Event/DreamPointerEventData.h"

bool UDreamScrollBoxInputHandler::ApplyScroll(float PrimaryDelta) const
{
	UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get();
	return IsValid(Layout) && Layout->ScrollByFromUser(PrimaryDelta);
}

void UDreamScrollBoxInputHandler::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
	{
		// Only while something is actually moving: a resting scroll box was paying spring and
		// momentum math every frame of its life. The handler keeps ticking, so the frame after ANY
		// path sets the box in motion -- a fling here, an animated SetScrollOffset from code --
		// IsScrolling turns true and the physics resumes; no opt-in bookkeeping to forget.
		if (Layout->IsScrolling())
		{
			Layout->TickScrollPhysics(DeltaTime);
		}
	}
}

bool UDreamScrollBoxInputHandler::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (EventData)
	{
		PrevPointerPosition = EventData->GetWorldPointInPlane();
	}
	DragVelocity = 0.0f;
	// Grabbing the content stops whatever it was doing, including a spring-back in progress, and
	// keeps the physics out of the way until the pointer lets go.
	if (UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
	{
		Layout->SetScrollVelocity(0.0f);
		Layout->SetDragging(true);
	}
	return false;
}

bool UDreamScrollBoxInputHandler::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get();
	if (!EventData || !IsValid(Layout))
	{
		return true;
	}
	const FVector CurrentPointerPosition = EventData->GetWorldPointInPlane();
	const FVector LocalMoveDelta =
		EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
	PrevPointerPosition = CurrentPointerPosition;

	// DreamGUI local space: Y is horizontal, Z is vertical (up). The content follows the pointer, so
	// dragging down moves the content down and reveals what is above -- which means the offset moves
	// WITH the drag, not against it. The signs used to be inverted and the comment above them
	// described the intended behaviour rather than the actual one, so dragging down scrolled up.
	const bool bHorizontal = Layout->Orientation == EDreamPanelOrientation::Horizontal;
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

bool UDreamScrollBoxInputHandler::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	if (UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get(); IsValid(Layout))
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

bool UDreamScrollBoxInputHandler::OnPointerScroll_Implementation(UDreamPointerEventData* EventData)
{
	UDreamLayoutContainerScrollBox* Layout = TargetLayout.Get();
	if (!EventData || !IsValid(Layout) || EventData->ScrollAxisValue.IsZero())
	{
		return true;
	}
	if (Layout->ConsumeMouseWheel == EDreamScrollBoxConsumeMouseWheel::Never)
	{
		return true;//hand the wheel straight on without scrolling
	}
	const bool bHorizontal = Layout->Orientation == EDreamPanelOrientation::Horizontal;
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
	if (Layout->ConsumeMouseWheel == EDreamScrollBoxConsumeMouseWheel::Always)
	{
		return false;//swallow it even at a limit, so an outer box never takes over
	}
	return !bMoved;
}
