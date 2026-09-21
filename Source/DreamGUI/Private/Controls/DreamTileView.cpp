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
	// The CROSS axis, not the width: a line runs across the scroll axis, so a horizontal tile view
	// packs its columns down the viewport's height. Everything below is the same arithmetic.
	const float ViewportCross = GetViewportCrossExtent();
	// The inset first: the tiles live inside the viewport's padding, so that is not space to fill.
	const float Usable = ViewportCross - GetCrossPadStart(Active.List) - GetCrossPadEnd(Active.List);
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
	// How many tiles this particular line actually holds: the LAST line is usually short, and every
	// alignment but the two "left/start" ones needs to know that before it can place anything.
	const int32 Line = InDisplayIndex / Columns;
	const int32 TileCount = VisibleItemIndices.Num();
	const int32 TilesOnLine = FMath::Clamp(TileCount - Line * Columns, 1, Columns);

	float TileExtent = Active.TileWidth;
	float LineStart = GetCrossPadStart(InStyle);
	float Pitch = GetTilePitch();
	ResolveLineLayout(TilesOnLine, TileExtent, LineStart, Pitch);
	const float CrossOffset = LineStart + Column * Pitch;
	const float MainOffset = GetRowTopOffset(InDisplayIndex);

	// A POINT anchor on both axes and absolute numbers, where the list stretches across: a tile has a
	// size of its own, and a stretched axis would hand it the whole line's. Pinned to the near corner
	// so both offsets are measured from the same one the display index counts from.
	if (IsHorizontalList())
	{
		InRow.SetPivot(FVector2D(0.0, 1.0));
		InRow.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
		// Cross is Y here and Y runs UP, so an offset from the TOP goes in negative.
		InRow.SetAnchoredPositionAndSizeDelta(
			FVector2D(MainOffset, -CrossOffset), FVector2D(InStyle.RowHeight, TileExtent));
	}
	else
	{
		InRow.SetPivot(FVector2D(0.0, 1.0));
		InRow.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
		InRow.SetAnchoredPositionAndSizeDelta(
			FVector2D(CrossOffset, -MainOffset), FVector2D(TileExtent, InStyle.RowHeight));
	}
}

