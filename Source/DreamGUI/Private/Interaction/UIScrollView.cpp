// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIScrollView.h"
#include "DreamGUI.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"
// For the touch-scrolling gate: what the player's hands are on is the event system's answer, not
// something the pointer event carries -- touch id zero and mouse id zero are the same number.
#include "Event/DreamEventSystem.h"

namespace DreamScrollViewLocal
{
	/**
	 * Under this many LOCAL UNITS per notch, a wheel cannot visibly move anything, so a value here is
	 * almost certainly a MULTIPLIER left over from what this property used to mean (it shipped at 1,
	 * multiplying the raw axis). Generous on purpose: nobody authors "three units a notch" on
	 * purpose, and the whole point is to tell somebody whose wheel stopped working why.
	 */
	static constexpr float LegacyMultiplierCeiling = 5.0f;
	/** Below this, an offset is at the boundary and a velocity is stopped. Local units, and units/s. */
	static constexpr float SettleThreshold = 0.01f;
	/** How hard the boundary pushes back against a fling that is already past it. Per unit of overshoot. */
	static constexpr float BoundaryForce = 50.0f;
	/** e-folds per second of the pull back into range once the fling has given up. */
	static constexpr float ReturnRate = 10.0f;
	/** Converts DecelerateRate into e-folds per second, so 0.135 keeps the feel it always had. */
	static constexpr float DecelerationScale = 50.0f;

	/** Exponential decay: the frame-rate independent spelling of Lerp(Value, 0, Rate * Dt). */
	static double Decay(double InValue, double InRatePerSecond, double InDeltaTime)
	{
		return InValue * FMath::Exp(-FMath::Max(0.0, InRatePerSecond) * InDeltaTime);
	}
}

void UUIScrollViewHelper::Awake()
{
    Super::Awake();
    this->SetCanExecuteTick(false);
}
void UUIScrollViewHelper::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    if (!TargetComp.IsValid())
    {
        this->DestroyComponent();
    }
    else
    {
        TargetComp->RectRangeChanged();
    }
}
void UUIScrollViewHelper::OnChildDimensionsChanged(UDreamWidget *Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
    if (!TargetComp.IsValid())
    {
        this->DestroyComponent();
    }
    else if (WidthChanged || HeightChanged)
    {
        TargetComp->RectRangeChanged();
    }
}

void UUIScrollView::PostLoad()
{
    Super::PostLoad();
    // Said once, when the asset carrying the number arrives, and never for a value nobody serialized:
    // an asset that left ScrollSensitivity at the OLD default was saved with no delta at all and
    // picks the new default up by itself. What reaches here is a number somebody wrote -- back when
    // writing 1 meant "one times the axis" and now means "one unit a notch".
    if (ScrollSensitivity > 0.0f && ScrollSensitivity < DreamScrollViewLocal::LegacyMultiplierCeiling)
    {
        UE_LOG(DreamGUI, Warning,
            TEXT("[%s].%d '%s' has ScrollSensitivity %.3f. That is LOCAL UNITS travelled per wheel notch now (it used to be a multiplier on the raw axis), so this is a wheel that barely moves. The library default is 40."),
            ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), ScrollSensitivity);
    }
}

void UUIScrollView::Awake()
{
    Super::Awake();
    // A scroll view IS a window onto content larger than itself: content that keeps drawing outside
    // the view is not a scroll view, it is a pile. The framework already draws this conclusion for
    // UDreamLayoutContainerScrollBox (DreamWidget.cpp, where the layout container is registered), but
    // that path only sees layout containers, and a scroll view is a behaviour -- so scrolling here
    // dragged the rows straight out of the box and went on painting them.
    //
    // An override rather than an assignment: GetClipping only consults it while the author left
    // Clipping at Inherit, so anyone who deliberately wrote No Clip still gets no clip.
    if (UDreamWidget* Widget = GetWidget())
    {
        Widget->SetLayoutClippingOverride(EDreamWidgetClipping::ClipToBounds);
    }
    bGestureHorizontal = bGestureVertical = false;
    RectRangeChanged();
    this->SetCanExecuteTick(true);
}

void UUIScrollView::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    if (bCanUpdateAfterDrag)
        UpdateAfterDrag(DeltaTime);
}

void UUIScrollView::OnUnregister()
{
	ReleaseRangeHelper();
	Super::OnUnregister();
}

void UUIScrollView::OnDestroy()
{
	// Removing the behaviour removes what it asserted about its host; the widget outlives it.
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->ClearLayoutClippingOverride();
	}
	ReleaseRangeHelper();
	Super::OnDestroy();
}

#if WITH_EDITOR
void UUIScrollView::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    RectRangeChanged();
    if (auto Property = PropertyChangedEvent.MemberProperty)
    {
        if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UUIScrollView, Progress))
        {
            ApplyContentPositionWithProgress();
        }
    }
}
#endif

/**
 * Both ranges, the two capability bits, and whatever has to move because of them.
 *
 * The capability bits are written on EVERY pass rather than only when scrolling is on, which is the
 * half of this function that used to be missing: they are the answer to "does this view scroll
 * sideways", and a past drag has no business being the last word on it.
 */
void UUIScrollView::RecalculateRange()
{
    if (bRangeCalculated)return;
    if (!CheckParameters())return;

    bRangeCalculated = true;
    bAllowHorizontalScroll = Horizontal;
    bAllowVerticalScroll = Vertical;
    if (Horizontal)
    {
        this->CalculateHorizontalRange();
    }
    if (Vertical)
    {
        this->CalculateVerticalRange();
    }

    if (KeepProgress)
    {
        // The author asked for the progress to be the thing that survives, so the content moves to
        // wherever the new range puts that progress. This is the branch the recycler leans on: its
        // content grows by whole cells and the view must not appear to jump.
        ApplyContentPositionWithProgress();
        return;
    }
    // Otherwise the OFFSET is what survives, and the progress follows from it. A content that grew
    // while the view sat at the top stays at the top; one that shrank out from under the current
    // offset is out of range, and the settle pass walks it back in.
    const FVector2D Position = GetContentPosition();
    const bool bOutOfRange =
        (bAllowHorizontalScroll && (Position.X < HorizontalRange.X - DreamScrollViewLocal::SettleThreshold
            || Position.X > HorizontalRange.Y + DreamScrollViewLocal::SettleThreshold))
        || (bAllowVerticalScroll && (Position.Y < VerticalRange.X - DreamScrollViewLocal::SettleThreshold
            || Position.Y > VerticalRange.Y + DreamScrollViewLocal::SettleThreshold));
    if (bOutOfRange && RestrictRectArea)
    {
        bCanUpdateAfterDrag = true;
    }
    UpdateProgress(false);
}

