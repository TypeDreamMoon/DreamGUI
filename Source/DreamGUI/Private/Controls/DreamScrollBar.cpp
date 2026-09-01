// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamScrollBar.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"

void UDreamScrollBar::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Track"), TrackNode);
	OutParts.Emplace(TEXT("Handle"), HandleNode);
}

void UDreamScrollBar::RealizeBuiltIn()
{
	using namespace DreamUI;

	// No layout container: a scroll bar is anchor-driven geometry, and the behaviour is the only
	// thing that moves the handle. The handle is a direct child of the track because the behaviour
	// reads a handle's PARENT as the space its value is measured in -- so the track and the handle
	// area are the same rect here, which is what makes value 0 sit the handle flush against the
	// track's start.
	Realize(this,
		Node<UDreamRectBlock>("Track")
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Handle")));
}

void UDreamScrollBar::WireParts()
{
	BarBehaviour = EnsureComponent<UUIScrollbar>(TrackNode);
	if (BarBehaviour == nullptr)
	{
		return;
	}
	BarBehaviour->SetHandle(HandleNode);
	// The pointer transition rides the handle, the way the slider's does: the track is scenery, the
	// handle is the thing being grabbed.
	BarBehaviour->SetTransitionTarget(HandleNode != nullptr ? HandleNode->GetVisual() : nullptr);
	BarBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamScrollBar::HandleValueChanged);
}

void UDreamScrollBar::ApplyStyle()
{
	const FDreamScrollBarStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ScrollBarStyle);
	const bool bHorizontal = IsHorizontal();

	// The bar's own thickness, across its axis; its LENGTH belongs to whoever placed it. Both halves
	// sync the slot's desired-size snapshot for the same reason SizeControlHeight does -- the slot's
	// first capture happens before any style has applied, so an Auto consumer would measure the
	// pre-style default rather than the style's number.
	if (bHorizontal)
	{
		SizeControlHeight(Active.Thickness);
	}
	else
	{
		SetWidth(Active.Thickness);
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}

	ShapeFace(TrackNode, Active.CornerRadius);
	ShapeFace(HandleNode, Active.CornerRadius);
	SkinFace(TrackNode, Active.TrackBrush);
	SkinFace(HandleNode, Active.HandleBrush);
	if (UDreamVisual* TrackVisual = TrackNode != nullptr ? TrackNode->GetVisual() : nullptr)
	{
		TrackVisual->SetColor(Active.TrackColor);
	}

	if (BarBehaviour != nullptr)
	{
		// The handle's colour is the SELECTABLE's, not the visual's: the behaviour re-tints its
		// transition target on every state change, so a handle whose behaviour was never given
		// colours ships white the first time a pointer touches it.
		BarBehaviour->SetNormalColor(Active.HandleNormal);
		BarBehaviour->SetHoveredColor(Active.HandleHovered);
		BarBehaviour->SetPressedColor(Active.HandlePressed);
		BarBehaviour->SetDirectionType(Direction);
		BarBehaviour->SetNavigationChangeInterval(NavigationChangeInterval);
		// The floor lives in the behaviour, which is the one place that can honour it in the drawn
		// length and in the drag scale at the same time -- and the only one that reads the TRACK's
		// resolved length rather than guessing it from the control's.
		BarBehaviour->SetMinHandleSize(MinHandleLength);
	}

	// Last, and after the thickness: the handle's rect is read off the track's live size.
	PushValueAndSize(Value, HandleSize, false);
}

float UDreamScrollBar::GetValue() const
{
	return BarBehaviour != nullptr ? BarBehaviour->GetValue() : Value;
}

void UDreamScrollBar::SetValue(float InValue)
{
	PushValueAndSize(InValue, HandleSize, true);
}

void UDreamScrollBar::SetValueWithoutNotify(float InValue)
{
	PushValueAndSize(InValue, HandleSize, false);
}

