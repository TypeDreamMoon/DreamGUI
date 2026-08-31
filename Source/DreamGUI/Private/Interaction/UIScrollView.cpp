// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIScrollView.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"

namespace DreamScrollViewLocal
{
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
	return GetContentPosition() + Delta;
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
	return FVector2D(
		FMath::Max(0.0, ContentSize.X - ViewportSize.X),
		FMath::Max(0.0, ContentSize.Y - ViewportSize.Y));
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

bool UUIScrollView::OnPointerBeginDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (EventData && CheckParameters() && CheckValidHit(EventData->DragWidget))
    {
        PrevPointerPosition = EventData->PressWorldPoint;
        const auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
        const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
        PrevPointerPosition = CurrentPointerPosition;
        ResolveGestureAxes(FVector2D(localMoveDelta.Y, localMoveDelta.Z));
        Velocity = FVector2D::ZeroVector;
        bCanUpdateAfterDrag = false;
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
    // The pointer's delta, damped on whichever axis is already past its boundary -- and past it
    // BEFORE this move rather than after, so a drag heading back into range is at full weight the
    // whole way home instead of crawling the last unit.
    if (bGestureHorizontal)
    {
        Position.X += localMoveDelta.Y * ((RestrictRectArea
            && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y)) ? OutOfRangeDamper : 1.0f);
    }
    if (bGestureVertical)
    {
        Position.Y += localMoveDelta.Z * ((RestrictRectArea
            && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y)) ? OutOfRangeDamper : 1.0f);
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
    RecalculateRange();
    ResolveGestureAxes(FVector2D(EventData->ScrollAxisValue.X, EventData->ScrollAxisValue.Y));

    // The wheel reads in the SAME direction on both axes -- a notch away from the user advances the
    // offset -- which is what the offset model buys: no per-axis sign to remember, because the
    // offset itself already runs left-to-right and top-to-bottom.
    const FVector2D Extent = GetScrollableExtent();
    const FVector2D BeforeOffset = GetScrollOffset();
    FVector2D AfterOffset = BeforeOffset;
    if (WheelProgressStep > UE_SMALL_NUMBER)
    {
        if (bGestureHorizontal) AfterOffset.X -= FMath::Sign(EventData->ScrollAxisValue.X) * WheelProgressStep * Extent.X;
        if (bGestureVertical) AfterOffset.Y -= FMath::Sign(EventData->ScrollAxisValue.Y) * WheelProgressStep * Extent.Y;
    }
    else
    {
        if (bGestureHorizontal) AfterOffset.X -= EventData->ScrollAxisValue.X * ScrollSensitivity;
        if (bGestureVertical) AfterOffset.Y -= EventData->ScrollAxisValue.Y * ScrollSensitivity;
    }
    AfterOffset.X = FMath::Clamp(AfterOffset.X, 0.0, Extent.X);
    AfterOffset.Y = FMath::Clamp(AfterOffset.Y, 0.0, Extent.Y);
    if (AfterOffset.Equals(BeforeOffset, DreamScrollViewLocal::SettleThreshold))
    {
        // Nothing moved: already at that end, or this axis does not scroll. Hand it on.
        return true;
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
            && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y)) ? OutOfRangeDamper : 1.0f;
        Position.X += value.X * Damper;
        Velocity.X = value.X * Damper / DeltaTime;
        bGestureHorizontal = true;
        bCanUpdateAfterDrag = true;
    }
    if (Vertical)
    {
        const float Damper = (RestrictRectArea
            && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y)) ? OutOfRangeDamper : 1.0f;
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

bool UUIScrollView::CalculateRevealContentPosition(UDreamWidget* InChild, FVector2D& OutPosition)
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
    auto DeltaAlongAxis = [](float ItemMin, float ItemMax, float ViewMin, float ViewMax)
    {
        if (ItemMin < ViewMin)
        {
            return ViewMin - ItemMin;
        }
        if (ItemMax > ViewMax)
        {
            return (ItemMax - ItemMin) > (ViewMax - ViewMin) ? ViewMin - ItemMin : ViewMax - ItemMax;
        }
        return 0.0f;
    };

    FVector2D Position = OutPosition;
    if (Horizontal)
    {
        const float Delta = DeltaAlongAxis(ChildLeft, ChildRight, ViewLeft, ViewRight);
        Position.X = FMath::Clamp(Position.X + Delta, HorizontalRange.X, HorizontalRange.Y);
    }
    if (Vertical)
    {
        const float Delta = DeltaAlongAxis(ChildBottom, ChildTop, ViewBottom, ViewTop);
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

bool UUIScrollView::ScrollWidgetIntoView(UDreamWidget* InChild, bool InEaseAnimation, float InAnimationDuration)
{
    FVector2D TargetContentPos;
    if (!CalculateRevealContentPosition(InChild, TargetContentPos))
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
