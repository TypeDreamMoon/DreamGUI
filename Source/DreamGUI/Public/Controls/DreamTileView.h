// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "Event/DreamPointerEventData.h"
#include "DreamTileView.generated.h"

/**
 * How a line's tiles are distributed across the width it has -- UMG's EListItemAlignment, with its
 * seven answers and its names.
 *
 * The distinction worth holding on to is WHAT each one spends the leftover space on. Three of them
 * move the line without changing it (Left/Right/Center), two put the slack into the gaps or the
 * cells and leave the tile alone (EvenlyDistributed, EvenlyWide), and two grow the tile itself
 * (EvenlySize, Fill) -- differing only in whether a short last line grows with the rest.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTileAlignment : uint8
{
	/** Slack into the gaps; the end tiles stay flush with both edges. One tile centres instead. */
	EvenlyDistributed,
	/** Slack into the TILES, at the count the authored size fits -- every line's tiles the same size. */
	EvenlySize,
	/** Slack into equal CELLS; the tile keeps its size and floats in the middle of its cell. */
	EvenlyWide,
	/** The line packs against the near edge and the slack sits after it. What this control has always done. */
	LeftAligned,
	/** The line packs against the far edge. */
	RightAligned,
	/** The line is centred and the slack is split between both ends. */
	CenterAligned,
	/** Like EvenlySize, but a short last line stretches to fill the width on its own. */
	Fill,
};

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Tile View")
	FDreamTileViewStyle Style;

	/**
	 * How a line's tiles are distributed across the width -- UMG's TileAlignment.
	 *
	 * LeftAligned, not UMG's EvenlyDistributed: it is what every tile view in this library has drawn,
	 * and a grid that silently re-spaced itself on upgrade would be a layout change nobody asked for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTileAlignment", BlueprintSetter = "SetTileAlignment", Category = "Tile View")
	EDreamTileAlignment TileAlignment = EDreamTileAlignment::LeftAligned;

	/**
	 * Whether stepping off the end of a line lands on the next one -- UMG's bWrapHorizontalNavigation.
	 *
	 * Answered by INDEX rather than by geometry: "the tile after this one" is a fact about the
	 * source, and the geometric navigator would have to infer it from rects that say nothing about
	 * which line they are on. A navigation press on a tile is answered by GetNavigationTarget, so this
	 * reaches the player as well as the API. Off by default, as UMG's is: a press off the end of a line
	 * then leaves the tile view for whatever is beside it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetWrapHorizontalNavigation", BlueprintSetter = "SetWrapHorizontalNavigation", Category = "Tile View")
	bool bWrapHorizontalNavigation = false;

	UFUNCTION(BlueprintPure, Category = "Tile View")
	EDreamTileAlignment GetTileAlignment() const { return TileAlignment; }

	/** Changes where every tile sits without changing how many fit, so it re-places rather than re-flows. */
	UFUNCTION(BlueprintCallable, Category = "Tile View")
	void SetTileAlignment(EDreamTileAlignment InAlignment);

	UFUNCTION(BlueprintPure, Category = "Tile View")
	bool GetWrapHorizontalNavigation() const { return bWrapHorizontalNavigation; }

	UFUNCTION(BlueprintCallable, Category = "Tile View")
	void SetWrapHorizontalNavigation(bool bInWrap) { bWrapHorizontalNavigation = bInWrap; }

	/**
	 * The display index one step in a direction from InDisplayIndex, or INDEX_NONE for "off the grid".
	 *
	 * The tile view's own answer, because a grid's neighbours are arithmetic: across is plus or minus
	 * one WITHIN a line, down is plus or minus the column count. Wrapping is what happens at a line's
	 * ends when bWrapHorizontalNavigation is on -- and it is exactly the step the geometric navigator
	 * cannot make, because the tile it should land on is at the opposite edge of the control.
	 *
	 * What a navigation press on a tile steps by, for the four directions STileView and SListView
	 * answer (Next and Prev are left to the ordinary scan): the tile it names is selected while
	 * bSelectItemOnNavigation is on and scrolled into view, and INDEX_NONE hands the press on.
	 */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	int32 GetNavigationTarget(int32 InDisplayIndex, EDreamUINavigationDirection InDirection) const;

	/**
	 * Where a line starts, how big its tiles are and how far apart -- all three at once, because
	 * TileAlignment moves them together and answering them separately is how two of the seven modes
	 * end up disagreeing about the same leftover space.
	 *
	 * Public because it is the one piece of the alignment work that is a pure function of the style
	 * and a tile count, and is therefore the piece worth pinning exactly. In and out: the caller
	 * seeds the three with the authored answer and this rewrites whichever the mode moves.
	 */
	void ResolveLineLayout(int32 InTilesOnLine, float& OutTileExtent, float& OutLineStart, float& OutPitch) const;

	/** By value, not by reference: a UFUNCTION return has to be a value, and a style is a small struct. */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	FDreamTileViewStyle GetStyle() const { return Style; }

	/** Replace the whole look and re-push it. A tile's size is style, so this rebuilds the tiles. */
	UFUNCTION(BlueprintCallable, Category = "Tile View")
	void SetStyle(const FDreamTileViewStyle& InStyle);

	/**
	 * A tile's height -- UMG's EntryHeight, which is FDreamListStyle::RowHeight here because it is
	 * the same measurement a list's row height is. One tile, one size, and the style holds it.
	 */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	float GetEntryHeight() const;

	UFUNCTION(BlueprintCallable, Category = "Tile View")
	void SetEntryHeight(float InHeight);

	/** A tile's width -- UMG's EntryWidth, which is FDreamTileViewStyle::TileWidth. */
	UFUNCTION(BlueprintPure, Category = "Tile View")
	float GetEntryWidth() const;

	/** Changes how many tiles fit across, so it re-flows rather than repaints. */
	UFUNCTION(BlueprintCallable, Category = "Tile View")
	void SetEntryWidth(float InWidth);

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
	/** The grid's answer -- GetNavigationTarget -- for Left, Right, Up and Down; nothing for the rest. */
	virtual int32 ResolveNavigationTarget(int32 InDisplayIndex, EDreamUINavigationDirection InDirection) const override;

private:
	/** The whole style. ResolveListStyle hands the base the List half of this same answer. */
	FDreamTileViewStyle ResolveTileStyle() const;

	/** One step ACROSS a line: the tile's width plus the gap beside it, never zero. */
	float GetTilePitch() const;
};