void UUIScrollView::OnEnable()
{
    Super::OnEnable();
    RectRangeChanged();
}

void UUIScrollView::OnTransformChanged()
{
    Super::OnTransformChanged();
    RectRangeChanged();
}

void UUIScrollView::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    RectRangeChanged();
}

bool UUIScrollView::CheckParameters()
{
    auto Widget = GetWidget();
    if (!Widget)return false;
    if (!Content.IsValid())return false;
	UDreamWidget* CurrentParent = Content->GetParent();
	if (ContentParent.Get() != CurrentParent)
	{
		ReleaseRangeHelper();
		ContentParent = CurrentParent;
		bRangeCalculated = false;
	}
    if (!ContentParent.IsValid())return false;
	if (!RangeHelper.IsValid())
	{
		RangeHelper = ContentParent->AddComponent<UUIScrollViewHelper>();
		if (RangeHelper.IsValid())
		{
			RangeHelper->TargetComp = this;
		}
	}
    return true;
}

bool UUIScrollView::CheckValidHit(UDreamWidget *InHitComp)
{
    auto Widget = GetWidget();
	return IsValid(Widget) && IsValid(InHitComp)
		&& (InHitComp->IsChildOf(Widget) || InHitComp == Widget);
}

FVector2D UUIScrollView::GetContentPosition() const
{
	if (!Content.IsValid())
	{
		return FVector2D::ZeroVector;
	}
	if (CoordinateMode == EDreamScrollCoordinateMode::AnchoredPosition)
	{
		return Content->GetAnchoredPosition();
	}
	const FVector RelativeLocation = Content->GetRelativeLocation();
	return FVector2D(RelativeLocation.Y, RelativeLocation.Z);
}

void UUIScrollView::SetContentPosition(const FVector2D& Value) const
{
	if (!Content.IsValid())
	{
		return;
	}
	if (CoordinateMode == EDreamScrollCoordinateMode::AnchoredPosition)
	{
		Content->SetAnchoredPosition(Value);
		return;
	}
	FVector RelativeLocation = Content->GetRelativeLocation();
	RelativeLocation.Y = Value.X;
	RelativeLocation.Z = Value.Y;
	Content->SetRelativeLocation(RelativeLocation);
}

void UUIScrollView::ApplyContentPosition(const FVector2D& InPosition, bool bInFireEvent)
{
	SetContentPosition(InPosition);
	UpdateProgress(bInFireEvent);
}

/**
 * "How far must the content move for its start edges to meet the viewport's", added to where the
 * content is now.
 *
 * The delta is measured in the parent's own space -- relative location plus the child's local edge,
 * which is the same arithmetic CalculateRevealContentPosition already uses -- and then added to the
 * position in whichever coordinate mode is active. That is legitimate because the two modes differ
 * by a term that depends on the ANCHORS and the parent's size and not on where the content sits: a
 * displacement of D in one is a displacement of D in the other. It is also the entire reason the
 * CoordinateMode correction that used to be bolted onto the range maths is gone.
 */
FVector2D UUIScrollView::GetStartAlignedPosition() const
{
	if (!Content.IsValid() || !ContentParent.IsValid())
	{
		return GetContentPosition();
	}
	const UDreamWidget* Viewport = ContentParent.Get();
	const FVector ContentLocation = Content->GetRelativeLocation();
	// Left edges together on X, TOP edges together on Y: the start of the reading direction, which
	// is where a scroll view rests before anybody has scrolled it.
	const double ContentLeft = ContentLocation.Y + Content->GetLocalSpaceLeft();
	const double ContentTop = ContentLocation.Z + Content->GetLocalSpaceTop();
	FVector2D Delta(
		Viewport->GetLocalSpaceLeft() - ContentLeft,
		Viewport->GetLocalSpaceTop() - ContentTop);

	// An axis whose content is SMALLER than the window has no travel, so where it rests is the whole
	// of its behaviour -- and FlipDirectionInSmallSize is the author's word on which end that is.
	if (FlipDirectionInSmallSize)
	{
		const FVector2D ViewportSize = GetViewportSize();
		const FVector2D ContentSize = GetContentSize();
		if (ContentSize.X < ViewportSize.X)
		{
			Delta.X += ViewportSize.X - ContentSize.X;
		}
		if (ContentSize.Y < ViewportSize.Y)
		{
			Delta.Y -= ViewportSize.Y - ContentSize.Y;
		}
	}
	// The leading pad moves the resting place rather than adding a term to every caller: offset zero
	// is "the content's start edge one window further in", and the sign is the offset's own -- X runs
	// rightward so the content sits further RIGHT, Y runs downward so it sits further DOWN, which is
	// a SMALLER content Y because the content's own Y runs up.
	const FVector2D Leading = GetLeadingScrollPad();
	Delta.X += Leading.X;
	Delta.Y -= Leading.Y;
	return GetContentPosition() + Delta;
}

FVector2D UUIScrollView::GetLeadingScrollPad() const
{
	return bBackPadScrolling ? GetViewportSize() : FVector2D::ZeroVector;
}

FVector2D UUIScrollView::GetTrailingScrollPad() const
{
	return bFrontPadScrolling ? GetViewportSize() : FVector2D::ZeroVector;
}

FVector2D UUIScrollView::GetViewportSize() const
{
	const UDreamWidget* Viewport = ContentParent.Get();
	return Viewport != nullptr ? FVector2D(Viewport->GetWidth(), Viewport->GetHeight()) : FVector2D::ZeroVector;
}

FVector2D UUIScrollView::GetContentSize() const
{
	const UDreamWidget* Widget = Content.Get();
	return Widget != nullptr ? FVector2D(Widget->GetWidth(), Widget->GetHeight()) : FVector2D::ZeroVector;
}

FVector2D UUIScrollView::GetScrollableExtent() const
{
	const FVector2D ViewportSize = GetViewportSize();
	const FVector2D ContentSize = GetContentSize();
	// Both pads are travel that exists over nothing, which is exactly what they are for: the first
	// item can be dragged to the far edge and the last one to the near edge. Slate spends a whole
	// window per pad, not half of one, and this is the same arithmetic in the offset's terms.
	const FVector2D Leading = GetLeadingScrollPad();
	const FVector2D Trailing = GetTrailingScrollPad();
	return FVector2D(
		FMath::Max(0.0, ContentSize.X - ViewportSize.X) + Leading.X + Trailing.X,
		FMath::Max(0.0, ContentSize.Y - ViewportSize.Y) + Leading.Y + Trailing.Y);
}