float UDreamScrollBar::GetHandleSize() const
{
	return HandleSize;
}

void UDreamScrollBar::SetHandleSize(float InFraction)
{
	PushValueAndSize(Value, InFraction, false);
}

bool UDreamScrollBar::IsHorizontal() const
{
	return Direction == EUIScrollbarDirectionType::LeftToRight
		|| Direction == EUIScrollbarDirectionType::RightToLeft;
}

void UDreamScrollBar::SetScrollView(UUIScrollView* InView)
{
	if (UUIScrollView* Previous = ScrollView.Get())
	{
		Previous->GetOnValueChangedEvent().Remove(ScrollViewDelegateHandle);
	}
	ScrollViewDelegateHandle.Reset();
	ScrollView = InView;
	if (InView != nullptr)
	{
		ScrollViewDelegateHandle = InView->GetOnValueChangedEvent()
			.AddUObject(this, &UDreamScrollBar::HandleScrollViewProgress);
	}
	RefreshFromScrollView();
}

UUIScrollView* UDreamScrollBar::GetScrollView() const
{
	return ScrollView.Get();
}

void UDreamScrollBar::RefreshFromScrollView()
{
	UUIScrollView* View = ScrollView.Get();
	if (View == nullptr)
	{
		return;
	}
	// The visible fraction is viewport over content, which is the same ratio UUIScrollViewWithScrollbar
	// feeds its bars -- and both numbers are the view's to answer now that it measures them itself.
	const FVector2D ViewportSize = View->GetViewportSize();
	const FVector2D ContentSize = View->GetContentSize();
	const bool bHorizontal = IsHorizontal();
	const double Visible = bHorizontal ? ViewportSize.X : ViewportSize.Y;
	const double Total = bHorizontal ? ContentSize.X : ContentSize.Y;
	const float Fraction = Total > KINDA_SMALL_NUMBER
		? FMath::Clamp(static_cast<float>(Visible / Total), 0.0f, 1.0f)
		: 1.0f;
	// The raw progress, never inverted: which end of the bar means zero is the BAR's business, and
	// Direction has already decided it. Inverting here as well would cancel out on two of the four.
	const FVector2D Progress = View->GetScrollProgress();
	PushValueAndSize(static_cast<float>(bHorizontal ? Progress.X : Progress.Y), Fraction, false);
}

void UDreamScrollBar::PushValueAndSize(float InValue, float InFraction, bool bInBroadcast)
{
	// Clamped where it is stored: a scroll position outside 0..1 is not a number anyone authored on
	// purpose, and the mirror properties are what .dui and the details panel read back.
	Value = FMath::Clamp(InValue, 0.0f, 1.0f);
	HandleSize = FMath::Clamp(InFraction, 0.0f, 1.0f);
	if (BarBehaviour != nullptr)
	{
		// The behaviour places the handle from this, floor included. Nothing re-places it afterwards
		// -- the whole point of the base class owning the geometry.
		BarBehaviour->SetValueAndSize(Value, HandleSize, bInBroadcast);
	}
}

void UDreamScrollBar::HandleValueChanged(float InValue)
{
	Value = InValue;
	if (UUIScrollView* View = ScrollView.Get())
	{
		// The other axis is read back rather than zeroed: a bar owns one axis of a view that may
		// scroll on two, and SetScrollProgress takes both at once.
		FVector2D Progress = View->GetScrollProgress();
		if (IsHorizontal())
		{
			Progress.X = Value;
		}
		else
		{
			Progress.Y = Value;
		}
		View->SetScrollProgress(Progress);
	}
	OnValueChanged.Broadcast(Value);
	OnValueChangedBP.Broadcast(Value);
}

void UDreamScrollBar::HandleScrollViewProgress(FVector2D InProgress)
{
	// Silent, which is what stops the ring: the view moved, so telling the view about it again would
	// be the same number going back round.
	RefreshFromScrollView();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ScrollBar", UDreamScrollBar)
