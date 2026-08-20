// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIScrollView.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"

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
        TargetComp->bRangeCalculated = false;
        TargetComp->RecalculateRange();
    }
}
void UUIScrollViewHelper::OnChildDimensionsChanged(UDreamWidget *Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
    if (!TargetComp.IsValid())
    {
        this->DestroyComponent();
    }
    else
    {
        if (WidthChanged || HeightChanged)
        {
            TargetComp->bRangeCalculated = false;
            TargetComp->RecalculateRange();
        }
    }
}

void UUIScrollView::Awake()
{
    Super::Awake();
    bRangeCalculated = false;
    RecalculateRange();
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
	ReleaseRangeHelper();
	Super::OnDestroy();
}

#if WITH_EDITOR
void UUIScrollView::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    bRangeCalculated = false;
    RecalculateRange();
    if (auto Property = PropertyChangedEvent.MemberProperty)
    {
        if (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UUIScrollView, Progress))
        {
            ApplyContentPositionWithProgress();
        }
    }
}
#endif

void UUIScrollView::RecalculateRange()
{
    if (bRangeCalculated)return;
    if (CheckParameters())
    {
		bRangeCalculated = true;
        if (Horizontal)
        {
            this->CalculateHorizontalRange();
            bAllowHorizontalScroll = true;
        }
        else
        {
            bAllowHorizontalScroll = false;
        }
        if (Vertical)
        {
            this->CalculateVerticalRange();
            bAllowVerticalScroll = true;
        }
        else
        {
            bAllowVerticalScroll = false;
        }

        if (KeepProgress)
        {
            ApplyContentPositionWithProgress();
        }
        else
        {
			const FVector2D Position = GetContentPosition();
            if (
				(bAllowHorizontalScroll && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y))
				|| (bAllowVerticalScroll && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y))
                )
            {
                bCanUpdateAfterDrag = true;
            }
            else
            {
                UpdateProgress(false);
            }
        }
    }
}

void UUIScrollView::OnEnable()
{
    Super::OnEnable();
    bRangeCalculated = false;
    RecalculateRange();
}

void UUIScrollView::OnTransformChanged()
{
    Super::OnTransformChanged();
    bRangeCalculated = false;
    RecalculateRange();
}

void UUIScrollView::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    bRangeCalculated = false;
    RecalculateRange();
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

bool UUIScrollView::OnPointerBeginDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (EventData && CheckParameters() && CheckValidHit(EventData->DragWidget))
    {
        PrevPointerPosition = EventData->PressWorldPoint;
        auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
        const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
        PrevPointerPosition = CurrentPointerPosition;
        bAllowHorizontalScroll = false;
        bAllowVerticalScroll = false;
        if (OnlyOneDirection && Horizontal && Vertical)
        {
            if (FMath::Abs(localMoveDelta.Y) > FMath::Abs(localMoveDelta.Z))
            {
                bAllowHorizontalScroll = true;
            }
            else
            {
                bAllowVerticalScroll = true;
            }
        }
        else
        {
            if (Horizontal)
            {
                bAllowHorizontalScroll = true;
            }
            if (Vertical)
            {
                bAllowVerticalScroll = true;
            }
        }
        bCanUpdateAfterDrag = false;
        OnPointerDrag_Implementation(EventData);
    }
    else
    {
        bAllowHorizontalScroll = bAllowVerticalScroll = false;
    }
    return AllowEventBubbleUp;
}

bool UUIScrollView::OnPointerDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (!EventData || !Content.IsValid())
        return AllowEventBubbleUp;
	FVector2D Position = GetContentPosition();
    auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
    auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
    PrevPointerPosition = CurrentPointerPosition;
    if (bAllowHorizontalScroll)
    {
		auto predict = Position.X + localMoveDelta.Y;
        if ((predict < HorizontalRange.X || predict > HorizontalRange.Y) && RestrictRectArea) //out-of-range, lower the sentitivity
        {
			Position.X += localMoveDelta.Y * OutOfRangeDamper;
        }
        else
        {
			Position.X = predict;
        }
        bCanUpdateAfterDrag = false;
		SetContentPosition(Position);
        UpdateProgress();
    }
    if (bAllowVerticalScroll)
    {
		auto predict = Position.Y + localMoveDelta.Z;
        if ((predict < VerticalRange.X || predict > VerticalRange.Y) && RestrictRectArea)
        {
			Position.Y += localMoveDelta.Z * OutOfRangeDamper;
        }
        else
        {
			Position.Y = predict;
        }
        bCanUpdateAfterDrag = false;
		SetContentPosition(Position);
        UpdateProgress();
    }
    return AllowEventBubbleUp;
}