FVector2D UUIScrollView::GetOverscrollOffset() const
{
	if (!bAllowOverscroll)
	{
		return FVector2D::ZeroVector;
	}
	const FVector2D Offset = GetScrollOffset();
	const FVector2D Extent = GetScrollableExtent();
	// Signed, and in the reading direction: below the start is negative, past the end positive. The
	// same three cases SScrollBox::GetOverscrollOffset answers with, per axis instead of per box.
	auto AxisOverscroll = [](double InOffset, double InExtent)
	{
		if (InOffset < 0.0) return InOffset;
		if (InOffset > InExtent) return InOffset - InExtent;
		return 0.0;
	};
	return FVector2D(AxisOverscroll(Offset.X, Extent.X), AxisOverscroll(Offset.Y, Extent.Y));
}

FVector2D UUIScrollView::GetOverscrollPercentage() const
{
	const FVector2D ViewportSize = GetViewportSize();
	const FVector2D Overscroll = GetOverscrollOffset();
	// A window of zero has no percentage to give rather than an infinite one.
	return FVector2D(
		ViewportSize.X > UE_SMALL_NUMBER ? (Overscroll.X / ViewportSize.X) * 100.0 : 0.0,
		ViewportSize.Y > UE_SMALL_NUMBER ? (Overscroll.Y / ViewportSize.Y) * 100.0 : 0.0);
}

bool UUIScrollView::IsScrolling() const
{
	return bCanUpdateAfterDrag || !Velocity.IsNearlyZero();
}

void UUIScrollView::EndInertialScrolling()
{
	Velocity = FVector2D::ZeroVector;
	// Whether anything is still due to happen is decided by where the content IS, not by the fact
	// that the fling was cancelled: a band left open still has to close.
	bCanUpdateAfterDrag = RestrictRectArea
		&& !ClampToRange(GetContentPosition()).Equals(GetContentPosition(), DreamScrollViewLocal::SettleThreshold);
}

void UUIScrollView::SetWheelScrollMultiplier(float value)
{
	WheelScrollMultiplier = FMath::Max(0.0f, FMath::IsFinite(value) ? value : 1.0f);
}

void UUIScrollView::SetAllowOverscroll(bool value)
{
	if (bAllowOverscroll != value)
	{
		bAllowOverscroll = value;
		// Switching it off leaves whatever band is currently open to be closed, which is the settle
		// pass's job -- so arm it rather than teleporting the content back under the player's finger.
		if (!bAllowOverscroll && RestrictRectArea)
		{
			bCanUpdateAfterDrag = true;
		}
	}
}

void UUIScrollView::SetBackPadScrolling(bool value)
{
	if (bBackPadScrolling != value)
	{
		bBackPadScrolling = value;
		// The pad moves where offset zero is, so every range this view holds is now wrong.
		RectRangeChanged();
	}
}

void UUIScrollView::SetFrontPadScrolling(bool value)
{
	if (bFrontPadScrolling != value)
	{
		bFrontPadScrolling = value;
		RectRangeChanged();
	}
}

FVector2D UUIScrollView::GetScrollOffset() const
{
	// Distance from the resting position, expressed in the reading direction: scrolling right moves
	// the content LEFT (X decreases), scrolling down moves it UP (Y increases). One sign flip on X
	// is the whole of the difference between the two axes.
	const FVector2D Start = GetStartAlignedPosition();
	const FVector2D Position = GetContentPosition();
	return FVector2D(Start.X - Position.X, Position.Y - Start.Y);
}

void UUIScrollView::SetScrollOffset(FVector2D InOffset)
{
	if (!CheckParameters())
	{
		return;
	}
	RecalculateRange();
	const FVector2D Extent = GetScrollableExtent();
	const FVector2D Start = GetStartAlignedPosition();
	FVector2D Position = GetContentPosition();
	if (Horizontal)
	{
		Position.X = Start.X - FMath::Clamp(InOffset.X, 0.0, Extent.X);
	}
	if (Vertical)
	{
		Position.Y = Start.Y + FMath::Clamp(InOffset.Y, 0.0, Extent.Y);
	}
	Velocity = FVector2D::ZeroVector;
	bCanUpdateAfterDrag = false;
	ApplyContentPosition(Position);
}

void UUIScrollView::ScrollBy(FVector2D InDelta)
{
	SetScrollOffset(GetScrollOffset() + InDelta);
}

void UUIScrollView::ScrollToStart()
{
	SetScrollOffset(FVector2D::ZeroVector);
}

void UUIScrollView::ScrollToEnd()
{
	SetScrollOffset(GetScrollableExtent());
}

bool UUIScrollView::CanScrollOnAxis(bool bInHorizontalAxis) const
{
	if (bInHorizontalAxis ? !Horizontal : !Vertical)
	{
		return false;
	}
	const FVector2D Extent = GetScrollableExtent();
	return (bInHorizontalAxis ? Extent.X : Extent.Y) > DreamScrollViewLocal::SettleThreshold;
}

void UUIScrollView::ReleaseRangeHelper()
{
	if (RangeHelper.IsValid())
	{
		RangeHelper->TargetComp.Reset();
		RangeHelper->DestroyComponent();
	}
	RangeHelper.Reset();
}

float UUIScrollView::GetSafeDeltaTime() const
{
	const UWorld* World = GetWorld();
	return FMath::Max(World ? World->GetDeltaSeconds() : 0.0f, UE_SMALL_NUMBER);
}

/**
 * Which axis a gesture drives, decided once from its first movement and then left alone.
 *
 * OnlyOneDirection is about the GESTURE, so a diagonal flick on a two-axis view commits to the axis
 * it started along; without it every axis this view scrolls follows the pointer.
 */
void UUIScrollView::ResolveGestureAxes(const FVector2D& InFirstDelta)
{
	bGestureHorizontal = false;
	bGestureVertical = false;
	if (OnlyOneDirection && Horizontal && Vertical)
	{
		if (FMath::Abs(InFirstDelta.X) > FMath::Abs(InFirstDelta.Y))
		{
			bGestureHorizontal = true;
		}
		else
		{
			bGestureVertical = true;
		}
		return;
	}
	bGestureHorizontal = Horizontal;
	bGestureVertical = Vertical;
}

