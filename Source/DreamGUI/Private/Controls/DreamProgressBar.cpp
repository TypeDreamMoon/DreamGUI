// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamProgressBar.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

void UDreamProgressBar::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Track"), TrackNode);
	OutParts.Emplace(TEXT("Fill"), FillNode);
}

void UDreamProgressBar::RealizeBuiltIn()
{
	using namespace DreamUI;

	// No layout container and no behaviour: the fill is anchor-driven geometry and the control is
	// the only thing that moves it. The fill starts stretched -- (0,0)-(1,1) with a zero SizeDelta,
	// which matters because anchors alone do not clear it -- and ApplyPercent writes over that.
	//
	// The same two nodes serve both shapes. A ring needs no extra part: Radial turns each rect into
	// a circle drawn as its own border, and the swept wedge is a property of the fill rect.
	Realize(this,
		Node<UDreamRectBlock>("Track")
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Fill").Stretch()));
}

void UDreamProgressBar::WireParts()
{
	// The fill's rect is written in ABSOLUTE numbers read off the track's live size (see
	// ApplyPercent), and absolute numbers go stale the moment the track is arranged to a new one.
	// Nothing re-derives them: the fill is point-anchored precisely so that no setter resolves a
	// span, which is the same trade the scroll box and the list make -- and both of them pay for it
	// with exactly this subscription.
	//
	// On the TRACK rather than on the control: the track is what ApplyPercent measures, and it is
	// stretched to the control, so a control that was resized reaches here one hop later with the
	// number already resolved. Subscribing to the control instead would fire while the track still
	// held the old width.
	if (TrackNode != nullptr)
	{
		TrackNode->GetDimensionChangedEvent().AddUObject(this, &UDreamProgressBar::HandleTrackDimensionsChanged);
	}
}

void UDreamProgressBar::HandleTrackDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged)
{
	if (!bWidthChanged && !bHeightChanged)
	{
		return;
	}
	// Only the fill's rect, never the whole style: a style push writes the control's own size, and
	// answering a resize by resizing would be a loop rather than a refresh. ApplyPercent writes the
	// FILL, whose own dimension event is a different one from the track's, so this cannot re-enter.
	ApplyPercent();
}

void UDreamProgressBar::ApplyStyle()
{
	const FDreamProgressBarStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ProgressBarStyle);

	ShapeFace(TrackNode, Active.CornerRadius);
	ShapeFace(FillNode, Active.CornerRadius);
	SkinFace(TrackNode, Active.TrackBrush);
	SkinFace(FillNode, Active.FillBrush);
	if (UDreamVisual* TrackVisual = TrackNode != nullptr ? TrackNode->GetVisual() : nullptr)
	{
		TrackVisual->SetColor(Active.TrackColor);
	}
	if (UDreamVisual* FillVisual = FillNode != nullptr ? FillNode->GetVisual() : nullptr)
	{
		FillVisual->SetColor(Active.FillColor);
	}
	// After the two above, and deliberately: ShapeFace states a Value-mode radius and SkinFace a body
	// texture, and the ring overrides exactly those. Before ApplyPercent, which needs the shape
	// settled to know whether the percent is a width or an angle.
	ApplyShape(Active);
	if (Shape == EDreamProgressShape::Radial)
	{
		// A ring is square and states BOTH axes -- there is no "length comes from whoever placed it"
		// for a circle. SizeControl rather than SizeControlHeight for that reason, and the track
		// stretches to it. NOT SizeFace: that helper is for PARTS, because it re-captures the whole
		// authored rect, and this control's live anchors may already be holding layout output -- see
		// SizeControl's own note, which spells out why enshrining that as the restore target is the
		// one thing a control must not do to itself.
		SizeControl(Active.RadialSize);
	}
	else
	{
		// The control's own height; placed in a stack this is what Auto measures. Width belongs to
		// whoever placed the control.
		SizeControlHeight(Active.Height);
	}
	// Re-stated here as well as in the setter, because the flag is also arrived at by authoring: a
	// .dui line or a details-panel edit writes it raw, and the tick has to follow whichever road it
	// came down. Shape is part of the answer -- a ring has no sweep.
	SetWantsTick(bIsMarquee && Shape == EDreamProgressShape::Bar);
	ApplyPercent();
}

float UDreamProgressBar::GetPercent() const
{
	return Percent;
}

void UDreamProgressBar::SetPercent(float InPercent)
{
	Percent = InPercent;
	ApplyPercent();
}

EDreamProgressShape UDreamProgressBar::GetShape() const
{
	return Shape;
}

void UDreamProgressBar::SetShape(EDreamProgressShape InShape)
{
	if (Shape == InShape)
	{
		return;
	}
	Shape = InShape;
	// The whole style, not just the silhouette: the shape decides which size knobs apply (RadialSize
	// against Height), what the corner radius means, and whether the fill is a width or a wedge.
	ApplyStyle();
}