bool UUIScrollView::OnPointerEndDrag_Implementation(UDreamPointerEventData *EventData)
{
	if (!EventData || !Content.IsValid())
	{
		return AllowEventBubbleUp;
	}
    auto CurrentPointerPosition = EventData->GetWorldPointInPlane();
    const auto localMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(CurrentPointerPosition - PrevPointerPosition);
    if (bAllowHorizontalScroll)
    {
        bCanUpdateAfterDrag = true;
		Velocity.X = localMoveDelta.Y / GetSafeDeltaTime();
    }
    if (bAllowVerticalScroll)
    {
        bCanUpdateAfterDrag = true;
		Velocity.Y = localMoveDelta.Z / GetSafeDeltaTime();
    }
    return AllowEventBubbleUp;
}
bool UUIScrollView::OnPointerScroll_Implementation(UDreamPointerEventData *EventData)
{
	if (EventData && CheckParameters() && CheckValidHit(EventData->EnterWidget))
    {
        if (EventData->ScrollAxisValue != FVector2D::ZeroVector)
        {
            bAllowHorizontalScroll = false;
            bAllowVerticalScroll = false;
            if (OnlyOneDirection && Horizontal && Vertical)
            {
                if (FMath::Abs(EventData->ScrollAxisValue.X) > FMath::Abs(EventData->ScrollAxisValue.Y))
                {
                    bAllowHorizontalScroll = true;
                }
                else
                {
                    bAllowVerticalScroll = true;
                }
            }
            else
            {
                if (Horizontal)
                {
                    bAllowHorizontalScroll = true;
                }
                if (Vertical)
                {
                    bAllowVerticalScroll = true;
                }
            }

			if (WheelProgressStep > UE_SMALL_NUMBER)
			{
				FVector2D NextProgress = Progress;
				if (bAllowHorizontalScroll)
				{
					NextProgress.X -= FMath::Sign(EventData->ScrollAxisValue.X) * WheelProgressStep;
				}
				if (bAllowVerticalScroll)
				{
					NextProgress.Y -= FMath::Sign(EventData->ScrollAxisValue.Y) * WheelProgressStep;
				}
				Velocity = FVector2D::ZeroVector;
				bCanUpdateAfterDrag = false;
				SetScrollProgress(NextProgress);
				return AllowEventBubbleUp;
			}

			FVector2D Position = GetContentPosition();
            if (bAllowHorizontalScroll)
            {
                auto delta = EventData->ScrollAxisValue.X * ScrollSensitivity;
                bCanUpdateAfterDrag = true;
				if ((Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y) && RestrictRectArea)
                {
					Position.X += delta * OutOfRangeDamper;
					Velocity.X = delta * OutOfRangeDamper / GetSafeDeltaTime();
                }
                else
                {
					Position.X += delta;
					Velocity.X = delta / GetSafeDeltaTime();
                }
				SetContentPosition(Position);
            }
            if (bAllowVerticalScroll)
            {
                auto delta = EventData->ScrollAxisValue.Y * -ScrollSensitivity;
                bCanUpdateAfterDrag = true;
				if ((Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y) && RestrictRectArea)
                {
					Position.Y += delta * OutOfRangeDamper;
					Velocity.Y = delta * OutOfRangeDamper / GetSafeDeltaTime();
                }
                else
                {
					Position.Y += delta;
					Velocity.Y = delta / GetSafeDeltaTime();
                }
				SetContentPosition(Position);
            }
			UpdateProgress();
        }
    }
    return AllowEventBubbleUp;
}

void UUIScrollView::SetVelocity(const FVector2D& value)
{
    if (CheckParameters())
    {
        Velocity = value;
		bCanUpdateAfterDrag = !Velocity.IsNearlyZero();
    }
}

