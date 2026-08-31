// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamLayoutSelfAuthoredSurface.h"

#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"

FVector2f UDreamLayoutSelfAuthoredSurface::GetLayoutPreferredSize() const
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return FVector2f::ZeroVector;
	}
	// The slot's snapshot, when the widget sits in a panel: the one record of authored intent that
	// an arrange pass cannot overwrite. The widget's live anchor data is no substitute there --
	// ApplyChildRect writes layout output straight into it.
	if (const UDreamPanelSlot* Slot = Widget->GetPanelSlot(); IsValid(Slot) && Slot->HasAuthoredGeometry())
	{
		return Slot->GetAuthoredDesiredSizeFallback();
	}
	// No slot, so nothing ever overwrote the anchor data: a point-anchored axis's SizeDelta is the
	// authored size itself, and a stretched axis's is a DELTA against a span somebody else owns --
	// no statement, which zero already means here.
	const FDreamUIAnchorData& Anchors = Widget->GetAnchorData();
	return FVector2f(
		Anchors.IsHorizontalStretched() ? 0.0f : FMath::Max(0.0f, static_cast<float>(Anchors.SizeDelta.X)),
		Anchors.IsVerticalStretched() ? 0.0f : FMath::Max(0.0f, static_cast<float>(Anchors.SizeDelta.Y)));
}

FDreamLayoutControlAnchorData UDreamLayoutSelfAuthoredSurface::GetLayoutControlAnchor(const UDreamWidget* Widget) const
{
	FDreamLayoutControlAnchorData Result;
	if (Widget == GetWidget())
	{
		// Claim only the axes the author actually stated. The measure's override gate accepts zero
		// as a value, so an unclaimed axis must be declared uncontrolled here rather than answered
		// with zero -- see the header on the ImageSize rule.
		const FVector2f Preferred = GetLayoutPreferredSize();
		Result.bCanControlHorizontalSize = Preferred.X > 0.0f;
		Result.bCanControlVerticalSize = Preferred.Y > 0.0f;
	}
	return Result;
}