void UDreamProgressBar::ApplyPercent()
{
	if (FillNode == nullptr)
	{
		return;
	}
	// Clamped where it becomes geometry, not where it is stored: the property keeps the author's
	// number (UMG does the same).
	const float Clamped = FMath::Clamp(Percent, 0.0f, 1.0f);
	// The track's live arranged size, which both shapes write absolutes from -- see the flicker note
	// below. Read once, before the branch, because the rule is the same either way.
	const FVector2D TrackSize = TrackNode != nullptr
		? FVector2D(TrackNode->GetWidth(), TrackNode->GetHeight())
		: FVector2D::ZeroVector;

	if (Shape == EDreamProgressShape::Radial)
	{
		// The ring is not a percentage of anything geometric: the fill sits exactly ON the track,
		// concentric and the same size, and the percent is spent entirely inside the rect as the
		// swept wedge. Absolute size from the track's live rect and a point anchor at its centre,
		// for the same reason the bar path does it -- an anchor SETTER resolves the parent's span at
		// write time, and a stretched parent's SizeDelta is zero on every frame but a full-layout
		// one.
		FillNode->SetPivot(FVector2D(0.5, 0.5));
		FillNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
		FillNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, TrackSize);

		if (UDreamRectBlock* FillRect = Cast<UDreamRectBlock>(FillNode->GetVisual()))
		{
			const FDreamProgressBarStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ProgressBarStyle);
			// The rect's own wedge mask, applied last in the shader (DreamUIRectBlock.ush) to
			// everything it drew -- border included, which is what makes it cut a RING rather than a
			// pie once the body is off.
			FillRect->SetEnableRadialFill(true);
			FillRect->SetRadialFillCenterUnitMode(EDreamRectBlockUnitMode::Percentage);
			FillRect->SetRadialFillCenter(FVector2D(0.5, 0.5));
			// The quarter turn is the whole conversion. Working the shader's wedge maths through:
			// the kept wedge runs from RadialFillRotation to RadialFillRotation + RadialFillAngle,
			// measured with atan2 in UV space -- whose Y runs DOWN (the same reason the shader flips
			// the authored fill centre, which is authored Y-up). Degrees there are therefore
			// CLOCKWISE from three o'clock, and this style speaks clockwise from twelve, so the two
			// conventions differ by exactly 90 degrees and nothing else.
			FillRect->SetRadialFillRotation(Active.RadialStartAngle - 90.0f);
			// Percent, made an angle. 360 is the shader's "no mask at all" (its gate is
			// sign(max(0, 360 - angle))), so a full bar is a whole ring rather than a hairline gap.
			FillRect->SetRadialFillAngle(Clamped * 360.0f);
		}
		return;
	}

	// BOTH axes are absolute numbers read from the track's live arranged size, pushed on every
	// percent write. The anchor-ratio route asked the setter to resolve the parent's span at write
	// time, and on all but the occasional full-layout frame that resolution saw the track's
	// SizeDelta (zero -- it stretches) rather than its arranged width: the fill spent those frames
	// at zero width, rendered as a round dot walking the track (the flicker). The same family as
	// the dropdown list's stale width and this fill's own stale height: an anchor-driven child's
	// geometry is only as fresh as the last time ITS OWN data changed, so the control feeds it
	// values with no spans left to resolve.
	//
	// WHICH edge it grows from is FillType's, and the whole of that is three numbers: the pivot (and
	// the point anchor under it) sits on the growing edge, the length is spent along one axis, and
	// the sweep -- when there is one -- travels in the direction that edge points.
	const bool bHorizontal = (FillType == EDreamProgressFillType::LeftToRight
		|| FillType == EDreamProgressFillType::RightToLeft);
	const bool bFromFarEdge = (FillType == EDreamProgressFillType::RightToLeft
		|| FillType == EDreamProgressFillType::TopToBottom);
	// The anchor's own coordinates: X runs right, Y runs UP, so the far edge is 1 on both axes and
	// TopToBottom grows downward from a Y of 1.
	const FVector2D Anchor = bHorizontal
		? FVector2D(bFromFarEdge ? 1.0 : 0.0, 0.5)
		: FVector2D(0.5, bFromFarEdge ? 1.0 : 0.0);
	const double TrackLength = bHorizontal ? TrackSize.X : TrackSize.Y;
	const double TrackThickness = bHorizontal ? TrackSize.Y : TrackSize.X;

	// Indeterminate: the fill is a fixed SHORT length and the percent decides nothing -- what moves
	// is where it is. One cycle carries it from entirely off the near edge to entirely off the far
	// one, which is why the travel is the track plus the fill rather than the track alone.
	double FillLength = Clamped * TrackLength;
	double Offset = 0.0;
	if (bIsMarquee)
	{
		FillLength = FMath::Clamp(MarqueeFraction, 0.01f, 1.0f) * TrackLength;
		const double Phase = MarqueeDuration > KINDA_SMALL_NUMBER
			? FMath::Fmod(MarqueeTime / MarqueeDuration, 1.0)
			: 0.0;
		// Measured from the growing edge, so it is positive in the direction the fill grows; the sign
		// that turns it into an anchored position is applied below, with the axis.
		Offset = Phase * (TrackLength + FillLength) - FillLength;
	}
	const double Signed = bFromFarEdge ? -Offset : Offset;

	FillNode->SetPivot(Anchor);
	FillNode->SetHorizontalAndVerticalAnchorMinMax(Anchor, Anchor, false, false);
	FillNode->SetAnchoredPositionAndSizeDelta(
		bHorizontal ? FVector2D(Signed, 0.0) : FVector2D(0.0, Signed),
		bHorizontal ? FVector2D(FillLength, TrackThickness) : FVector2D(TrackThickness, FillLength));
}