void UUIScrollView::SetContent(UDreamWidget* Value)
{
	if (Content.Get() != Value)
	{
		ReleaseRangeHelper();
		Content = Value;
		ContentParent = nullptr;
		bRangeCalculated = false;
		RecalculateRange();
	}
}

void UUIScrollView::SetDecelerateRate(float value)
{
    if (DecelerateRate != value)
    {
        DecelerateRate = value;
        DecelerateRate = FMath::Max(0.0f, DecelerateRate);
    }
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
    if (OutOfRangeDamper != value)
    {
        OutOfRangeDamper = value;
        OutOfRangeDamper = FMath::Clamp(OutOfRangeDamper, 0.0f, 1.0f);
    }
}

void UUIScrollView::SetScrollDelta(FVector2D value)
{
    if (CheckParameters())
    {
        auto delta = value;
		FVector2D Position = GetContentPosition();
        if (Horizontal)
		{
			bAllowHorizontalScroll = true;
			bCanUpdateAfterDrag = true;
			if ((Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y) && RestrictRectArea)
			{
				Position.X += delta.X * OutOfRangeDamper;
				Velocity.X = delta.X * OutOfRangeDamper / GetSafeDeltaTime();
			}
			else
			{
				Position.X += delta.X;
				Velocity.X = delta.X / GetSafeDeltaTime();
			}
			SetContentPosition(Position);
		}
		if (Vertical)
		{
			bAllowVerticalScroll = true;
			bCanUpdateAfterDrag = true;
			if ((Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y) && RestrictRectArea)
			{
				Position.Y += delta.Y * OutOfRangeDamper;
				Velocity.Y = delta.Y * OutOfRangeDamper / GetSafeDeltaTime();
			}
			else
			{
				Position.Y += delta.Y;
				Velocity.Y = delta.Y / GetSafeDeltaTime();
			}
			SetContentPosition(Position);
		}
		UpdateProgress();
    }
}
void UUIScrollView::SetScrollValue(FVector2D value)
{
    if (CheckParameters())
    {
		FVector2D Position = GetContentPosition();
        if (Horizontal)
		{
			bAllowHorizontalScroll = true;
			bCanUpdateAfterDrag = true;
			Position.X = value.X;
			Velocity.X = 0;
        }
		if (Vertical)
		{
			bAllowVerticalScroll = true;
			bCanUpdateAfterDrag = true;
			Position.Y = value.Y;
			Velocity.Y = 0;
		}
		SetContentPosition(Position);
		UpdateProgress();
    }
}

void UUIScrollView::SetScrollProgress(FVector2D value)
{
    if (CheckParameters())
    {
		Progress.X = FMath::Clamp(value.X, 0.0f, 1.0f);
		Progress.Y = FMath::Clamp(value.Y, 0.0f, 1.0f);
		bAllowHorizontalScroll = Horizontal;
		bAllowVerticalScroll = Vertical;
		Velocity = FVector2D::ZeroVector;
		bCanUpdateAfterDrag = false;
		ApplyContentPositionWithProgress();
		UpdateProgress();
    }
}

void UUIScrollView::ScrollTo(UDreamWidget* InChild, bool InEaseAnimation, float InAnimationDuration)
{
    if (!CheckParameters())return;
    auto CenterPos = InChild->GetLocalSpaceCenter();
    auto CenterPosWorld = InChild->GetWorldTransform().TransformPosition(FVector(0, CenterPos.X, CenterPos.Y));
    auto PosOffset = Content->GetWorldTransform().InverseTransformPosition(CenterPosWorld);
    auto TargetContentPos = FVector2D(-PosOffset.Y, -PosOffset.Z);
    TargetContentPos.X = FMath::Clamp(TargetContentPos.X, HorizontalRange.X, HorizontalRange.Y);
    TargetContentPos.Y = FMath::Clamp(TargetContentPos.Y, VerticalRange.X, VerticalRange.Y);
    if (InEaseAnimation)
    {
        auto Tweener = UDreamTweenManager::To(this, FDreamTweenVector2DGetterFunction::CreateWeakLambda(this
            , [this] {
				return GetContentPosition();
            })
            , FDreamTweenVector2DSetterFunction::CreateWeakLambda(this, [this](FVector2D value) {
                this->SetScrollValue(value);
                }), TargetContentPos, InAnimationDuration);
        if (Tweener)
        {
            UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), Tweener);
        }
    }
    else
    {
        SetScrollValue(TargetContentPos);
    }
}