/**
 * Whether THIS gesture is one this view accepts at all -- the right button and the finger, which are
 * the two UMG lets a project turn off separately.
 *
 * Asked once, at the start of the drag, and not again: a gesture that was refused must stay refused
 * for its whole length, or letting go of the right button mid-drag would hand the rest of the motion
 * to the content. The two gesture bits already carry that decision, so refusing here is enough.
 */
bool UUIScrollView::AcceptsDragGesture(UDreamPointerEventData* InEventData) const
{
    if (InEventData == nullptr)
    {
        return false;
    }
    // The master switch first: it is about whether ANY pointer gesture drives this view, where the
    // two below are about which one.
    if (!bIsPointerScrollingEnabled)
    {
        return false;
    }
    if (!bAllowRightClickDragScrolling && InEventData->MouseButtonType == EDreamUIMouseButtonType::Right)
    {
        return false;
    }
    if (!bEnableTouchScrolling && IsTouchInput(InEventData))
    {
        return false;
    }
    return true;
}

bool UUIScrollView::IsTouchInput(UDreamPointerEventData* InEventData) const
{
    // The DEVICE, not the pointer id: a finger and a mouse both arrive as pointer 0, so the only
    // honest question is what the player last touched -- which is exactly what the event system
    // tracks for the key-prompt tables. One place, because three callers ask it now.
    const UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(
        const_cast<UUIScrollView*>(this), InEventData != nullptr ? InEventData->UserIndex : 0);
    return Events != nullptr && Events->GetCurrentInputDevice() == EDreamUIInputDevice::Touch;
}

bool UUIScrollView::OnPointerBeginDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (EventData && AcceptsDragGesture(EventData) && CheckParameters() && CheckValidHit(EventData->DragWidget))
    {
        PrevPointerPosition = EventData->PressWorldPoint;
        const auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
        const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
        // The RIGHT-button drag is UMG's drag-to-scroll, and there the travel that crossed the drag
        // threshold scrolls too: SScrollBox::OnMouseMove adds every move to AmountScrolledWhileRightMouseDown
        // and, once that passes the trigger distance, scrolls by the move that got it there as well as
        // every one after -- so the content stays under the pointer that grabbed it. Leaving the previous
        // point at the PRESS is what hands that first stretch to the move applied below. The left button
        // and the finger keep dropping it, as they always have: a left drag is this library's own
        // gesture, and SScrollBox's touch path spends the crossing move on capturing, not scrolling.
        if (EventData->MouseButtonType != EDreamUIMouseButtonType::Right)
        {
            PrevPointerPosition = CurrentPointerPosition;
        }
        ResolveGestureAxes(FVector2D(localMoveDelta.Y, localMoveDelta.Z));
        Velocity = FVector2D::ZeroVector;
        bCanUpdateAfterDrag = false;
        // Before the first move is applied, so a consumer hears "began" ahead of the delta that
        // began it rather than after -- the order a handler recording a gesture has to have.
        OnDragGestureCPP.Broadcast(EDreamScrollDragPhase::Begin, IsTouchInput(EventData));
        OnPointerDrag_Implementation(EventData);
    }
    else
    {
        bGestureHorizontal = bGestureVertical = false;
    }
    return AllowEventBubbleUp;
}

bool UUIScrollView::OnPointerDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (!EventData || !Content.IsValid())
        return AllowEventBubbleUp;
	FVector2D Position = GetContentPosition();
    const auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
    const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
    PrevPointerPosition = CurrentPointerPosition;
    if (!bGestureHorizontal && !bGestureVertical)
    {
        return AllowEventBubbleUp;
    }
    OnDragGestureCPP.Broadcast(EDreamScrollDragPhase::Move, IsTouchInput(EventData));
    // The pointer's delta, damped on whichever axis is already past its boundary -- and past it
    // BEFORE this move rather than after, so a drag heading back into range is at full weight the
    // whole way home instead of crawling the last unit.
    const float DragDamper = GetEffectiveOutOfRangeDamper();
    if (bGestureHorizontal)
    {
        Position.X += localMoveDelta.Y * ((RestrictRectArea
            && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y)) ? DragDamper : 1.0f);
    }
    if (bGestureVertical)
    {
        Position.Y += localMoveDelta.Z * ((RestrictRectArea
            && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y)) ? DragDamper : 1.0f);
    }
    if (!CanScrollInSmallSize)
    {
        // The author said content smaller than the window does not move at all, so it does not move
        // at all -- not even out and back. Only the axes that actually fit are pinned.
        const FVector2D Extent = GetScrollableExtent();
        const FVector2D Start = GetStartAlignedPosition();
        if (Extent.X <= DreamScrollViewLocal::SettleThreshold) Position.X = Start.X;
        if (Extent.Y <= DreamScrollViewLocal::SettleThreshold) Position.Y = Start.Y;
    }
    bCanUpdateAfterDrag = false;
    ApplyContentPosition(Position);
    return AllowEventBubbleUp;
}

bool UUIScrollView::OnPointerEndDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (!EventData || !Content.IsValid())
	{
		return AllowEventBubbleUp;
	}
    const auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
    const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
    const float DeltaTime = GetSafeDeltaTime();
    if (bGestureHorizontal)
    {
        bCanUpdateAfterDrag = true;
		Velocity.X = localMoveDelta.Y / DeltaTime;
    }
    if (bGestureVertical)
    {
        bCanUpdateAfterDrag = true;
		Velocity.Y = localMoveDelta.Z / DeltaTime;
    }
    const bool bTouch = IsTouchInput(EventData);
    // A finger let go can EASE to its resting place instead of coasting on the momentum it built --
    // UMG's touch animated scrolling. The glide is the WHEEL's, aimed at where the fling was going
    // to end up, so the two roads share one tween rather than each carrying their own; the velocity
    // is dropped so the physics does not keep pushing the thing the tween is already moving.
    if (bAnimateTouchScrolling && bTouch && !Velocity.IsNearlyZero())
    {
        const FVector2D Projected = GetContentPosition() + Velocity * WheelScrollAnimationDuration;
        Velocity = FVector2D::ZeroVector;
        bCanUpdateAfterDrag = false;
        GlideContentTo(ClampToRange(Projected), true, WheelScrollAnimationDuration);
    }
    OnDragGestureCPP.Broadcast(EDreamScrollDragPhase::End, bTouch);
    return AllowEventBubbleUp;
}

