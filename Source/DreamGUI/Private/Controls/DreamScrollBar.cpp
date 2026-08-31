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

void UDreamScrollBarBehaviour::SetHandleWidget(UDreamWidget* InHandle)
{
	Handle = InHandle;
	// The area is normally derived lazily from the handle's parent, and cached. Re-deriving it here
	// keeps a re-pointed handle from being measured against the old one's parent.
	HandleArea = InHandle != nullptr ? InHandle->GetParent() : nullptr;
}

void UDreamScrollBarBehaviour::SetBarDirection(EUIScrollbarDirectionType InDirection)
{
	DirectionType = InDirection;
}

void UDreamScrollBarBehaviour::Start()
{
	Super::Start();
	OnHandleVisualDirtyCPP.Broadcast();
}

void UDreamScrollBarBehaviour::OnEnable()
{
	Super::OnEnable();
	OnHandleVisualDirtyCPP.Broadcast();
}

void UDreamScrollBarBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	OnHandleVisualDirtyCPP.Broadcast();
}

void UDreamScrollBar::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// No layout container: a scroll bar is anchor-driven geometry, and the control is the only thing
	// that moves the handle. The handle is a direct child of the track because the behaviour reads a
	// handle's PARENT as the space its value is measured in -- so the track and the handle area are
	// the same rect here, which is what makes value 0 sit the handle flush against the track's start.
	Realize(this,
		Node<UDreamRectBlock>("Track").Out(TrackNode)
			.Stretch()
			.With<UDreamScrollBarBehaviour>()
			.Children(
				Node<UDreamRectBlock>("Handle").Out(HandleNode))
			.Then([this](UDreamWidget& InRoot)
			{
				BarBehaviour = InRoot.GetComponent<UDreamScrollBarBehaviour>();
				if (BarBehaviour == nullptr)
				{
					return;
				}
				BarBehaviour->SetHandleWidget(HandleNode);
				// The pointer transition rides the handle, the way the slider's does: the track is
				// scenery, the handle is the thing being grabbed.
				BarBehaviour->SetTransitionTarget(HandleNode != nullptr ? HandleNode->GetVisual() : nullptr);
				BarBehaviour->GetOnValueChangedEvent().AddUObject(this, &UDreamScrollBar::HandleValueChanged);
				// The value event covers every path that changes the value; this covers the three
				// lifecycle paths that place the handle without one. See the behaviour's comment.
				BarBehaviour->GetOnHandleVisualDirtyEvent().AddUObject(this, &UDreamScrollBar::ApplyHandleGeometry);
			}));

	ApplyStyle();
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
		BarBehaviour->SetBarDirection(Direction);
		BarBehaviour->SetNavigationChangeInterval(NavigationChangeInterval);
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
	// feeds its bars. The viewport is the content's PARENT because that is the rect the view slides
	// the content inside -- it never stores one of its own.
	UDreamWidget* Content = View->GetContent();
	UDreamWidget* Viewport = Content != nullptr ? Content->GetParent() : nullptr;
	float Fraction = 1.0f;
	if (Content != nullptr && Viewport != nullptr)
	{
		const float Visible = IsHorizontal() ? Viewport->GetWidth() : Viewport->GetHeight();
		const float Total = IsHorizontal() ? Content->GetWidth() : Content->GetHeight();
		Fraction = Total > KINDA_SMALL_NUMBER ? FMath::Clamp(Visible / Total, 0.0f, 1.0f) : 1.0f;
	}
	// The raw progress, never inverted: which end of the bar means zero is the BAR's business, and
	// Direction has already decided it. Inverting here as well would cancel out on two of the four.
	const FVector2D Progress = View->GetScrollProgress();
	PushValueAndSize(IsHorizontal() ? Progress.X : Progress.Y, Fraction, false);
}

