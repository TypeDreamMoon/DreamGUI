// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamSlider.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"

void UDreamSlider::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Track"), TrackNode);
	// The fill and the handle each move inside their own AREA, and the behaviour reads that area as
	// the space -- so both halves of each pair are parts a template has to provide.
	OutParts.Emplace(TEXT("FillArea"), FillAreaNode);
	OutParts.Emplace(TEXT("Fill"), FillNode);
	OutParts.Emplace(TEXT("HandleArea"), HandleAreaNode);
	OutParts.Emplace(TEXT("Handle"), HandleNode);
}

void UDreamSlider::RealizeBuiltIn()
{
	using namespace DreamUI;

	// No layout container anywhere in here: a slider is anchor-driven geometry, because the
	// behaviour's whole job is to move the fill's and the handle's anchors inside their areas.
	// That also means SetWidth/SetHeight talk directly to SizeDelta and are not overwritten by any
	// arrange pass -- the opposite situation from a control that lives in a stack.
	Realize(this,
		Widget("Slider")
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Track"),
				Widget("FillArea")
					.Children(
						Node<UDreamRectBlock>("Fill").Stretch()),
				Widget("HandleArea")
					.Children(
						Node<UDreamRectBlock>("Handle"))));
}

void UDreamSlider::WireParts()
{
	// On the CONTENT ROOT, which is what the builder's .Then handed over: the behaviour moves the
	// parts within it, so it belongs to the whole control rather than to any one piece.
	SliderBehaviour = EnsureComponent<UUISlider>(GetContentRoot());
	if (SliderBehaviour == nullptr)
	{
		return;
	}
	// The behaviour reads each part's PARENT as the space it moves the part in; handing it the parts
	// is handing it the geometry.
	SliderBehaviour->SetFill(FillNode);
	SliderBehaviour->SetHandle(HandleNode);
	// The pointer transition rides the handle, the way the toggle's rides its box.
	SliderBehaviour->SetTransitionTarget(HandleNode != nullptr ? HandleNode->GetVisual() : nullptr);
	SliderBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamSlider::HandleValueChanged);
	// The four capture moments, re-broadcast at the control the way the value change is: a consumer
	// binds to this control, never to the behaviour sitting on one of its parts.
	SliderBehaviour->GetOnMouseCaptureBeginEvent().AddUObject(this, &UDreamSlider::HandleMouseCaptureBegin);
	SliderBehaviour->GetOnMouseCaptureEndEvent().AddUObject(this, &UDreamSlider::HandleMouseCaptureEnd);
	SliderBehaviour->GetOnControllerCaptureBeginEvent().AddUObject(this, &UDreamSlider::HandleControllerCaptureBegin);
	SliderBehaviour->GetOnControllerCaptureEndEvent().AddUObject(this, &UDreamSlider::HandleControllerCaptureEnd);
}

