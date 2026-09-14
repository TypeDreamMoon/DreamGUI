// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "DreamTileView.generated.h"

/**
 * A tile view whose hierarchy is code, not an asset: a list whose rows are laid out across as well
 * as down.
 *
 * UMG's TileView derives from its ListView and adds an entry WIDTH; this is the same relationship,
 * for the same reason. Everything that makes a list -- the viewport, the bar, the scrolled column,
 * the row template, the pool, the selection, the recycling threshold -- is UDreamListViewBase's and
 * is not restated here. What a tile view adds is one question answered differently: how many rows sit
 * side by side.
 *
 * THE COLUMN COUNT IS MEASURED, NOT AUTHORED
 * ------------------------------------------
 * UMG's tile view fits as many entries across as the width allows and re-flows when it changes, and
 * so does this one: ResolveColumnCount divides the viewport by the tile pitch. An authored count
 * would be a second number that has to agree with the width, and the first resize would end that
 * agreement -- the same equation-in-two-places the row placement was rewritten to remove.
 *
 * A floor of one, always: a viewport narrower than a tile shows one tile per line and clips it,
 * which is visible and recoverable, where zero columns is a division by zero and a blank control.
 *
 * WHAT A TILE'S SIZE IS
 * ---------------------
 * Its height is FDreamListStyle::RowHeight -- the SAME field a list's row height comes from, because
 * it is the same measurement -- and its width is FDreamTileViewStyle::TileWidth. There is no aspect
 * lock and no "square" flag: two numbers already say every shape those flags could, and a lock would
 * make one of the two unreadable in the panel.
 *
 * See UDreamListViewBase for the rest, and UDreamListView for the flat case.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Tile View")
class DREAMGUI_API UDreamTileView : public UDreamListViewBase
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile View")
	FDreamTileViewStyle Style;

	/**
	 * How many tiles fit across the viewport right now: the measured answer the layout is using.
	 *
	 * Public because it is the one thing about a tile view a consumer cannot work out for itself --
	 * the viewport's width is the control's business -- and because a test that cannot see it can
	 * only assert positions, which is the same claim written less clearly.
	 */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	int32 GetColumnCount() const { return ResolveColumnCount(); }

	/** How many LINES of tiles the source makes at the current column count. */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	int32 GetRowLineCount() const { return GetLineCount(); }

protected:
	virtual FDreamListStyle ResolveListStyle() const override;
	virtual int32 ResolveColumnCount() const override;
	virtual void PlaceRow(UDreamWidget& InRow, int32 InDisplayIndex, const FDreamListStyle& InStyle) override;

private:
	/** The whole style. ResolveListStyle hands the base the List half of this same answer. */
	FDreamTileViewStyle ResolveTileStyle() const;

	/** One step ACROSS a line: the tile's width plus the gap beside it, never zero. */
	float GetTilePitch() const;
};
