// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UIStandardControls.h"
#include "Core/Components/DreamWidget.h"

void UUIProgressBar::Awake()
{
	Super::Awake();
	ApplyProgress();
	SetCanExecuteTick(bIsMarquee);
}

void UUIProgressBar::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bIsMarquee)
	{
		MarqueeOffset = FMath::Fmod(MarqueeOffset + DeltaTime * MarqueeSpeed, 1.0f + MarqueeWidth);
		ApplyProgress();
	}
}

void UUIProgressBar::SetPercent(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (!FMath::IsNearlyEqual(Percent, Value))
	{
		Percent = Value;
		ApplyProgress();
		OnPercentChanged.Broadcast(Percent);
	}
}

void UUIProgressBar::SetIsMarquee(bool Value)
{
	if (bIsMarquee != Value)
	{
		bIsMarquee = Value;
		SetCanExecuteTick(bIsMarquee);
		ApplyProgress();
	}
}

/*
 * FillType is BlueprintReadWrite and had no setter, so a Blueprint could change the direction and
 * nothing redrew until the next SetPercent -- and a C++ caller had no route at all. Percent has a
 * setter for exactly this reason and this is the same shape: gate on a real change, then apply.
 *
 * No OnPercentChanged broadcast, deliberately. Turning a bar upside down does not change how full
 * it is, and anything listening for a progress value would be told the same number twice.
 */
void UUIProgressBar::SetFillType(EUIProgressBarFillType Value)
{
	if (FillType != Value)
	{
		FillType = Value;
		ApplyProgress();
	}
}

/*
 * Progress lives on ONE axis, and the other one has to be handed back the full span every time.
 *
 * Without that, switching a live bar from a horizontal direction to a vertical one left the
 * horizontal anchors holding the last horizontal progress span: a bar that had been showing
 * twenty-five percent across became a fill squeezed to a quarter of the width AND a quarter of the
 * height, the two spans multiplying into a corner. The bar had no way back either -- nothing else
 * writes those anchors.
 *
 * Resetting to 0..1 rather than restoring some remembered authored span is the deliberate part.
 * Whatever is on the cross axis when a switch happens is the previous direction's progress, which
 * is definitely wrong, and "the whole track" is the only value the bar can know is right. It costs
 * nothing an author would miss: SetHorizontalAnchorMinMax and its vertical twin PRESERVE the anchor
 * offsets and re-derive the size, so a fill inset by a margin keeps its inset -- only anchors, which
 * are this component's to write, are overwritten. Both setters are change-gated internally, so the
 * cross-axis write is free once it has happened.
 */
void UUIProgressBar::ApplyProgress()
{
	if (!FillWidget.IsValid())
	{
		return;
	}
	float Start = 0.0f;
	float End = Percent;
	if (bIsMarquee)
	{
		Start = FMath::Clamp(MarqueeOffset - MarqueeWidth, 0.0f, 1.0f);
		End = FMath::Clamp(MarqueeOffset, 0.0f, 1.0f);
	}
	const FVector2D FullSpan(0.0, 1.0);
	switch (FillType)
	{
	case EUIProgressBarFillType::LeftToRight:
		FillWidget->SetVerticalAnchorMinMax(FullSpan);
		FillWidget->SetHorizontalAnchorMinMax(FVector2D(Start, End));
		break;
	case EUIProgressBarFillType::RightToLeft:
		FillWidget->SetVerticalAnchorMinMax(FullSpan);
		FillWidget->SetHorizontalAnchorMinMax(FVector2D(1.0f - End, 1.0f - Start));
		break;
	case EUIProgressBarFillType::BottomToTop:
		FillWidget->SetHorizontalAnchorMinMax(FullSpan);
		FillWidget->SetVerticalAnchorMinMax(FVector2D(Start, End));
		break;
	case EUIProgressBarFillType::TopToBottom:
		FillWidget->SetHorizontalAnchorMinMax(FullSpan);
		FillWidget->SetVerticalAnchorMinMax(FVector2D(1.0f - End, 1.0f - Start));
		break;
	}
}

void UDreamLayoutSelfSpacer::CalculateSize()
{
	if (UDreamWidget* Widget = GetWidget())
	{
		Widget->SetWidth(static_cast<float>(Size.X));
		Widget->SetHeight(static_cast<float>(Size.Y));
	}
}

FDreamLayoutControlAnchorData UDreamLayoutSelfSpacer::GetLayoutControlAnchor(const UDreamWidget* Widget) const
{
	FDreamLayoutControlAnchorData Result;
	if (Widget == GetWidget())
	{
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}