#define POSITION_THRESHOLD 0.001f
void UUIScrollView::UpdateAfterDrag(float deltaTime)
{
	if (!Content.IsValid())
	{
		bCanUpdateAfterDrag = false;
		return;
	}
	FVector2D Position = GetContentPosition();
    if (FMath::Abs(Velocity.X) > KINDA_SMALL_NUMBER || FMath::Abs(Velocity.Y) > KINDA_SMALL_NUMBER//speed larger than threshold
		|| (bAllowHorizontalScroll && (Position.X < HorizontalRange.X || Position.X > HorizontalRange.Y))//horizontal out of range
		|| (bAllowVerticalScroll && (Position.Y < VerticalRange.X || Position.Y > VerticalRange.Y)))//vertical out of range
    {
        bool canMove = false;
        const float dragForceMulitply = 500.0f;
        const float positionLerpTimeMultiply = 10.0f;
        if (bAllowHorizontalScroll)
        {
			if (Position.X - HorizontalRange.X < 0 && RestrictRectArea)
            {
                if (Velocity.X < 0)
                {
					float dragForce = (HorizontalRange.X - Position.X) * dragForceMulitply;
                    Velocity.X += -FMath::Sign(Velocity.X) * dragForce * deltaTime;

					Position.X += Velocity.X * deltaTime;
                    canMove = true;
                }
                else
                {
                    Velocity.X = 0;
					if (FMath::Abs(HorizontalRange.X - Position.X) < POSITION_THRESHOLD)
                    {
						Position.X = HorizontalRange.X;
                    }
                    else
                    {
                        auto lerpAlpha = FMath::Clamp(positionLerpTimeMultiply * deltaTime, 0.0f, 1.0f);
						Position.X = FMath::Lerp(Position.X, HorizontalRange.X, lerpAlpha);
                    }
                    canMove = true;
                }
            }
			else if (Position.X - HorizontalRange.Y > 0 && RestrictRectArea)
            {
                if (Velocity.X > 0) //move right, use opposite force
                {
					float dragForce = (Position.X - HorizontalRange.Y) * dragForceMulitply;
                    Velocity.X += -FMath::Sign(Velocity.X) * dragForce * deltaTime;

					Position.X += Velocity.X * deltaTime;
                    canMove = true;
                }
                else
                {
                    Velocity.X = 0;
					if (FMath::Abs(Position.X - HorizontalRange.Y) < POSITION_THRESHOLD)
                    {
						Position.X = HorizontalRange.Y;
                    }
                    else
                    {
                        auto lerpAlpha = FMath::Clamp(positionLerpTimeMultiply * deltaTime, 0.0f, 1.0f);
						Position.X = FMath::Lerp(Position.X, HorizontalRange.Y, lerpAlpha);
                    }
                    canMove = true;
                }
            }
            else
            {
                auto speedXDir = FMath::Sign(Velocity.X);
                float dragForce = dragForceMulitply * 0.1f;
                float VelocityLerpAlpha = FMath::Clamp(DecelerateRate * dragForce * deltaTime, 0.0f, 1.0f);
                Velocity.X = FMath::Lerp(Velocity.X, 0.0f, VelocityLerpAlpha);
				Position.X += Velocity.X * deltaTime;
                canMove = true;
            }
        }
        if (bAllowVerticalScroll)
        {
			if (Position.Y - VerticalRange.X < 0 && RestrictRectArea)
            {
                if (Velocity.Y < 0)
                {
					float dragForce = (VerticalRange.X - Position.Y) * dragForceMulitply;
                    Velocity.Y += -FMath::Sign(Velocity.Y) * dragForce * deltaTime;

					Position.Y += Velocity.Y * deltaTime;
                    canMove = true;
                }
                else
                {
                    Velocity.Y = 0;
					if (FMath::Abs(VerticalRange.X - Position.Y) < POSITION_THRESHOLD)
                    {
						Position.Y = VerticalRange.X;
                    }
                    else
                    {
                        auto lerpAlpha = FMath::Clamp(positionLerpTimeMultiply * deltaTime, 0.0f, 1.0f);
						Position.Y = FMath::Lerp(Position.Y, VerticalRange.X, lerpAlpha);
                    }
                    canMove = true;
                }
            }
			else if (Position.Y - VerticalRange.Y > 0 && RestrictRectArea)
            {
                if (Velocity.Y > 0) //move up, use opposite force
                {
					float dragForce = (Position.Y - VerticalRange.Y) * dragForceMulitply;
                    Velocity.Y += -FMath::Sign(Velocity.Y) * dragForce * deltaTime;

					Position.Y += Velocity.Y * deltaTime;
                    canMove = true;
                }
                else
                {
                    Velocity.Y = 0;
					if (FMath::Abs(Position.Y - VerticalRange.Y) < POSITION_THRESHOLD)
                    {
						Position.Y = VerticalRange.Y;
                    }
                    else
                    {
                        auto lerpAlpha = FMath::Clamp(positionLerpTimeMultiply * deltaTime, 0.0f, 1.0f);
						Position.Y = FMath::Lerp(Position.Y, VerticalRange.Y, lerpAlpha);
                    }
                    canMove = true;
                }
            }
            else
            {
                float dragForce = dragForceMulitply * 0.1f;
                float VelocityLerpAlpha = FMath::Clamp(DecelerateRate * dragForce * deltaTime, 0.0f, 1.0f);
                Velocity.Y = FMath::Lerp(Velocity.Y, 0.0f, VelocityLerpAlpha);
				Position.Y += Velocity.Y * deltaTime;
                canMove = true;
            }
        }
		if (canMove)
		{
			SetContentPosition(Position);
			UpdateProgress();
        }
    }
    else
    {
        bCanUpdateAfterDrag = false;
    }
}

