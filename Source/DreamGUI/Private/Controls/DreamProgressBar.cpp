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
	// number (UMG does the same), the anchors never leave the track.
	const float Clamped = FMath::Clamp(Percent, 0.0f, 1.0f);
	FillNode->SetHorizontalAnchorMinMax(FVector2D(0.0, Clamped), false, false);
	// The HEIGHT is absolute, not stretched: an anchor-driven child re-derives a stretched axis
	// only when its own anchor data changes, and the Y half of this fill's data never would -- a
	// track that grew back from a mid-layout zero left the fill's cached height at zero forever
	// (the dropdown list's stale-width case, one axis over). The track's height is always live,
	// and this runs on every percent push anyway.
	FillNode->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5), false, false);
	const float TrackHeight = TrackNode != nullptr ? static_cast<float>(TrackNode->GetHeight()) : 0.0f;
	FillNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D(0.0, TrackHeight));
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ProgressBar", UDreamProgressBar)