void UDreamScrollBar::ApplyHandleGeometry()
{
	if (TrackNode == nullptr || HandleNode == nullptr)
	{
		return;
	}
	const bool bHorizontal = IsHorizontal();
	// The BAR's own size, not the track's. The track is stretched over the bar, and a stretched axis
	// reads back its DELTA (zero) until a layout pass has resolved it -- while ApplyStyle runs before
	// the first one ever does, so a handle placed off the track would start at zero length and stay
	// there until something else re-placed it. The bar itself carries a real size at every moment the
	// handle needs placing: authored before layout, arranged after. Sixth entry in this library's
	// running family of "an anchor-driven child's geometry is only as fresh as its own last write";
	// see DreamProgressBar::ApplyPercent for the one that was found on video.
	const FVector2D TrackSize(GetWidth(), GetHeight());
	const float AxisLength = bHorizontal ? static_cast<float>(TrackSize.X) : static_cast<float>(TrackSize.Y);

	// Read back from the behaviour rather than from the properties: the behaviour clamps, and a drag
	// writes it directly. The properties are the author's numbers, which is a different question.
	const float Fraction = FMath::Clamp(BarBehaviour != nullptr ? BarBehaviour->GetSize() : ResolveEffectiveSize(), 0.0f, 1.0f);
	const float Progress = FMath::Clamp(BarBehaviour != nullptr ? BarBehaviour->GetValue() : Value, 0.0f, 1.0f);
	const float Length = FMath::Clamp(Fraction * AxisLength, 0.0f, AxisLength);
	const float Travel = FMath::Max(AxisLength - Length, 0.0f);
	// RightToLeft and TopToBottom put zero at the far end; the base component's own anchor maths
	// makes the same two the reversed pair.
	const bool bReversed = Direction == EUIScrollbarDirectionType::RightToLeft
		|| Direction == EUIScrollbarDirectionType::TopToBottom;
	const float Offset = (bReversed ? 1.0f - Progress : Progress) * Travel;

	// BOTH axes are absolute numbers read from the track's live arranged size, pushed on every value
	// write, with the anchor collapsed to a POINT on the track's start edge. The component's own
	// ApplyValueToVisual does this with a RATIO anchor instead, and that is the shape this library
	// has now paid for four times: an anchor setter resolves the parent's span AT WRITE TIME, and on
	// every frame that is not a full layout that resolution reads the parent's SizeDelta -- zero, for
	// a stretched track -- rather than its arranged size. The progress bar's fill flickered as a dot,
	// the dropdown's list opened at zero width, and a handle would spend those frames collapsed onto
	// the track's start. Absolute numbers have no span left to resolve, so there is nothing to be
	// stale about; the pivot on the start edge makes the length grow the way the ratio anchor drew it.
	if (bHorizontal)
	{
		HandleNode->SetPivot(FVector2D(0.0, 0.5));
		HandleNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.5), FVector2D(0.0, 0.5), false, false);
		HandleNode->SetAnchoredPositionAndSizeDelta(FVector2D(Offset, 0.0), FVector2D(Length, TrackSize.Y));
	}
	else
	{
		HandleNode->SetPivot(FVector2D(0.5, 0.0));
		HandleNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.0), FVector2D(0.5, 0.0), false, false);
		HandleNode->SetAnchoredPositionAndSizeDelta(FVector2D(0.0, Offset), FVector2D(TrackSize.X, Length));
	}
}

void UDreamScrollBar::PushValueAndSize(float InValue, float InFraction, bool bInBroadcast)
{
	// Clamped where it is stored, unlike the slider's mirror: a scroll position outside 0..1 is not a
	// number anyone authored on purpose, and the behaviour compares before it clamps -- so handing it
	// 1.7 against a held 1.0 would count as a change and broadcast a value that never moved.
	Value = FMath::Clamp(InValue, 0.0f, 1.0f);
	HandleSize = FMath::Clamp(InFraction, 0.0f, 1.0f);
	if (BarBehaviour != nullptr)
	{
		BarBehaviour->SetValueAndSize(Value, ResolveEffectiveSize(), bInBroadcast);
	}
	ApplyHandleGeometry();
}

float UDreamScrollBar::ResolveEffectiveSize() const
{
	float Fraction = FMath::Clamp(HandleSize, 0.0f, 1.0f);
	// The bar's own length, for the reason ApplyHandleGeometry reads it there: the track is stretched
	// over the bar and reads back a zero delta until a layout pass resolves it, which would make the
	// minimum-length floor silently do nothing on exactly the frames it exists for.
	const float AxisLength = IsHorizontal()
		? static_cast<float>(GetWidth())
		: static_cast<float>(GetHeight());
	if (AxisLength > KINDA_SMALL_NUMBER && MinHandleLength > 0.0f)
	{
		Fraction = FMath::Clamp(FMath::Max(Fraction, MinHandleLength / AxisLength), 0.0f, 1.0f);
	}
	return Fraction;
}

void UDreamScrollBar::HandleValueChanged(float InValue)
{
	Value = InValue;
	// The behaviour has just placed the handle its own way; put it back before anyone sees it.
	ApplyHandleGeometry();
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
