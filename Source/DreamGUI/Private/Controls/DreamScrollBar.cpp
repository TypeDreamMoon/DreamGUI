// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamScrollBar.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIScrollView.h"

void UDreamScrollBar::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Track"), TrackNode);
	OutParts.Emplace(TEXT("Handle"), HandleNode);
	// The two step buttons are DECORATION in the strict sense: a bar without them is a complete
	// scroll bar (and is what this control drew before they existed), so a template that offers
	// neither is not a broken template.
	OutParts.Emplace(TEXT("ArrowStart"), ArrowStartNode, /*bRequired*/false);
	OutParts.Emplace(TEXT("ArrowEnd"), ArrowEndNode, /*bRequired*/false);
	OutParts.Emplace(TEXT("ArrowStartGlyph"), ArrowStartGlyphNode, /*bRequired*/false);
	OutParts.Emplace(TEXT("ArrowEndGlyph"), ArrowEndGlyphNode, /*bRequired*/false);
}

void UDreamScrollBar::RealizeBuiltIn()
{
	using namespace DreamUI;

	// No layout container anywhere: a scroll bar is anchor-driven geometry, and the behaviour is the
	// only thing that moves the handle. The handle is a direct child of the TRACK because the
	// behaviour reads a handle's PARENT as the space its value is measured in -- so the track is both
	// the groove and the handle's area, which is what makes value 0 sit the handle flush against its
	// start, and what makes insetting the track between the arrows enough to keep the handle out
	// from under them.
	//
	// The root is a bare node rather than the track itself, which it used to be: the arrows are the
	// track's SIBLINGS, not its children, or the handle would travel underneath them.
	Realize(this,
		// "BarRoot", not "ScrollBar": UDreamScrollBox nests one of these under the display name
		// ScrollBar, and a node inside the bar wearing the same name would put two widgets of one
		// name in that box's tree -- the exact shape the expander's Header collision was.
		Widget("BarRoot")
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("ArrowStart")
					.Self([](UDreamWidget& InArrow) { InArrow.SetWidgetActive(false); })
					.Children(
						DreamUI::Text("ArrowStartGlyph")
							.Visual([](UDreamText& InText)
							{
								// U+25BC, the one triangle proven to exist in the default SDF font --
								// the dropdown's arrow. The other three directions are this glyph
								// ROTATED (see ApplyArrows), rather than three more code points that
								// may each turn out to be a tofu box.
								InText.SetText(FText::AsCultureInvariant(TEXT("▼")));
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
							})
							.Stretch()),
				Node<UDreamRectBlock>("Track")
					.Stretch()
					.Children(
						Node<UDreamRectBlock>("Handle")),
				Node<UDreamRectBlock>("ArrowEnd")
					.Self([](UDreamWidget& InArrow) { InArrow.SetWidgetActive(false); })
					.Children(
						DreamUI::Text("ArrowEndGlyph")
							.Visual([](UDreamText& InText)
							{
								InText.SetText(FText::AsCultureInvariant(TEXT("▼")));
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
							})
							.Stretch())));
}

void UDreamScrollBar::WireParts()
{
	// Ensure, not Get: on the template road the arrows are somebody's drawing, and this is what makes
	// them buttons at all. Bound before the early-out below, so a bar whose track lost its behaviour
	// still has working arrows.
	ArrowStartBehaviour = EnsureComponent<UUIButton>(ArrowStartNode);
	if (ArrowStartBehaviour != nullptr && ArrowStartNode != nullptr)
	{
		ArrowStartBehaviour->SetTransitionTarget(ArrowStartNode->GetVisual());
		ArrowStartBehaviour->GetOnClickEvent().AddUObject(this, &UDreamScrollBar::HandleArrowStartClicked);
	}
	ArrowEndBehaviour = EnsureComponent<UUIButton>(ArrowEndNode);
	if (ArrowEndBehaviour != nullptr && ArrowEndNode != nullptr)
	{
		ArrowEndBehaviour->SetTransitionTarget(ArrowEndNode->GetVisual());
		ArrowEndBehaviour->GetOnClickEvent().AddUObject(this, &UDreamScrollBar::HandleArrowEndClicked);
	}

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
		PushSelectableState(BarBehaviour, Active.HandleNormal, Active.HandleHovered, Active.HandlePressed,
			Active.HandleDisabled, Active.HandleFocused, Active.TransitionDuration);
		BarBehaviour->SetDirectionType(Direction);
		BarBehaviour->SetNavigationChangeInterval(NavigationChangeInterval);
		// The floor lives in the behaviour, which is the one place that can honour it in the drawn
		// length and in the drag scale at the same time -- and the only one that reads the TRACK's
		// resolved length rather than guessing it from the control's.
		BarBehaviour->SetMinHandleSize(MinHandleLength);
		// The groove the handle runs in: an inset across the axis, so the bar reads as a pill in a
		// track rather than as one solid slab. Zero is what this control has always drawn.
		BarBehaviour->SetHandlePadding(Active.HandlePadding);
	}

	// Before the handle's rect, because the arrows decide how long the TRACK is and the handle's
	// rect is read off exactly that.
	ApplyArrows(Active);

	// Last, and after the thickness: the handle's rect is read off the track's live size.
	PushValueAndSize(Value, HandleSize, false);
}