void UUIScrollView::ApplyContentPositionWithProgress()
{
    if (CheckParameters())
    {
		FVector2D Position = GetContentPosition();
        if (Horizontal)
        {
            bAllowHorizontalScroll = true;

            Progress.X = FMath::Clamp(Progress.X, 0.0f, 1.0f);
			Position.X = FMath::Lerp(HorizontalRange.X, HorizontalRange.Y, 1.0f - Progress.X);
        }
        if (Vertical)
        {
            bAllowVerticalScroll = true;

            Progress.Y = FMath::Clamp(Progress.Y, 0.0f, 1.0f);
			Position.Y = FMath::Lerp(VerticalRange.X, VerticalRange.Y, Progress.Y);
        }
		SetContentPosition(Position);
		bCanUpdateAfterDrag = false;
    }
}


void UUIScrollView::UpdateProgress(bool InFireEvent)
{
    if (!Content.IsValid())
        return;
	const FVector2D Position = GetContentPosition();
    if (bAllowHorizontalScroll)
    {
        if (FMath::Abs(HorizontalRange.Y - HorizontalRange.X) > KINDA_SMALL_NUMBER)
        {
			Progress.X = 1.0f - (Position.X - HorizontalRange.X) / (HorizontalRange.Y - HorizontalRange.X);
        }
    }
    if (bAllowVerticalScroll)
    {
        if (FMath::Abs(VerticalRange.Y - VerticalRange.X) > KINDA_SMALL_NUMBER)
        {
			Progress.Y = (Position.Y - VerticalRange.X) / (VerticalRange.Y - VerticalRange.X);
        }
    }
    if (InFireEvent)
    {
        OnValueChangedCPP.Broadcast(Progress);
        OnValueChangedBP.Broadcast(Progress);
        OnValueChanged.FireEvent(Progress);
    }
}