/**
 * The wheel, and the one place this component deliberately declines to consume an event.
 *
 * A view that cannot move on the axis the wheel just turned hands the event ON, which is what makes
 * a list inside a scrolling page behave: reaching the end of the inner list continues the page,
 * rather than the page freezing because something under the pointer is nominally scrollable. UMG's
 * scroll box makes the same call.
 */
bool UUIScrollView::OnPointerScroll_Implementation(UDreamPointerEventData *EventData)
{
    if (!EventData || !CheckParameters() || !CheckValidHit(EventData->EnterWidget))
    {
        return AllowEventBubbleUp;
    }
    if (EventData->ScrollAxisValue.IsZero())
    {
        return AllowEventBubbleUp;
    }
    if (ConsumeMouseWheel == EDreamScrollBoxConsumeMouseWheel::Never || !bIsPointerScrollingEnabled)
    {
        // Not "scroll and pass it on" -- the wheel does not drive this view at all, so the event has
        // to reach whatever is behind it untouched. The master pointer switch answers the same way
        // as Never, because from the wheel's side they are the same refusal.
        return true;
    }
    RecalculateRange();
    ResolveGestureAxes(FVector2D(EventData->ScrollAxisValue.X, EventData->ScrollAxisValue.Y));

    // The wheel reads in the SAME direction on both axes -- a notch away from the user advances the
    // offset -- which is what the offset model buys: no per-axis sign to remember, because the
    // offset itself already runs left-to-right and top-to-bottom.
    const FVector2D Extent = GetScrollableExtent();
    const FVector2D BeforeOffset = GetScrollOffset();
    FVector2D AfterOffset = BeforeOffset;
    // The multiplier scales whichever of the two distances is in force, so "twice as fast" means the
    // same thing whether a notch is measured in local units or in a fraction of the range.
    const float WheelScale = FMath::Max(0.0f, WheelScrollMultiplier);
    if (WheelProgressStep > UE_SMALL_NUMBER)
    {
        const double Step = static_cast<double>(WheelProgressStep) * WheelScale;
        if (bGestureHorizontal) AfterOffset.X -= FMath::Sign(EventData->ScrollAxisValue.X) * Step * Extent.X;
        if (bGestureVertical) AfterOffset.Y -= FMath::Sign(EventData->ScrollAxisValue.Y) * Step * Extent.Y;
    }
    else
    {
        const double Distance = static_cast<double>(ScrollSensitivity) * WheelScale;
        if (bGestureHorizontal) AfterOffset.X -= EventData->ScrollAxisValue.X * Distance;
        if (bGestureVertical) AfterOffset.Y -= EventData->ScrollAxisValue.Y * Distance;
    }
    AfterOffset.X = FMath::Clamp(AfterOffset.X, 0.0, Extent.X);
    AfterOffset.Y = FMath::Clamp(AfterOffset.Y, 0.0, Extent.Y);
    if (AfterOffset.Equals(BeforeOffset, DreamScrollViewLocal::SettleThreshold))
    {
        // Nothing moved: already at that end, or this axis does not scroll. Hand it on -- unless the
        // author said Always, which is the setting that exists precisely so an inner list at its end
        // does not start driving the page behind it. Always answers the way a handled event does,
        // which is AllowEventBubbleUp rather than a bare false.
        return ConsumeMouseWheel == EDreamScrollBoxConsumeMouseWheel::Always ? AllowEventBubbleUp : true;
    }
    if (bAnimateWheelScrolling)
    {
        // UMG's AnimateWheelScrolling: the notch GLIDES rather than teleporting, which is what makes
        // a long list readable while the wheel is turning. The same easing ScrollTo uses, so a wheel
        // notch and a programmatic scroll move the content in exactly one way.
        //
        // Converted to a content POSITION here rather than going through SetScrollOffset, because
        // that setter kills the velocity and applies the position at once -- which is precisely the
        // teleport this branch exists to avoid. The conversion is the setter's own arithmetic: the
        // start-aligned position, less the offset on X and plus it on Y (the offset runs rightward
        // and DOWNWARD, while the content's Y runs up).
        const FVector2D Start = GetStartAlignedPosition();
        FVector2D Target = GetContentPosition();
        if (bGestureHorizontal) Target.X = Start.X - AfterOffset.X;
        if (bGestureVertical)   Target.Y = Start.Y + AfterOffset.Y;
        GlideContentTo(ClampToRange(Target), true, WheelScrollAnimationDuration);
        return AllowEventBubbleUp;
    }
    SetScrollOffset(AfterOffset);
    return AllowEventBubbleUp;
}

void UUIScrollView::SetVelocity(const FVector2D& value)
{
    if (CheckParameters())
    {
        Velocity = value;
		bCanUpdateAfterDrag = !Velocity.IsNearlyZero();
        if (bCanUpdateAfterDrag)
        {
            // A velocity handed in from outside is a fling nobody made a gesture for, so it drives
            // every axis this view scrolls.
            bGestureHorizontal = Horizontal;
            bGestureVertical = Vertical;
        }
    }
}

void UUIScrollView::SetContent(UDreamWidget* Value)
{
	if (Content.Get() != Value)
	{
		ReleaseRangeHelper();
		Content = Value;
		ContentParent = nullptr;
		RectRangeChanged();
	}
}

void UUIScrollView::SetDecelerateRate(float value)
{
    DecelerateRate = FMath::Max(0.0f, value);
}

void UUIScrollView::SetRestrictRectArea(bool value)
{
    if (RestrictRectArea != value)
    {
        RestrictRectArea = value;
        if (RestrictRectArea)
        {
            bCanUpdateAfterDrag = true;
        }
    }
}

void UUIScrollView::SetOutOfRangeDamper(float value)
{
    OutOfRangeDamper = FMath::Clamp(value, 0.0f, 1.0f);
}

void UUIScrollView::SetScrollDelta(FVector2D value)
{
    if (!CheckParameters())
    {
        return;
    }
    RecalculateRange();
    FVector2D Position = GetContentPosition();
    const float DeltaTime = GetSafeDeltaTime();
    if (Horizontal)
    {
        const float Damper = (RestrictRectArea
            && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y)) ? GetEffectiveOutOfRangeDamper() : 1.0f;
        Position.X += value.X * Damper;
        Velocity.X = value.X * Damper / DeltaTime;
        bGestureHorizontal = true;
        bCanUpdateAfterDrag = true;
    }
    if (Vertical)
    {
        const float Damper = (RestrictRectArea
            && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y)) ? GetEffectiveOutOfRangeDamper() : 1.0f;
        Position.Y += value.Y * Damper;
        Velocity.Y = value.Y * Damper / DeltaTime;
        bGestureVertical = true;
        bCanUpdateAfterDrag = true;
    }
    ApplyContentPosition(Position);
}