void UDreamScrollBar::ApplyArrows(const FDreamScrollBarStyle& InActive)
{
	const bool bHorizontal = IsHorizontal();
	// A square at each end: an arrow button is as long as the bar is thick, which is the one
	// proportion that needs no style knob of its own and is what every desktop bar draws.
	const float ArrowLength = bShowArrows ? InActive.Thickness : 0.0f;

	if (ArrowStartNode != nullptr)
	{
		ArrowStartNode->SetWidgetActive(bShowArrows);
	}
	if (ArrowEndNode != nullptr)
	{
		ArrowEndNode->SetWidgetActive(bShowArrows);
	}

	if (TrackNode != nullptr)
	{
		// Stretched along the bar and inset by an arrow at each end -- the scroll box's gutter idiom:
		// a SizeDelta on a stretched axis is the difference from the parent's span, so the two arrows
		// go in negative and the position stays centred between them.
		TrackNode->SetPivot(FVector2D(0.5, 0.5));
		TrackNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
		TrackNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, bHorizontal
			? FVector2D(-2.0 * ArrowLength, 0.0)
			: FVector2D(0.0, -2.0 * ArrowLength));
	}
	if (!bShowArrows)
	{
		return;
	}

	// Absolute squares pinned to the ends, against POINT anchors -- the law this codebase paid four
	// defects for in a day. The START of a horizontal bar is its left edge; of a vertical one, its
	// TOP, because a scroll bar's zero is the top and Y runs up, so the start anchor is Y = 1.
	const FVector2D StartAnchor = bHorizontal ? FVector2D(0.0, 0.5) : FVector2D(0.5, 1.0);
	const FVector2D EndAnchor = bHorizontal ? FVector2D(1.0, 0.5) : FVector2D(0.5, 0.0);
	const FVector2D ArrowSize(InActive.Thickness, InActive.Thickness);
	auto PlaceArrow = [&ArrowSize](UDreamWidget* InNode, const FVector2D& InAnchor)
	{
		if (InNode != nullptr)
		{
			InNode->SetPivot(InAnchor);
			InNode->SetHorizontalAndVerticalAnchorMinMax(InAnchor, InAnchor, false, false);
			InNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, ArrowSize);
		}
	};
	PlaceArrow(ArrowStartNode, StartAnchor);
	PlaceArrow(ArrowEndNode, EndAnchor);

	ShapeFace(ArrowStartNode, InActive.CornerRadius);
	ShapeFace(ArrowEndNode, InActive.CornerRadius);
	SkinFace(ArrowStartNode, InActive.HandleBrush);
	SkinFace(ArrowEndNode, InActive.HandleBrush);
	if (ArrowStartBehaviour != nullptr)
	{
		// An arrow wears the HANDLE's five colours: it is the same furniture, and a second set of
		// style fields for it would be five more knobs saying the same thing.
		PushSelectableState(ArrowStartBehaviour, InActive.HandleNormal, InActive.HandleHovered,
			InActive.HandlePressed, InActive.HandleDisabled, InActive.HandleFocused, InActive.TransitionDuration);
	}
	if (ArrowEndBehaviour != nullptr)
	{
		PushSelectableState(ArrowEndBehaviour, InActive.HandleNormal, InActive.HandleHovered,
			InActive.HandlePressed, InActive.HandleDisabled, InActive.HandleFocused, InActive.TransitionDuration);
	}

	// ONE glyph, rotated. The down-pointing triangle is the only one proven to exist in the default
	// SDF font, and a missing code point draws a tofu box rather than nothing -- which is why the
	// spin box's minus is an ASCII hyphen and the expander's indicators are plus and minus. Rotating
	// the widget costs nothing and cannot be missing.
	auto OrientGlyph = [](UDreamWidget* InNode, float InYawDegrees)
	{
		if (InNode != nullptr)
		{
			// Around the axis that faces the viewer in this plane: the UI is the YZ plane, so a
			// spin within it is a ROLL.
			InNode->SetRelativeRotationEuler(FRotator(0.0f, 0.0f, InYawDegrees));
		}
	};
	// Vertical: start points up (the glyph turned half about), end points down (as drawn).
	// Horizontal: start points left, end points right.
	OrientGlyph(ArrowStartGlyphNode, bHorizontal ? -90.0f : 180.0f);
	OrientGlyph(ArrowEndGlyphNode, bHorizontal ? 90.0f : 0.0f);
	auto StyleGlyph = [&InActive](UDreamWidget* InNode)
	{
		if (UDreamText* GlyphVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr)
		{
			GlyphVisual->SetColor(InActive.TrackColor);
			// Half the bar's thickness, so the triangle sits inside its square with room around it.
			GlyphVisual->SetFontSize(FMath::Max(InActive.Thickness * 0.5f, 1.0f));
		}
	};
	StyleGlyph(ArrowStartGlyphNode);
	StyleGlyph(ArrowEndGlyphNode);
}

void UDreamScrollBar::HandleArrowStartClicked()
{
	// Towards zero, which is the top of a vertical bar and the left of a horizontal one -- the same
	// end the start arrow is pinned to, so the button points where it goes.
	SetValue(Value - ArrowStepSize);
}

void UDreamScrollBar::HandleArrowEndClicked()
{
	SetValue(Value + ArrowStepSize);
}

void UDreamScrollBar::SetShowArrows(bool bInShowArrows)
{
	if (bShowArrows == bInShowArrows)
	{
		return;
	}
	bShowArrows = bInShowArrows;
	// The whole style push: the arrows decide how long the TRACK is, and the handle's rect is read
	// off exactly that -- so waking a pair of buttons is a re-layout of everything below them.
	ApplyStyle();
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
