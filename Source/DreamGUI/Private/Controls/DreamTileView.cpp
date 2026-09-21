// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTileView.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/Components/DreamWidget.h"

float UDreamTileView::GetTilePitch() const
{
	const FDreamTileViewStyle& Active = ResolveTileStyle();
	// Never zero: a pitch of zero would put every tile on top of every other one and make the column
	// arithmetic divide by it. The list's GetRowPitch says the same thing about the other axis.
	return FMath::Max(Active.TileWidth + Active.TileSpacing, KINDA_SMALL_NUMBER);
}

int32 UDreamTileView::ResolveColumnCount() const
{
	const FDreamTileViewStyle& Active = ResolveTileStyle();
	const float ViewportWidth = ViewportNode != nullptr ? static_cast<float>(ViewportNode->GetWidth()) : 0.0f;
	// The inset first: the tiles live inside the viewport's padding, so that is not width to fill.
	const float Usable = ViewportWidth - Active.List.Padding.Left - Active.List.Padding.Right;
	if (Usable <= KINDA_SMALL_NUMBER)
	{
		// A viewport nothing has arranged and nobody authored -- the first frame of a control a
		// consumer's layout has not reached yet. One column is the answer that is never WRONG, only
		// narrow: every tile still has a place, the scroll range is still right for what is drawn,
		// and the dimensions handler re-asks the moment there is a real width.
		return 1;
	}
	// The gap goes BETWEEN tiles, so N tiles occupy N*Width + (N-1)*Spacing -- which is N pitches
	// less one gap. Solving for N and flooring is the count that fits without the last tile hanging
	// over the edge.
	const int32 Fits = FMath::FloorToInt((Usable + Active.TileSpacing) / GetTilePitch());
	return FMath::Max(1, Fits);
}

void UDreamTileView::PlaceRow(UDreamWidget& InRow, int32 InDisplayIndex, const FDreamListStyle& InStyle)
{
	const FDreamTileViewStyle& Active = ResolveTileStyle();
	const int32 Columns = FMath::Max(1, ResolveColumnCount());
	const int32 Column = InDisplayIndex % Columns;

	// A POINT anchor on both axes and absolute numbers, where the list stretches across: a tile has a
	// width of its own, and a stretched axis would hand it the column's. Pinned to the column's
	// TOP-LEFT so both offsets are measured from the same corner the display index counts from.
	InRow.SetPivot(FVector2D(0.0, 1.0));
	InRow.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
	InRow.SetAnchoredPositionAndSizeDelta(
		FVector2D(InStyle.Padding.Left + Column * GetTilePitch(), -GetRowTopOffset(InDisplayIndex)),
		FVector2D(Active.TileWidth, InStyle.RowHeight));
}

FDreamListStyle UDreamTileView::ResolveListStyle() const
{
	// The list half of the tile view's own style -- the reason FDreamTileViewStyle embeds a whole
	// FDreamListStyle instead of restating its fields. BY VALUE, like everything downstream of
	// ResolveStyle: a resolved style can be a MERGE of the sheet's and this instance's, which is a
	// value neither of them stores.
	return ResolveTileStyle().List;
}

FDreamTileViewStyle UDreamTileView::ResolveTileStyle() const
{
	return ResolveStyle(Style, &UDreamUIStyleSheet::TileViewStyle);
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TileView", UDreamTileView)