void UUIScrollView::SetScrollValue(FVector2D value)
{
    if (!CheckParameters())
    {
        return;
    }
    RecalculateRange();
    FVector2D Position = GetContentPosition();
    if (Horizontal)
    {
        Position.X = value.X;
        Velocity.X = 0;
    }
    if (Vertical)
    {
        Position.Y = value.Y;
        Velocity.Y = 0;
    }
    // Whether the settle pass runs is decided by where this PUT the content, not by the fact that
    // somebody called a setter: writing an in-range position used to arm the spring anyway, and the
    // spring then fought every tween that drove this function frame by frame.
    bCanUpdateAfterDrag = RestrictRectArea && !ClampToRange(Position).Equals(Position, DreamScrollViewLocal::SettleThreshold);
    if (bCanUpdateAfterDrag)
    {
        bGestureHorizontal = Horizontal;
        bGestureVertical = Vertical;
    }
    ApplyContentPosition(Position);
}

void UUIScrollView::SetScrollProgress(FVector2D value)
{
    if (!CheckParameters())
    {
        return;
    }
    RecalculateRange();
    Progress.X = FMath::Clamp(value.X, 0.0, 1.0);
    Progress.Y = FMath::Clamp(value.Y, 0.0, 1.0);
    Velocity = FVector2D::ZeroVector;
    bCanUpdateAfterDrag = false;
    ApplyContentPositionWithProgress();
    UpdateProgress();
}

FVector2D UUIScrollView::ClampToRange(const FVector2D& InPosition) const
{
    FVector2D Result = InPosition;
    if (bAllowHorizontalScroll)
    {
        Result.X = FMath::Clamp(Result.X, HorizontalRange.X, HorizontalRange.Y);
    }
    if (bAllowVerticalScroll)
    {
        Result.Y = FMath::Clamp(Result.Y, VerticalRange.X, VerticalRange.Y);
    }
    return Result;
}

void UUIScrollView::GlideContentTo(const FVector2D& InTargetPosition, bool InEaseAnimation, float InAnimationDuration)
{
    if (!InEaseAnimation)
    {
        Velocity = FVector2D::ZeroVector;
        bCanUpdateAfterDrag = false;
        ApplyContentPosition(InTargetPosition);
        return;
    }
    // The tween writes the position and nothing else: no velocity, no spring armed behind it. The
    // old form drove SetScrollValue, which set bCanUpdateAfterDrag on every step, so the settle pass
    // and the tween spent the whole animation writing the same widget.
    Velocity = FVector2D::ZeroVector;
    bCanUpdateAfterDrag = false;
    auto Tweener = UDreamTweenManager::To(this
        , FDreamTweenVector2DGetterFunction::CreateWeakLambda(this, [this]
        {
            return GetContentPosition();
        })
        , FDreamTweenVector2DSetterFunction::CreateWeakLambda(this, [this](FVector2D Value)
        {
            ApplyContentPosition(Value);
        }), InTargetPosition, InAnimationDuration);
    if (Tweener)
    {
        UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
    }
    else
    {
        // No tween manager to glide on: it is a game instance subsystem, and a world no game instance
        // owns -- the designer's preview is one -- has none, so To answers null. The glide's end is
        // where the content was going, so that is where it lands. Returning here instead accepted the
        // scroll and moved nothing: an animated ScrollTo, an animated reveal and every gliding wheel
        // notch silently did nothing at all in such a world.
        ApplyContentPosition(InTargetPosition);
    }
}

void UUIScrollView::ScrollTo(UDreamWidget* InChild, bool InEaseAnimation, float InAnimationDuration)
{
    if (!IsValid(InChild))return;
    if (!CheckParameters())return;
    RecalculateRange();
    const UDreamWidget* Viewport = ContentParent.Get();
    if (!IsValid(Viewport))return;
    const FVector2D ChildCenter = InChild->GetLocalSpaceCenter();
    const FVector ChildCenterWorld = InChild->GetWorldTransform().TransformPosition(FVector(0, ChildCenter.X, ChildCenter.Y));
    const FVector InContent = Content->GetWorldTransform().InverseTransformPosition(ChildCenterWorld);
    // Where the child's centre sits in the VIEWPORT's space right now, and where it should sit. The
    // answer is a DISPLACEMENT, which is what makes it independent of the coordinate mode: the two
    // modes differ by a term that does not depend on where the content is, so a move of D in one is
    // a move of D in the other. The old form computed an absolute relative-location and wrote it
    // through a setter that, in AnchoredPosition mode, meant something else entirely -- and it also
    // assumed the viewport's pivot was centred, which is why GetLocalSpaceCenter appears here.
    const FVector ContentLocation = Content->GetRelativeLocation();
    const FVector2D ChildCenterInViewport(ContentLocation.Y + InContent.Y, ContentLocation.Z + InContent.Z);
    const FVector2D Displacement = Viewport->GetLocalSpaceCenter() - ChildCenterInViewport;
    GlideContentTo(ClampToRange(GetContentPosition() + Displacement), InEaseAnimation, InAnimationDuration);
}