void UDreamSlider::ApplyStyle()
{
	const FDreamSliderStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::SliderStyle);

	const bool bHorizontal = Direction == EUISliderDirectionType::LeftToRight
		|| Direction == EUISliderDirectionType::RightToLeft;
	// The handle's drawn size; its brush may state one (Slate's ImageSize), the style otherwise.
	const FVector2D HandleSize = BrushSizeOr(Active.HandleBrush, Active.HandleSize);

	// The track and the fill area share one rect: a line across the middle of the control, as thick
	// as the style says. The handle area is that same line inset by the handle's own size, so the
	// handle's center at 0 and 1 sits over the track's ends rather than overhanging them -- the
	// uGUI arrangement, and the one both preset Blueprints hand-build.
	const FVector2D AxisMin = bHorizontal ? FVector2D(0.0, 0.5) : FVector2D(0.5, 0.0);
	const FVector2D AxisMax = bHorizontal ? FVector2D(1.0, 0.5) : FVector2D(0.5, 1.0);
	auto PlaceOnAxis = [&](UDreamWidget* InNode, const FVector2D& InSizeDelta)
	{
		if (InNode != nullptr)
		{
			InNode->SetHorizontalAndVerticalAnchorMinMax(AxisMin, AxisMax, false, false);
			InNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, InSizeDelta);
		}
	};
	PlaceOnAxis(TrackNode, bHorizontal
		? FVector2D(0.0, Active.TrackThickness)
		: FVector2D(Active.TrackThickness, 0.0));
	PlaceOnAxis(FillAreaNode, bHorizontal
		? FVector2D(0.0, Active.TrackThickness)
		: FVector2D(Active.TrackThickness, 0.0));
	// UMG's IndentHandle, and the inset above is what it means: indented (the default, and what this
	// slider has always drawn) keeps the whole handle inside the track's ends, because the handle's
	// travel is the track MINUS its own width. Un-indented gives the handle the track's full length,
	// so its centre reaches the very ends and half of it hangs off each -- which is the look UMG
	// ships for a handle drawn as a notch rather than as a knob.
	PlaceOnAxis(HandleAreaNode, bIndentHandle
		? (bHorizontal ? FVector2D(-HandleSize.X, HandleSize.Y) : FVector2D(HandleSize.X, -HandleSize.Y))
		: (bHorizontal ? FVector2D(0.0, HandleSize.Y) : FVector2D(HandleSize.X, 0.0)));

	if (HandleNode != nullptr)
	{
		HandleNode->SetWidth(static_cast<float>(HandleSize.X));
		HandleNode->SetHeight(static_cast<float>(HandleSize.Y));
	}
	// Capsules, derived rather than styled: half the thickness rounds a bar fully, half the handle
	// makes it a circle -- the UMG slider's silhouette.
	ShapeFace(TrackNode, Active.TrackThickness * 0.5f);
	ShapeFace(FillNode, Active.TrackThickness * 0.5f);
	ShapeFace(HandleNode, static_cast<float>(FMath::Min(HandleSize.X, HandleSize.Y)) * 0.5f);
	SkinFace(TrackNode, Active.TrackBrush);
	SkinFace(FillNode, Active.FillBrush);
	SkinFace(HandleNode, Active.HandleBrush);
	// The bar's runtime tint rides the track, not the fill: the fill is the accent a style names and
	// UMG's SliderBarColor is the colour of the bar the handle travels along. White is no opinion,
	// so an existing slider is unchanged.
	if (UDreamVisual* TrackVisual = TrackNode != nullptr ? TrackNode->GetVisual() : nullptr)
	{
		TrackVisual->SetColor(TintOver(Active.TrackColor, SliderBarColor));
	}
	if (UDreamVisual* FillVisual = FillNode != nullptr ? FillNode->GetVisual() : nullptr)
	{
		FillVisual->SetColor(Active.FillColor);
	}

	if (SliderBehaviour != nullptr)
	{
		// The handle's five state colours, each through the runtime tint: a handle that goes red
		// while a value is out of range has to stay red through a hover, which a tint does and a
		// single overwritten colour does not.
		PushSelectableState(SliderBehaviour,
			TintOver(Active.HandleNormal, SliderHandleColor),
			TintOver(Active.HandleHovered, SliderHandleColor),
			TintOver(Active.HandlePressed, SliderHandleColor),
			TintOver(Active.HandleDisabled, SliderHandleColor),
			TintOver(Active.HandleFocused, SliderHandleColor),
			Active.TransitionDuration);
		SliderBehaviour->SetLocked(bLocked);
		SliderBehaviour->SetDirectionType(Direction);
		// The two rules the behaviour has always carried and the control never stated. Before the
		// value, because whole numbers snap whatever it is holding.
		SliderBehaviour->SetWholeNumbers(bWholeNumbers);
		SliderBehaviour->SetNavigationChangeInterval(NavigationChangeInterval);
		SliderBehaviour->SetStepSize(StepSize);
		SliderBehaviour->SetMouseUsesStep(bMouseUsesStep);
		SliderBehaviour->SetRequiresControllerLock(bRequiresControllerLock);
		// Range before value, so the value is clamped against the authored range and not the default
		// one; both without events, since pushing authored state is not the user dragging.
		SliderBehaviour->SetMinValue(MinValue, false, false);
		SliderBehaviour->SetMaxValue(MaxValue, false, false);
		SliderBehaviour->SetValueWithoutNotify(Value);
		// Read back, so the property never claims a value the behaviour refused: an authored Value
		// outside [Min, Max] -- or a fractional one on a whole-number slider -- is clamped there, and
		// a property that kept the raw number would be a second answer to "what is this slider set to".
		Value = SliderBehaviour->GetValue();
	}
}

EUISliderDirectionType UDreamSlider::GetDirection() const
{
	return Direction;
}

void UDreamSlider::SetDirection(EUISliderDirectionType InDirection)
{
	if (Direction == InDirection)
	{
		return;
	}
	Direction = InDirection;
	// The whole style, not just the behaviour's flag: the direction decides which axis every part is
	// anchored along, and those anchors are written in ApplyStyle.
	ApplyStyle();
}

float UDreamSlider::GetMinValue() const
{
	return SliderBehaviour != nullptr ? SliderBehaviour->GetMinValue() : MinValue;
}

void UDreamSlider::SetMinValue(float InMinValue)
{
	MinValue = InMinValue;
	if (SliderBehaviour != nullptr)
	{
		// Without keeping the relative value and without an event: this is the author restating the
		// range, not the player moving the handle. The behaviour re-clamps the value, so the mirror
		// is read back rather than assumed.
		SliderBehaviour->SetMinValue(InMinValue, false, false);
		Value = SliderBehaviour->GetValue();
	}
}

float UDreamSlider::GetMaxValue() const
{
	return SliderBehaviour != nullptr ? SliderBehaviour->GetMaxValue() : MaxValue;
}

