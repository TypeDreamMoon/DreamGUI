// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamSlider.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"

void UDreamSlider::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// No layout container anywhere in here: a slider is anchor-driven geometry, because the
	// behaviour's whole job is to move the fill's and the handle's anchors inside their areas.
	// That also means SetWidth/SetHeight talk directly to SizeDelta and are not overwritten by any
	// arrange pass -- the opposite situation from a control that lives in a stack.
	Realize(this,
		Widget("Slider")
			.Stretch()
			.With<UUISlider>()
			.Children(
				Node<UDreamRectBlock>("Track").Out(TrackNode),
				Widget("FillArea").Out(FillAreaNode)
					.Children(
						Node<UDreamRectBlock>("Fill").Out(FillNode).Stretch()),
				Widget("HandleArea").Out(HandleAreaNode)
					.Children(
						Node<UDreamRectBlock>("Handle").Out(HandleNode)))
			.Then([this](UDreamWidget& InRoot)
			{
				SliderBehaviour = InRoot.GetComponent<UUISlider>();
				if (SliderBehaviour == nullptr)
				{
					return;
				}
				// The behaviour reads each part's PARENT as the space it moves the part in; handing
				// it the parts is handing it the geometry.
				SliderBehaviour->SetFill(FillNode);
				SliderBehaviour->SetHandle(HandleNode);
				// The pointer transition rides the handle, the way the toggle's rides its box.
				SliderBehaviour->SetTransitionTarget(HandleNode != nullptr ? HandleNode->GetVisual() : nullptr);
				SliderBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamSlider::HandleValueChanged);
			}));

	ApplyStyle();
}

void UDreamSlider::ApplyStyle()
{
	const FDreamSliderStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::SliderStyle);

	const bool bHorizontal = Direction == EUISliderDirectionType::LeftToRight
		|| Direction == EUISliderDirectionType::RightToLeft;

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
	PlaceOnAxis(HandleAreaNode, bHorizontal
		? FVector2D(-Active.HandleSize.X, Active.HandleSize.Y)
		: FVector2D(Active.HandleSize.X, -Active.HandleSize.Y));

	if (HandleNode != nullptr)
	{
		HandleNode->SetWidth(static_cast<float>(Active.HandleSize.X));
		HandleNode->SetHeight(static_cast<float>(Active.HandleSize.Y));
	}
	// Capsules, derived rather than styled: half the thickness rounds a bar fully, half the handle
	// makes it a circle -- the UMG slider's silhouette.
	ShapeFace(TrackNode, Active.TrackThickness * 0.5f);
	ShapeFace(FillNode, Active.TrackThickness * 0.5f);
	ShapeFace(HandleNode, static_cast<float>(FMath::Min(Active.HandleSize.X, Active.HandleSize.Y)) * 0.5f);
	if (UDreamVisual* TrackVisual = TrackNode != nullptr ? TrackNode->GetVisual() : nullptr)
	{
		TrackVisual->SetColor(Active.TrackColor);
	}
	if (UDreamVisual* FillVisual = FillNode != nullptr ? FillNode->GetVisual() : nullptr)
	{
		FillVisual->SetColor(Active.FillColor);
	}

	if (SliderBehaviour != nullptr)
	{
		SliderBehaviour->SetNormalColor(Active.HandleNormal);
		SliderBehaviour->SetHoveredColor(Active.HandleHovered);
		SliderBehaviour->SetPressedColor(Active.HandlePressed);
		SliderBehaviour->SetDirectionType(Direction);
		// Range before value, so the value is clamped against the authored range and not the default
		// one; both without events, since pushing authored state is not the user dragging.
		SliderBehaviour->SetMinValue(MinValue, false, false);
		SliderBehaviour->SetMaxValue(MaxValue, false, false);
		SliderBehaviour->SetValueWithoutNotify(Value);
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

void UDreamSlider::HandleValueChanged(float InValue)
{
	Value = InValue;
	OnValueChanged.Broadcast(InValue);
	OnValueChangedBP.Broadcast(InValue);
}
