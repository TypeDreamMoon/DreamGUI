// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Widget/SDreamAngleWheel.h"

#include "Core/DreamLayerStyleTypes.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

EDreamAngleConvention DreamAngleWheel::ParseConvention(const FString& InMeta)
{
	if (InMeta.Equals(TEXT("CW"), ESearchCase::IgnoreCase))
	{
		return EDreamAngleConvention::RightClockwise;
	}
	if (InMeta.Equals(TEXT("TopCW"), ESearchCase::IgnoreCase))
	{
		return EDreamAngleConvention::TopClockwise;
	}
	return EDreamAngleConvention::RightCounterClockwise;
}

float DreamAngleWheel::AngleOf(const FVector2D& InOffset, EDreamAngleConvention InConvention)
{
	switch (InConvention)
	{
	case EDreamAngleConvention::RightClockwise:
		return FMath::RadiansToDegrees(FMath::Atan2(InOffset.Y, InOffset.X));
	case EDreamAngleConvention::TopClockwise:
		return FMath::RadiansToDegrees(FMath::Atan2(InOffset.X, -InOffset.Y));
	default:
		return FMath::RadiansToDegrees(FMath::Atan2(-InOffset.Y, InOffset.X));
	}
}

FVector2D DreamAngleWheel::DirectionOf(float InDegrees, EDreamAngleConvention InConvention)
{
	const double Radians = FMath::DegreesToRadians(static_cast<double>(InDegrees));
	switch (InConvention)
	{
	case EDreamAngleConvention::RightClockwise:
		return FVector2D(FMath::Cos(Radians), FMath::Sin(Radians));
	case EDreamAngleConvention::TopClockwise:
		return FVector2D(FMath::Sin(Radians), -FMath::Cos(Radians));
	default:
		return FVector2D(FMath::Cos(Radians), -FMath::Sin(Radians));
	}
}

float DreamAngleWheel::Wrap(float InDegrees, bool bInSigned)
{
	if (bInSigned)
	{
		// The one rule a light's angle is kept by, so the dial and the viewport's shadow and light handles agree.
		return DreamLayerStyleLight::NormalizeAngle(InDegrees);
	}
	const float Wrapped = FMath::Fmod(InDegrees, 360.0f);
	return Wrapped < 0.0f ? Wrapped + 360.0f : Wrapped;
}

void SDreamAngleWheel::Construct(const FArguments& InArgs)
{
	Angle = InArgs._Angle;
	Diameter = InArgs._Diameter;
	Convention = InArgs._Convention;
	bSigned = InArgs._bSigned;
	OnBeginDrag = InArgs._OnBeginDrag;
	OnAngleChanged = InArgs._OnAngleChanged;
	OnEndDrag = InArgs._OnEndDrag;
}

FVector2D SDreamAngleWheel::ComputeDesiredSize(float) const
{
	return FVector2D(Diameter, Diameter);
}

int32 SDreamAngleWheel::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements,
	int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const bool bEnabled = ShouldBeEnabled(bParentEnabled);
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const FVector2D Centre = Size * 0.5;
	const float Radius = FMath::Max(2.0f, static_cast<float>(FMath::Min(Size.X, Size.Y)) * 0.5f - 2.0f);
	const FLinearColor Rim = FLinearColor(1.0f, 1.0f, 1.0f, bEnabled ? 0.55f : 0.25f);
	const FLinearColor Hand = bEnabled ? FAppStyle::GetSlateColor("SelectionColor").GetSpecifiedColor() : FLinearColor(0.5f, 0.5f, 0.5f, 0.5f);

	TArray<FVector2D> Circle;
	constexpr int32 Segments = 40;
	for (int32 Index = 0; Index <= Segments; ++Index)
	{
		const double T = 2.0 * UE_DOUBLE_PI * Index / Segments;
		Circle.Add(Centre + FVector2D(FMath::Cos(T), FMath::Sin(T)) * Radius);
	}
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), Circle, ESlateDrawEffect::None, Rim, true, 1.0f);

	// Where zero is, a short tick on the rim.
	const FVector2D Zero = DreamAngleWheel::DirectionOf(0.0f, Convention);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(),
		{ Centre + Zero * (Radius - 3.0f), Centre + Zero * Radius }, ESlateDrawEffect::None, Rim, true, 1.0f);

	const TOptional<float> Value = Angle.Get();
	if (Value.IsSet())
	{
		const FVector2D Direction = DreamAngleWheel::DirectionOf(Value.GetValue(), Convention);
		const FVector2D Tip = Centre + Direction * (Radius - 1.0f);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(), { Centre, Tip }, ESlateDrawEffect::None, Hand, true, 2.0f);
		const FVector2D Knob(5.0, 5.0);
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 2, AllottedGeometry.ToPaintGeometry(Knob, FSlateLayoutTransform(Tip - Knob * 0.5)),
			FAppStyle::GetBrush("Icons.FilledCircle"), ESlateDrawEffect::None, Hand);
	}
	return LayerId + 2;
}

float SDreamAngleWheel::AngleAt(const FGeometry& InGeometry, const FVector2D& InScreenPosition) const
{
	const FVector2D Local = InGeometry.AbsoluteToLocal(InScreenPosition);
	return DreamAngleWheel::Wrap(DreamAngleWheel::AngleOf(Local - InGeometry.GetLocalSize() * 0.5, Convention), bSigned);
}

float SDreamAngleWheel::AngleFromEvent(const FGeometry& InGeometry, const FPointerEvent& InEvent) const
{
	float Result = AngleAt(InGeometry, InEvent.GetScreenSpacePosition());
	if (InEvent.IsShiftDown())
	{
		Result = DreamAngleWheel::Wrap(FMath::GridSnap(Result, 15.0f), bSigned);
	}
	else
	{
		Result = FMath::RoundToFloat(Result);
	}
	return Result;
}

FReply SDreamAngleWheel::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled())
	{
		return FReply::Unhandled();
	}
	bDragging = true;
	OnBeginDrag.ExecuteIfBound();
	LastAngle = AngleFromEvent(MyGeometry, MouseEvent);
	OnAngleChanged.ExecuteIfBound(LastAngle);
	return FReply::Handled().CaptureMouse(SharedThis(this));
}

FReply SDreamAngleWheel::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging || !HasMouseCapture())
	{
		return FReply::Unhandled();
	}
	const float Now = AngleFromEvent(MyGeometry, MouseEvent);
	if (Now != LastAngle)
	{
		LastAngle = Now;
		OnAngleChanged.ExecuteIfBound(LastAngle);
	}
	return FReply::Handled();
}

FReply SDreamAngleWheel::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!bDragging)
	{
		return FReply::Unhandled();
	}
	bDragging = false;
	OnEndDrag.ExecuteIfBound(LastAngle);
	return FReply::Handled().ReleaseMouseCapture();
}

FCursorReply SDreamAngleWheel::OnCursorQuery(const FGeometry& MyGeometry, const FPointerEvent& CursorEvent) const
{
	return FCursorReply::Cursor(IsEnabled() ? EMouseCursor::Crosshairs : EMouseCursor::Default);
}
