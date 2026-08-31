// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamProgressBar.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"

void UDreamProgressBar::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	using namespace DreamUI;

	// No layout container and no behaviour: the fill is anchor-driven geometry and the control is
	// the only thing that moves it. The fill starts stretched -- (0,0)-(1,1) with a zero SizeDelta,
	// which matters because anchors alone do not clear it -- and ApplyPercent narrows the
	// horizontal max from there.
	Realize(this,
		Node<UDreamRectBlock>("Track").Out(TrackNode)
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Fill").Out(FillNode).Stretch()));

	ApplyStyle();
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
	// The control's own height; placed in a stack this is what Auto measures. Width belongs to
	// whoever placed the control.
	SizeControlHeight(Active.Height);
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

void UDreamProgressBar::ApplyPercent()
{
	if (FillNode == nullptr)
	{
		return;
	}
	// Clamped where it becomes geometry, not where it is stored: the property keeps the author's
	// number (UMG does the same).
	const float Clamped = FMath::Clamp(Percent, 0.0f, 1.0f);
	// BOTH axes are absolute numbers read from the track's live arranged size, pushed on every
	// percent write. The anchor-ratio route asked the setter to resolve the parent's span at write
	// time, and on all but the occasional full-layout frame that resolution saw the track's
	// SizeDelta (zero -- it stretches) rather than its arranged width: the fill spent those frames
	// at zero width, rendered as a round dot walking the track (the flicker). The same family as
	// the dropdown list's stale width and this fill's own stale height: an anchor-driven child's
	// geometry is only as fresh as the last time ITS OWN data changed, so the control feeds it
	// values with no spans left to resolve. Pivot and point anchor sit on the track's left edge,
	// so the width grows rightward, exactly as the ratio anchor drew it.
	const FVector2D TrackSize = TrackNode != nullptr
		? FVector2D(TrackNode->GetWidth(), TrackNode->GetHeight())
		: FVector2D::ZeroVector;
	FillNode->SetPivot(FVector2D(0.0, 0.5));
	FillNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.5), FVector2D(0.0, 0.5), false, false);
	FillNode->SetAnchoredPositionAndSizeDelta(
		FVector2D::ZeroVector, FVector2D(Clamped * TrackSize.X, TrackSize.Y));
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ProgressBar", UDreamProgressBar)