bool UUIScrollView::CalculateRevealContentPosition(UDreamWidget* InChild, FVector2D& OutPosition,
    EDreamUIScrollDestination InDestination, float InPadding)
{
    OutPosition = GetContentPosition();
    if (!IsValid(InChild))return false;
    if (!CheckParameters())return false;
    RecalculateRange();

    auto Viewport = ContentParent.Get();
    if (!IsValid(Viewport))return false;
    if (InChild == Viewport || !InChild->IsChildOf(Viewport))return false;

    // The child's box in the viewport's own space, which is also the space the content position lives
    // in -- Content is a direct child of the viewport, so moving the content by D moves the child by D.
    // All four corners are transformed rather than just two: a rotated item would otherwise report a
    // box narrower than it draws, and the reveal would stop one edge short.
    const FTransform ChildToViewport = InChild->GetWorldTransform() * Viewport->GetWorldTransform().Inverse();
    const FVector2D ChildLeftBottom = InChild->GetLocalSpaceLeftBottomPoint();
    const FVector2D ChildRightTop = InChild->GetLocalSpaceRightTopPoint();
    const FVector2D LocalCorners[4] = {
        ChildLeftBottom,
        FVector2D(ChildRightTop.X, ChildLeftBottom.Y),
        ChildRightTop,
        FVector2D(ChildLeftBottom.X, ChildRightTop.Y),
    };
    float ChildLeft = MAX_flt, ChildRight = -MAX_flt, ChildBottom = MAX_flt, ChildTop = -MAX_flt;
    for (const FVector2D& Corner : LocalCorners)
    {
        const FVector InViewport = ChildToViewport.TransformPosition(FVector(0, Corner.X, Corner.Y));
        ChildLeft = FMath::Min(ChildLeft, (float)InViewport.Y);
        ChildRight = FMath::Max(ChildRight, (float)InViewport.Y);
        ChildBottom = FMath::Min(ChildBottom, (float)InViewport.Z);
        ChildTop = FMath::Max(ChildTop, (float)InViewport.Z);
    }

    const float ViewLeft = Viewport->GetLocalSpaceLeft();
    const float ViewRight = Viewport->GetLocalSpaceRight();
    const float ViewBottom = Viewport->GetLocalSpaceBottom();
    const float ViewTop = Viewport->GetLocalSpaceTop();

    // An item larger than the viewport can never be framed, so show its leading edge -- the same
    // answer the scroll box layout gives, and the only one that does not look like an arbitrary crop.
    // WHERE the item is meant to end up -- UMG's NavigationDestination, and the padding that goes
    // with it. IntoView moves the least distance that reveals it (this view's original answer and
    // still the default); TopOrLeft always parks it against the leading edge, which is what a menu
    // stepping through equal rows wants; Center always frames it, which is what a carousel wants.
    // The padding shrinks the box the item must fit inside, so a row never lands flush against the
    // edge of the window with its neighbour cut in half beside it.
    // A negative padding and the Configured destination both mean "whatever this view was set up
    // with", which is what every caller meant before either could be passed in.
    const float Pad = FMath::Max(0.0f, InPadding < 0.0f ? NavigationScrollPadding : InPadding);
    const EDreamUIScrollDestination Destination = InDestination == EDreamUIScrollDestination::Configured
        ? NavigationDestination : InDestination;
    auto DeltaAlongAxis = [Pad, Destination](float ItemMin, float ItemMax, float ViewMin, float ViewMax,
        bool bLeadingIsMaxEdge)
    {
        const float PaddedMin = ViewMin + Pad;
        const float PaddedMax = ViewMax - Pad;
        if (Destination == EDreamUIScrollDestination::TopOrLeft)
        {
            // Which edge "leading" IS differs by axis, and the flag is the whole of that: X runs
            // right, so a horizontal view leads at its MIN edge; Y runs UP, so a vertical view's top
            // is its MAX edge. Spelled as a parameter rather than guessed from the numbers, because
            // both callers hand their axis in as (min, max) and the pair looks identical here.
            return bLeadingIsMaxEdge ? PaddedMax - ItemMax : PaddedMin - ItemMin;
        }
        if (Destination == EDreamUIScrollDestination::BottomOrRight)
        {
            // The mirror of TopOrLeft, and the flag flips the same way: the trailing edge is the MIN
            // one on Y (a vertical view's bottom) and the MAX one on X.
            return bLeadingIsMaxEdge ? PaddedMin - ItemMin : PaddedMax - ItemMax;
        }
        if (Destination == EDreamUIScrollDestination::Center)
        {
            return ((PaddedMin + PaddedMax) - (ItemMin + ItemMax)) * 0.5f;
        }
        if (ItemMin < PaddedMin)
        {
            return PaddedMin - ItemMin;
        }
        if (ItemMax > PaddedMax)
        {
            return (ItemMax - ItemMin) > (PaddedMax - PaddedMin) ? PaddedMin - ItemMin : PaddedMax - ItemMax;
        }
        return 0.0f;
    };

    FVector2D Position = OutPosition;
    if (Horizontal)
    {
        const float Delta = DeltaAlongAxis(ChildLeft, ChildRight, ViewLeft, ViewRight, /*bLeadingIsMaxEdge*/false);
        Position.X = FMath::Clamp(Position.X + Delta, HorizontalRange.X, HorizontalRange.Y);
    }
    if (Vertical)
    {
        const float Delta = DeltaAlongAxis(ChildBottom, ChildTop, ViewBottom, ViewTop, /*bLeadingIsMaxEdge*/true);
        Position.Y = FMath::Clamp(Position.Y + Delta, VerticalRange.X, VerticalRange.Y);
    }
    if (Position.Equals(OutPosition))
    {
        return false;
    }
    OutPosition = Position;
    return true;
}

bool UUIScrollView::CanScrollWidgetIntoView(UDreamWidget* InChild)
{
    FVector2D Unused;
    return CalculateRevealContentPosition(InChild, Unused);
}

bool UUIScrollView::ScrollWidgetIntoView(UDreamWidget* InChild, bool InEaseAnimation, float InAnimationDuration,
    EDreamUIScrollDestination InDestination, float InPadding)
{
    FVector2D TargetContentPos;
    if (!CalculateRevealContentPosition(InChild, TargetContentPos, InDestination, InPadding))
    {
        return false;
    }
    GlideContentTo(TargetContentPos, InEaseAnimation, InAnimationDuration);
    return true;
}

/**
 * Inertia, and the pull back into range. Both are exponential and both are per-second, so the feel
 * does not change with the frame rate -- the old form multiplied by (rate * deltaTime) directly,
 * which made a fling on a 30fps frame travel further than the same fling on a 120fps one.
 *
 * Per axis, and only on the axes the gesture drove: a vertical flick has no business settling the
 * horizontal one, and the capability bits decide what "in range" even means.
 */