EDreamProgressFillType UDreamProgressBar::GetFillType() const
{
	return FillType;
}

void UDreamProgressBar::SetFillType(EDreamProgressFillType InFillType)
{
	if (FillType == InFillType)
	{
		return;
	}
	FillType = InFillType;
	// The fill's rect alone: the direction decides which edge it hangs from and which axis it spends,
	// and both of those are written by ApplyPercent. Nothing about the style changes with it.
	ApplyPercent();
}

bool UDreamProgressBar::GetIsMarquee() const
{
	return bIsMarquee;
}

void UDreamProgressBar::SetIsMarquee(bool bInIsMarquee)
{
	if (bIsMarquee == bInIsMarquee)
	{
		return;
	}
	bIsMarquee = bInIsMarquee;
	// The sweep starts from the near edge every time it is turned on, rather than resuming wherever
	// the last one happened to stop -- a bar that appears mid-stride reads as a glitch.
	MarqueeTime = 0.0f;
	// The tick is the marquee's ONLY cost and it is opted in and out with it: a determinate bar joins
	// no tick list at all. SetWantsTick is safe before begin play -- the flag is read by the start
	// pass -- which is what lets ApplyStyle state it during initialize.
	SetWantsTick(bIsMarquee && Shape == EDreamProgressShape::Bar);
	ApplyPercent();
}

void UDreamProgressBar::NativeOnTick(float DeltaTime)
{
	Super::NativeOnTick(DeltaTime);
	if (!bIsMarquee || Shape != EDreamProgressShape::Bar)
	{
		return;
	}
	// Wrapped rather than left to grow: a bar left running for a long session would otherwise reach
	// the float range where a delta of a sixtieth of a second stops changing the number at all.
	const float Cycle = FMath::Max(MarqueeDuration, KINDA_SMALL_NUMBER);
	MarqueeTime = FMath::Fmod(MarqueeTime + DeltaTime, Cycle);
	ApplyPercent();
}

void UDreamProgressBar::ApplyShape(const FDreamProgressBarStyle& InActive)
{
	const bool bRadial = (Shape == EDreamProgressShape::Radial);
	// The thickness is stated as a fraction of half the ring's size, which is exactly what the rect's
	// Percentage unit mode means for a border width (Value = Percentage * min(w,h) * 0.5, see
	// UDreamRectBlock::FillData). So the style's number goes in untranslated; 1 is a full pie.
	const float Thickness = FMath::Clamp(InActive.RadialThickness, 0.0f, 1.0f);

	auto ShapeRect = [bRadial, Thickness](const UDreamWidget* InNode)
	{
		UDreamRectBlock* Rect = InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr;
		if (Rect == nullptr)
		{
			return;
		}
		if (bRadial)
		{
			// A circle: a Percentage radius of 1 is half the shorter side, whatever the widget is
			// arranged to, so the ring stays round without anyone re-pushing a pixel radius.
			Rect->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Percentage);
			Rect->SetCornerRadius(FVector4(1.0, 1.0, 1.0, 1.0));
			// THE RING. This primitive has exactly one hole in it -- the space a border does not
			// cover -- so a ring is the border alone with the body switched off. The cost of that is
			// honest and worth stating: a face BRUSH has nothing to draw on in Radial, because the
			// body is where an image would go. Brushes remain a Bar feature.
			Rect->SetEnableBody(false);
			Rect->SetEnableBorder(true);
			Rect->SetBorderWidthUnitMode(EDreamRectBlockUnitMode::Percentage);
			Rect->SetBorderWidth(Thickness);
			// White, explicitly. The style's colour is written to the VISUAL (the vertex colour that
			// multiplies everything the rect draws), which is the one writer both shapes share; the
			// border's own colour defaults to BLACK, and black times anything is a black ring.
			Rect->SetBorderColor(FColor::White);
		}
		else
		{
			// Back to a plain rounded rect. ShapeFace has already restated the Value-mode radius, so
			// what is left is undoing the ring -- and undoing it is not optional: a control that was
			// Radial a moment ago still has its body off and its border on.
			Rect->SetEnableBody(true);
			Rect->SetEnableBorder(false);
		}
		// Off in Bar for both nodes, and off in Radial for the TRACK: the track is the unfilled ring
		// and shows all the way round. ApplyPercent turns it back on for the fill alone, because
		// there the angle IS the value.
		Rect->SetEnableRadialFill(false);
	};

	ShapeRect(TrackNode);
	ShapeRect(FillNode);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ProgressBar", UDreamProgressBar)