void UUIScrollView::CalculateHorizontalRange()
{
    if (ContentParent->GetWidth() > Content->GetWidth())//content size smaller than parent
    {
        //parent
        HorizontalRange.X = -ContentParent->GetPivot().X * ContentParent->GetWidth();
        HorizontalRange.Y = (1.0f - ContentParent->GetPivot().X) * ContentParent->GetWidth();
        //self
        HorizontalRange.X += Content->GetPivot().X * Content->GetWidth();
        HorizontalRange.Y += (Content->GetPivot().X - 1.0f) * Content->GetWidth();

        if (KeepProgress)
        {
            if (!CanScrollInSmallSize)
            {
                //this can make content stay at Progress.X's position
                HorizontalRange.X = HorizontalRange.Y = FMath::Lerp(HorizontalRange.X, HorizontalRange.Y
                    , FlipDirectionInSmallSize ? 1.0f - Progress.X : Progress.X
                );
            }
        }
        else
        {
            HorizontalRange.Y -= ContentParent->GetWidth() - Content->GetWidth();
        }
    }
    else//content size bigger than parent
    {
        //self
        HorizontalRange.X = (Content->GetPivot().X - 1.0f) * Content->GetWidth();
        HorizontalRange.Y = Content->GetPivot().X * Content->GetWidth();
        //parent
        HorizontalRange.X += (1.0f - ContentParent->GetPivot().X) * ContentParent->GetWidth();
        HorizontalRange.Y += -ContentParent->GetPivot().X * ContentParent->GetWidth();
    }
	if (CoordinateMode == EDreamScrollCoordinateMode::AnchoredPosition)
	{
		const float CoordinateOffset = Content->GetAnchoredPosition().X - Content->GetRelativeLocation().Y;
		HorizontalRange += FVector2D(CoordinateOffset);
	}
}
void UUIScrollView::CalculateVerticalRange()
{
    if (ContentParent->GetHeight() > Content->GetHeight())//content size smaller than parent
    {
        //parent
        VerticalRange.X = -ContentParent->GetPivot().Y * ContentParent->GetHeight();
        VerticalRange.Y = (1.0f - ContentParent->GetPivot().Y) * ContentParent->GetHeight();
        //self
        VerticalRange.X += Content->GetPivot().Y * Content->GetHeight();
        VerticalRange.Y += (Content->GetPivot().Y - 1.0f) * Content->GetHeight();

        if (KeepProgress)
        {
            if (!CanScrollInSmallSize)
            {
                //this can make content stay at Progress.Y's position
                VerticalRange.X = VerticalRange.Y = FMath::Lerp(VerticalRange.X, VerticalRange.Y
                    , FlipDirectionInSmallSize ? Progress.Y : 1.0f - Progress.Y
                );
            }
        }
        else
        {
            VerticalRange.X += ContentParent->GetHeight() - Content->GetHeight();
        }
    }
    else//content size bigger than parent
    {
        //self
        VerticalRange.X = (Content->GetPivot().Y - 1.0f) * Content->GetHeight();
        VerticalRange.Y = Content->GetPivot().Y * Content->GetHeight();
        //parent
        VerticalRange.X += (1.0f - ContentParent->GetPivot().Y) * ContentParent->GetHeight();
        VerticalRange.Y += -ContentParent->GetPivot().Y * ContentParent->GetHeight();
    }
	if (CoordinateMode == EDreamScrollCoordinateMode::AnchoredPosition)
	{
		const float CoordinateOffset = Content->GetAnchoredPosition().Y - Content->GetRelativeLocation().Z;
		VerticalRange += FVector2D(CoordinateOffset);
	}
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
        bRangeCalculated = false;
        RecalculateRange();
    }
}
void UUIScrollView::SetVertical(bool value)
{
	if (Vertical != value)
	{
        Vertical = value;
        bRangeCalculated = false;
        RecalculateRange();
	}
}
void UUIScrollView::SetOnlyOneDirection(bool value)
{
	if (OnlyOneDirection != value)
	{
        OnlyOneDirection = value;
	}
}
void UUIScrollView::SetScrollSensitivity(float value)
{
	value = FMath::IsFinite(value) ? value : 0.0f;
	if (ScrollSensitivity != value)
    {
        ScrollSensitivity = value;
    }
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
		bRangeCalculated = false;
		RecalculateRange();
	}
}

void UUIScrollView::SetKeepProgress(bool value)
{
	if (KeepProgress != value)
	{
		KeepProgress = value;
		bRangeCalculated = false;
		RecalculateRange();
	}
}
void UUIScrollView::SetCanScrollInSmallSize(bool value)
{
    if (CanScrollInSmallSize != value)
    {
        CanScrollInSmallSize = value;
        bRangeCalculated = false;
        RecalculateRange();
    }
}
