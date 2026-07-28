// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/UIStandardControls.h"
#include "Core/Components/LexWidget.h"

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
	switch (FillType)
	{
	case EUIProgressBarFillType::LeftToRight:
		FillWidget->SetHorizontalAnchorMinMax(FVector2D(Start, End));
		break;
	case EUIProgressBarFillType::RightToLeft:
		FillWidget->SetHorizontalAnchorMinMax(FVector2D(1.0f - End, 1.0f - Start));
		break;
	case EUIProgressBarFillType::BottomToTop:
		FillWidget->SetVerticalAnchorMinMax(FVector2D(Start, End));
		break;
	case EUIProgressBarFillType::TopToBottom:
		FillWidget->SetVerticalAnchorMinMax(FVector2D(1.0f - End, 1.0f - Start));
		break;
	}
}

void ULexLayoutSelfSpacer::CalculateSize()
{
	if (ULexWidget* Widget = GetWidget())
	{
		Widget->SetWidth(static_cast<float>(Size.X));
		Widget->SetHeight(static_cast<float>(Size.Y));
	}
}

FLexLayoutControlAnchorData ULexLayoutSelfSpacer::GetLayoutControlAnchor(const ULexWidget* Widget) const
{
	FLexLayoutControlAnchorData Result;
	if (Widget == GetWidget())
	{
		Result.bCanControlHorizontalSize = true;
		Result.bCanControlVerticalSize = true;
	}
	return Result;
}