void UUIScrollView::UpdateAfterDrag(float deltaTime)
{
    if (!Content.IsValid() || deltaTime <= 0.0f)
    {
        bCanUpdateAfterDrag = false;
        return;
    }
    FVector2D Position = GetContentPosition();
    const FVector2D Clamped = ClampToRange(Position);

    bool bStillMoving = false;
    auto SettleAxis = [&](double& InOutPosition, double InClamped, double& InOutVelocity, bool bInActive)
    {
        if (!bInActive)
        {
            InOutVelocity = 0.0;
            return;
        }
        const double Overshoot = InOutPosition - InClamped;
        const bool bOutOfRange = RestrictRectArea && FMath::Abs(Overshoot) > DreamScrollViewLocal::SettleThreshold;
        if (bOutOfRange)
        {
            // Past the edge, and still travelling further out: the boundary bleeds the fling off in
            // proportion to how far past it already is, so a hard throw rebounds and a gentle one
            // simply stops.
            if (InOutVelocity != 0.0 && FMath::Sign(InOutVelocity) == FMath::Sign(Overshoot))
            {
                InOutVelocity -= FMath::Sign(InOutVelocity)
                    * FMath::Abs(Overshoot) * DreamScrollViewLocal::BoundaryForce * deltaTime;
                InOutPosition += InOutVelocity * deltaTime;
                bStillMoving = true;
                return;
            }
            // Heading home, or out of momentum: ease onto the boundary and give the velocity up.
            InOutVelocity = 0.0;
            InOutPosition = InClamped + Overshoot * FMath::Exp(-DreamScrollViewLocal::ReturnRate * deltaTime);
            if (FMath::Abs(InOutPosition - InClamped) < DreamScrollViewLocal::SettleThreshold)
            {
                InOutPosition = InClamped;
            }
            else
            {
                bStillMoving = true;
            }
            return;
        }
        if (FMath::Abs(InOutVelocity) <= DreamScrollViewLocal::SettleThreshold)
        {
            InOutVelocity = 0.0;
            return;
        }
        InOutVelocity = DreamScrollViewLocal::Decay(InOutVelocity,
            static_cast<double>(DecelerateRate) * DreamScrollViewLocal::DecelerationScale, deltaTime);
        InOutPosition += InOutVelocity * deltaTime;
        bStillMoving = true;
    };

    SettleAxis(Position.X, Clamped.X, Velocity.X, bGestureHorizontal && bAllowHorizontalScroll);
    SettleAxis(Position.Y, Clamped.Y, Velocity.Y, bGestureVertical && bAllowVerticalScroll);

    if (!Position.Equals(GetContentPosition(), DreamScrollViewLocal::SettleThreshold * 0.1))
    {
        ApplyContentPosition(Position);
    }
    bCanUpdateAfterDrag = bStillMoving;
}

void UUIScrollView::ApplyContentPositionWithProgress()
{
    if (!CheckParameters())
    {
        return;
    }
    // Progress in, position out -- through the offset, so this is the one function that has to
    // agree with GetScrollOffset and it agrees by construction.
    const FVector2D Extent = GetScrollableExtent();
    const FVector2D Start = GetStartAlignedPosition();
    FVector2D Position = GetContentPosition();
    if (Horizontal)
    {
        Progress.X = FMath::Clamp(Progress.X, 0.0, 1.0);
        Position.X = Start.X - Progress.X * Extent.X;
    }
    if (Vertical)
    {
        Progress.Y = FMath::Clamp(Progress.Y, 0.0, 1.0);
        Position.Y = Start.Y + Progress.Y * Extent.Y;
    }
    SetContentPosition(Position);
    bCanUpdateAfterDrag = false;
}

/**
 * Position in, progress out, on every axis this view SCROLLS -- not on the axis some past gesture
 * happened to drive. That distinction is the bug this pair of flags used to carry: after one
 * vertical drag with OnlyOneDirection on, Progress.X stopped being maintained for good, and every
 * horizontal scroll bar attached to this view quietly froze.
 */
void UUIScrollView::UpdateProgress(bool InFireEvent)
{
    if (!Content.IsValid())
        return;
    const FVector2D Extent = GetScrollableExtent();
    const FVector2D Offset = GetScrollOffset();
    if (bAllowHorizontalScroll)
    {
        // An axis with nowhere to go reads zero rather than dividing by it: the bar over a list that
        // fits is at its start, and it covers the whole track.
        Progress.X = Extent.X > DreamScrollViewLocal::SettleThreshold
            ? FMath::Clamp(Offset.X / Extent.X, 0.0, 1.0) : 0.0;
    }
    if (bAllowVerticalScroll)
    {
        Progress.Y = Extent.Y > DreamScrollViewLocal::SettleThreshold
            ? FMath::Clamp(Offset.Y / Extent.Y, 0.0, 1.0) : 0.0;
    }
    if (InFireEvent)
    {
        OnValueChangedCPP.Broadcast(Progress);
        OnValueChangedBP.Broadcast(Progress);
        OnValueChanged.FireEvent(Progress);
    }
}

/**
 * The content-position range on one axis: the resting position, and that position plus the extent.
 *
 * Four pivot-weighted terms and a coordinate correction became two lines because the resting
 * position is now measured instead of derived. Kept as a virtual writing into HorizontalRange
 * because UUIScrollViewWithScrollbar extends it and UUIRecyclableScrollView reads the result.
 */
void UUIScrollView::CalculateHorizontalRange()
{
    const double Start = GetStartAlignedPosition().X;
    // Scrolling right moves the content LEFT, so the far end is the SMALLER coordinate: this pair
    // stays (min, max) the way every reader of it expects.
    HorizontalRange.Y = Start;
    HorizontalRange.X = Start - GetScrollableExtent().X;
}
void UUIScrollView::CalculateVerticalRange()
{
    const double Start = GetStartAlignedPosition().Y;
    // Scrolling down moves the content UP, so the far end is the LARGER coordinate.
    VerticalRange.X = Start;
    VerticalRange.Y = Start + GetScrollableExtent().Y;
}
void UUIScrollView::RectRangeChanged()
{
	bRangeCalculated = false;
	RecalculateRange();
}

void UUIScrollView::SetHorizontal(bool value)
{
    if (Horizontal != value)
    {
        Horizontal = value;
        RectRangeChanged();
    }
}
void UUIScrollView::SetVertical(bool value)
{
	if (Vertical != value)
	{
        Vertical = value;
        RectRangeChanged();
	}
}
void UUIScrollView::SetOnlyOneDirection(bool value)
{
	OnlyOneDirection = value;
}
void UUIScrollView::SetScrollSensitivity(float value)
{
	ScrollSensitivity = FMath::IsFinite(value) ? value : 0.0f;
}

void UUIScrollView::SetWheelProgressStep(float value)
{
	WheelProgressStep = FMath::Clamp(FMath::IsFinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

void UUIScrollView::SetCoordinateMode(EDreamScrollCoordinateMode value)
{
	if (CoordinateMode != value)
	{
		CoordinateMode = value;
		RectRangeChanged();
	}
}

void UUIScrollView::SetKeepProgress(bool value)
{
	if (KeepProgress != value)
	{
		KeepProgress = value;
		RectRangeChanged();
	}
}
void UUIScrollView::SetCanScrollInSmallSize(bool value)
{
    if (CanScrollInSmallSize != value)
    {
        CanScrollInSmallSize = value;
        RectRangeChanged();
    }
}