void UDreamSlider::SetMaxValue(float InMaxValue)
{
	MaxValue = InMaxValue;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetMaxValue(InMaxValue, false, false);
		Value = SliderBehaviour->GetValue();
	}
}

bool UDreamSlider::GetWholeNumbers() const
{
	return bWholeNumbers;
}

void UDreamSlider::SetWholeNumbers(bool bInWholeNumbers)
{
	bWholeNumbers = bInWholeNumbers;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetWholeNumbers(bInWholeNumbers);
		Value = SliderBehaviour->GetValue();
	}
}

float UDreamSlider::GetNavigationChangeInterval() const
{
	return NavigationChangeInterval;
}

void UDreamSlider::SetNavigationChangeInterval(float InInterval)
{
	NavigationChangeInterval = InInterval;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetNavigationChangeInterval(InInterval);
	}
}

float UDreamSlider::GetValue() const
{
	return SliderBehaviour != nullptr ? SliderBehaviour->GetValue() : Value;
}

void UDreamSlider::SetValue(float InValue)
{
	Value = InValue;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetValue(InValue);
	}
}

float UDreamSlider::GetStepSize() const
{
	return StepSize;
}

void UDreamSlider::SetStepSize(float InStepSize)
{
	StepSize = FMath::Max(0.0f, InStepSize);
	if (SliderBehaviour != nullptr)
	{
		// Straight to the behaviour: the step is spent where a drag becomes a value and nothing about
		// the slider's geometry or colours depends on it.
		SliderBehaviour->SetStepSize(StepSize);
	}
}

bool UDreamSlider::GetMouseUsesStep() const
{
	return bMouseUsesStep;
}

void UDreamSlider::SetMouseUsesStep(bool bInMouseUsesStep)
{
	bMouseUsesStep = bInMouseUsesStep;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetMouseUsesStep(bInMouseUsesStep);
	}
}

bool UDreamSlider::GetRequiresControllerLock() const
{
	return bRequiresControllerLock;
}

void UDreamSlider::SetRequiresControllerLock(bool bInRequiresControllerLock)
{
	bRequiresControllerLock = bInRequiresControllerLock;
	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetRequiresControllerLock(bInRequiresControllerLock);
	}
}

bool UDreamSlider::IsControllerCaptured() const
{
	return SliderBehaviour != nullptr && SliderBehaviour->IsControllerCaptured();
}

void UDreamSlider::SetStyle(const FDreamSliderStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

void UDreamSlider::SetLocked(bool bInLocked)
{
	bLocked = bInLocked;
	if (SliderBehaviour != nullptr)
	{
		// Straight onto the behaviour: the lock decides nothing about how the slider is drawn, so a
		// whole style push would be a lot of work with one line of effect.
		SliderBehaviour->SetLocked(bInLocked);
	}
}

void UDreamSlider::SetSliderBarColor(FColor InSliderBarColor)
{
	SliderBarColor = InSliderBarColor;
	// Through the style push, because a tint multiplies the STYLE's colour and this is where that
	// colour is resolved; writing the product onto the visual here would lose it at the next push.
	ApplyStyle();
}

void UDreamSlider::SetSliderHandleColor(FColor InSliderHandleColor)
{
	SliderHandleColor = InSliderHandleColor;
	ApplyStyle();
}

void UDreamSlider::SetIndentHandle(bool bInIndentHandle)
{
	if (bIndentHandle == bInIndentHandle)
	{
		return;
	}
	bIndentHandle = bInIndentHandle;
	// Through the style push, because the inset is computed from the handle's drawn size and that
	// size comes out of the style -- a caller should not have to know which of the two it is asking
	// about.
	ApplyStyle();
}

float UDreamSlider::GetNormalizedValue() const
{
	const float Low = GetMinValue();
	const float High = GetMaxValue();
	// A slider whose ends meet has no positions between them; answering zero is the only thing that
	// is not a division by zero, and it is what UMG's own slider answers.
	return FMath::IsNearlyEqual(Low, High) ? 0.0f : (GetValue() - Low) / (High - Low);
}

void UDreamSlider::HandleMouseCaptureBegin()
{
	OnMouseCaptureBegin.Broadcast();
}

void UDreamSlider::HandleMouseCaptureEnd()
{
	OnMouseCaptureEnd.Broadcast();
}

void UDreamSlider::HandleControllerCaptureBegin()
{
	OnControllerCaptureBegin.Broadcast();
}

void UDreamSlider::HandleControllerCaptureEnd()
{
	OnControllerCaptureEnd.Broadcast();
}

void UDreamSlider::HandleValueChanged(float InValue)
{
	Value = InValue;
	OnValueChanged.Broadcast(InValue);
	OnValueChangedBP.Broadcast(InValue);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Slider", UDreamSlider)