void UDreamTileView::ResolveLineLayout(int32 InTilesOnLine, float& OutTileExtent, float& OutLineStart, float& OutPitch) const
{
	const FDreamTileViewStyle& Active = ResolveTileStyle();
	const FDreamListStyle& List = Active.List;
	const float Usable = FMath::Max(0.0f,
		GetViewportCrossExtent() - GetCrossPadStart(List) - GetCrossPadEnd(List));
	const int32 Columns = FMath::Max(1, ResolveColumnCount());
	const float Tile = Active.TileWidth;
	const float Gap = Active.TileSpacing;
	// What the line as authored actually occupies: N tiles and N-1 gaps, never N pitches -- the gap
	// goes BETWEEN tiles, so counting a trailing one is what pushes the last tile over the edge.
	const float Occupied = InTilesOnLine * Tile + FMath::Max(0, InTilesOnLine - 1) * Gap;
	const float Slack = FMath::Max(0.0f, Usable - Occupied);

	switch (TileAlignment)
	{
	case EDreamTileAlignment::LeftAligned:
		// The answer this control has always given, and the default for that reason.
		break;
	case EDreamTileAlignment::RightAligned:
		OutLineStart += Slack;
		break;
	case EDreamTileAlignment::CenterAligned:
		OutLineStart += Slack * 0.5f;
		break;
	case EDreamTileAlignment::EvenlyDistributed:
		// The slack goes into the GAPS, so the tiles keep their authored size and the two end tiles
		// stay flush with the edges. One tile has no gap to spend it in, so it simply centres.
		if (InTilesOnLine > 1)
		{
			OutPitch = Tile + Gap + Slack / (InTilesOnLine - 1);
		}
		else
		{
			OutLineStart += Slack * 0.5f;
		}
		break;
	case EDreamTileAlignment::EvenlySize:
		// The slack goes into the TILES, and the count is the one the AUTHORED size fits -- so every
		// line has the same number of tiles and they are all a little bigger than asked for. The last
		// line's short count is deliberately ignored here, or its tiles would be a different size
		// from every other line's and the grid would stop looking like one.
		if (Columns > 0)
		{
			OutTileExtent = (Usable - FMath::Max(0, Columns - 1) * Gap) / Columns;
			OutPitch = OutTileExtent + Gap;
		}
		break;
	case EDreamTileAlignment::EvenlyWide:
		// The slack goes into the PITCH without touching the tile: the tiles keep their size and
		// float in equal cells, which is the difference from EvenlyDistributed -- the first tile is
		// no longer flush with the edge.
		if (Columns > 0)
		{
			OutPitch = Usable / Columns;
			OutLineStart += (OutPitch - Tile) * 0.5f;
		}
		break;
	case EDreamTileAlignment::Fill:
		// Like EvenlySize, but honouring THIS line's count: the last line's tiles stretch to fill the
		// width on their own. Which is what "fill" says, and why it is a separate answer.
		if (InTilesOnLine > 0)
		{
			OutTileExtent = (Usable - FMath::Max(0, InTilesOnLine - 1) * Gap) / InTilesOnLine;
			OutPitch = OutTileExtent + Gap;
		}
		break;
	}
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

void UDreamTileView::SetTileAlignment(EDreamTileAlignment InAlignment)
{
	if (TileAlignment == InAlignment)
	{
		return;
	}
	TileAlignment = InAlignment;
	// Where every tile sits, not how many fit: the column count is decided by the AUTHORED size and
	// does not move, so this re-places rather than re-flows.
	ApplyStyle();
}

int32 UDreamTileView::GetNavigationTarget(int32 InDisplayIndex, EDreamUINavigationDirection InDirection) const
{
	const int32 Count = VisibleItemIndices.Num();
	if (InDisplayIndex < 0 || InDisplayIndex >= Count)
	{
		return INDEX_NONE;
	}
	const int32 Columns = FMath::Max(1, ResolveColumnCount());
	const int32 Line = InDisplayIndex / Columns;
	const int32 Column = InDisplayIndex % Columns;

	// Which screen direction is "along a line" depends on the orientation: a vertical tile view's
	// lines run across, a horizontal one's run down. Asked once, here, so the four cases below are
	// about the GRID rather than about the screen.
	const bool bHorizontal = IsHorizontalList();
	const bool bAlongLine = bHorizontal
		? (InDirection == EDreamUINavigationDirection::Up || InDirection == EDreamUINavigationDirection::Down)
		: (InDirection == EDreamUINavigationDirection::Left || InDirection == EDreamUINavigationDirection::Right);
	const bool bForward = InDirection == EDreamUINavigationDirection::Right
		|| InDirection == EDreamUINavigationDirection::Down
		|| InDirection == EDreamUINavigationDirection::Next;

	if (InDirection == EDreamUINavigationDirection::Next || InDirection == EDreamUINavigationDirection::Prev)
	{
		// Source order, which already runs through the lines: Next off the end of one line IS the
		// first tile of the next, with no wrapping question to answer.
		const int32 Target = InDisplayIndex + (bForward ? 1 : -1);
		return (Target >= 0 && Target < Count) ? Target : INDEX_NONE;
	}

	if (bAlongLine)
	{
		const int32 NextColumn = Column + (bForward ? 1 : -1);
		if (NextColumn >= 0 && NextColumn < Columns)
		{
			const int32 Target = Line * Columns + NextColumn;
			// Still inside the line, but a short last line can end before the column count does.
			return Target < Count ? Target : INDEX_NONE;
		}
		if (!bWrapHorizontalNavigation)
		{
			return INDEX_NONE;
		}
		// Off the end of a line, and asked to wrap: the first tile of the NEXT line going forward,
		// the last tile of the PREVIOUS one going back. This is the step the geometric navigator
		// cannot make, because the tile it lands on is at the opposite edge of the control.
		if (bForward)
		{
			const int32 Target = (Line + 1) * Columns;
			return Target < Count ? Target : INDEX_NONE;
		}
		if (Line == 0)
		{
			return INDEX_NONE;
		}
		// The last tile of the previous line is always full, because only the LAST line can be short.
		return Line * Columns - 1;
	}

	// Across the lines: plus or minus a whole line, and a short last line means the tile directly
	// there may not exist -- in which case the line's last tile is the honest answer rather than
	// nothing, the same way a grid's bottom row behaves everywhere else.
	const int32 TargetLine = Line + (bForward ? 1 : -1);
	if (TargetLine < 0)
	{
		return INDEX_NONE;
	}
	const int32 Target = TargetLine * Columns + Column;
	if (Target < Count)
	{
		return Target;
	}
	const int32 LastOnTargetLine = FMath::Min(TargetLine * Columns + Columns, Count) - 1;
	return LastOnTargetLine >= TargetLine * Columns ? LastOnTargetLine : INDEX_NONE;
}

void UDreamTileView::SetStyle(const FDreamTileViewStyle& InStyle)
{
	Style = InStyle;
	// A tile's size decides how many fit across, which decides how many lines there are and how tall
	// the scrolled column is. All of that is settled in the style push.
	ApplyStyle();
}

float UDreamTileView::GetEntryHeight() const
{
	// The RESOLVED style, not the inline one: a tile view wearing the project sheet is drawn at the
	// sheet's size, and answering with the untouched inline field would describe tiles nobody sees.
	return ResolveTileStyle().List.RowHeight;
}

void UDreamTileView::SetEntryHeight(float InHeight)
{
	Style.List.RowHeight = FMath::Max(0.0f, InHeight);
	// Written onto the inline style AND switched to it: a size pushed at runtime is this instance's
	// own answer, and leaving StyleSource pointing at the sheet would mean the next style push
	// quietly threw the number away.
	StyleSource = EDreamUIStyleSource::Inline;
	ApplyStyle();
}

float UDreamTileView::GetEntryWidth() const
{
	return ResolveTileStyle().TileWidth;
}

void UDreamTileView::SetEntryWidth(float InWidth)
{
	Style.TileWidth = FMath::Max(0.0f, InWidth);
	StyleSource = EDreamUIStyleSource::Inline;
	// A different width is a different column count, so this re-flows rather than repaints.
	ApplyStyle();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TileView", UDreamTileView)
