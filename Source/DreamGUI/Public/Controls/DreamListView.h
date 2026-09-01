// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Interaction/UIScrollView.h"
#include "DreamListView.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamListSelectionChangedEvent, int32, SelectedIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamListRowEvent, int32, ItemIndex, UDreamWidget*, Row, UObject*, Item);

/**
 * Everything a list and a tree have in common, which is nearly all of it.
 *
 * A tree IS a list that indents, and this class is written to be exactly that: the viewport, the
 * bar, the scrolled column, the row template, the row POOL, the row colours and the one selected
 * index live here, and each concrete control adds only what makes it itself -- its typed style,
 * and (for the tree) a depth per item, a per-row indent and a twisty. FDreamTreeViewStyle is built
 * the same way round, carrying a whole FDreamListStyle rather than restating its fields.
 *
 * This class is abstract and holds NO style property. That is deliberate, and it is the reason
 * there is a base class at all rather than UDreamTreeView deriving from UDreamListView: the
 * family's contract is that a control's look is ONE decision (see UDreamUIControl), and a tree
 * inheriting a spare FDreamListStyle it never resolves would be a second source of truth wearing
 * the same name as the real one. Each subclass declares `Style`, typed, and answers
 * ResolveListStyle() with the list half of it.
 *
 * THE SHAPE
 * ---------
 * A face carrying the look, a viewport clipped inside it holding the scroll behaviour, the column
 * that slides within the viewport, and a bar along the viewport's edge -- UDreamScrollBox's
 * arrangement, and for its reasons. Two of them are worth restating because they are not obvious:
 * the behaviour sits on the VIEWPORT and the bar is the viewport's SIBLING, because a scroll view
 * accepts drags from anywhere inside its own widget, so a bar hung underneath it would scroll the
 * content every time the handle was grabbed; and the column's height is the control's statement of
 * how far there is to scroll, never layout output.
 *
 * ROWS ARE PLACED, NOT ARRANGED
 * -----------------------------
 * The column holds NO layout container and every row states its own rect: top-anchored, stretched
 * across, and offset by its index times the row pitch. Three things follow, and all three matter:
 *
 *  - a row's height is the style's RowHeight, full stop. The previous shape put the rows in a
 *    vertical box on equal-weight Fill slots and then authored the COLUMN's height to exactly
 *    rows*RowHeight + gaps so the box would divide it back out -- an equation solved in two places
 *    that had to agree, and which stopped agreeing the moment anything measured a row's content;
 *  - placing row N costs nothing and depends on no sibling, which is what makes recycling possible
 *    at all: a box arranges every child it has, so a box can never show a window onto a million;
 *  - and the answer is available with no layout pass at all, which is what a headless test and the
 *    designer's first frame actually see.
 *
 * VIRTUALIZATION IS A THRESHOLD, NOT A MODE
 * -----------------------------------------
 * Under VirtualizationThreshold items there is one row widget per item and GetRowWidget answers for
 * every index -- the contract this control has always had, and the one that makes it correct in a
 * headless test where no viewport has been arranged. At or above it, the pool is sized to the
 * window plus an overscan and rows are re-bound as the view moves, which is what UMG's ListView
 * does and the only way a hundred thousand rows is anything but a hang. GetRowWidget then answers
 * for realized rows and null for the rest, which is UMG's contract too.
 *
 * WHY THE PLAIN SCROLL VIEW, NOT UUIListView
 * ------------------------------------------
 * The recycling stack (UUIRecyclableScrollView, and UUIListView on top of it) is the right answer
 * for a data source and the wrong one for a control, for reasons that are all in those files:
 *
 *  - its unit is a UObject* item, because IUIRecyclableScrollViewDataSource is a UObject protocol.
 *    A Native.List's natural item is a line of text -- the same call FDreamDropdownStyle's options
 *    make -- and feeding the recycler would mean minting a UObject per label;
 *  - a cell only exists after Start(), which needs a live world, a registered tree AND an arranged
 *    viewport: InitializeOnDataSource sizes the cell pool from the content parent's local-space
 *    extents. A control has to be correct the instant its properties are set;
 *  - and UUIListView::Awake hands its data-source seat to whoever claimed it first, which is how an
 *    `each` block's adapter gets in. A control hosting one would be a third party to that seat.
 *
 * So the rows are built here, from a template, into a column inside a plain UUIScrollView -- which
 * is what UDreamDropdown's option list does, because it is the same problem.
 *
 * HOW THIS RELATES TO `each`
 * --------------------------
 * They are the two halves of the same need and they do not compete:
 *
 *   `each Track in Tracks { ... }` is the LANGUAGE route. It compiles a repeated subtree into the
 *   generated class, feeds a host recyclable view through UDreamUIEachAdapter, and binds each row's
 *   fields to the item's members (`Text <- Track.Title`). You get arbitrary row shape and per-field
 *   bindings, and you pay for it with a .dui class to live in and a row you have to draw yourself.
 *
 *   `Native.List` is the CONTROL route. One tag, two properties, and you have a list that looks
 *   like every other list in the project because its rows come from the style sheet.
 *
 * Reach for `each` when the ROW is the interesting part. Reach for Native.List when the LIST is.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamListViewBase : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * The rows, as text. The common case, and the control's job is to be the common case -- the same
	 * call UDreamDropdown's Options make. Parallel to ItemObjects when both are given: the object
	 * carries the data, this carries what the built-in row says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	TArray<FText> Items;

	/**
	 * The rows, as objects. Non-empty, this decides the row count and each row's item; the label
	 * then comes from the matching entry of Items, or from the object's name when there is none.
	 * This is the seat UUIListView's own Items array occupies, kept so a consumer that already
	 * models its rows as UObjects does not have to unpack them into text first.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	TArray<TObjectPtr<UObject>> ItemObjects;

	/**
	 * A row's CONTENT, authored elsewhere: one instance of this class is created inside every row
	 * widget, filling it, and the built-in label steps aside. The row's face, height, hover and
	 * selection stay the control's, so a template only has to draw an item.
	 *
	 * Created once per POOL row rather than per item, which is what makes it survive recycling:
	 * OnRowGenerated fires on every bind, and that is where a consumer updates it.
	 *
	 * Null (the default) is the built-in label row. Instancing a user widget needs a world, so with
	 * none this quietly stays the built-in row rather than producing half a list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	TSubclassOf<UDreamUserWidget> RowTemplateClass;

	/**
	 * The selected row, as an index into the SOURCE -- not into the rows on screen. For a list the
	 * two are the same; for a tree they are not, and an index that survives a collapse is the one
	 * worth handing to a binding. -1 is none.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	int32 SelectedIndex = INDEX_NONE;

	/** Every other row wears FDreamListStyle::RowAlternate. Off is the dense list UMG draws. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	bool bAlternatingRowColors = false;

	/** Off means no bar at all, and the viewport keeps the gutter it would have cost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	bool bShowScrollBar = true;

	/** Whether the bar stays put or disappears while every row already fits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List", meta = (EditCondition = "bShowScrollBar"))
	EDreamScrollBoxScrollbarVisibility ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::AutoHide;

	/**
	 * Above this many rows the list recycles a window of widgets instead of building one per item.
	 *
	 * A threshold rather than a switch because the two behaviours are each right somewhere: below
	 * it every index has a widget, which is what a headless test, a designer preview and a consumer
	 * asking "give me the widget for item 7" all want; above it a list of a hundred thousand is a
	 * pool of thirty. Zero recycles always; a very large number never does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List", meta = (ClampMin = "0"))
	int32 VirtualizationThreshold = 200;

	/** Extra rows kept realized past each edge of the window, so a fast scroll never shows a gap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List", meta = (ClampMin = "0", ClampMax = "16"))
	int32 VirtualizationOverscan = 2;

	/** Re-broadcast from the rows, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListSelectionChangedEvent OnSelectionChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact name,
	 * so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListSelectionChangedEvent OnValueChangedBP;

	/**
	 * One per row, every time it is BOUND to an item -- which, while recycling, is every time that
	 * row comes back round to a new item rather than once in its life. The hook for a consumer whose
	 * rows are richer than a label but who would rather not author a whole class: everything under
	 * the row is reachable from here by display name.
	 */
	UPROPERTY(BlueprintAssignable, Category = "List")
	FDreamListRowEvent OnRowGenerated;

	/** The face: the list's own look, and what cuts everything off at its rounded edge. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> FaceNode = nullptr;

	/** The window the rows slide behind, and where the scroll behaviour lives. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> ViewportNode = nullptr;

	/** The scrolled content: as tall as ALL rows while the viewport shows only what fits. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> ColumnNode = nullptr;

	/**
	 * The thing rows are copied from -- authored once, inactive, never drawn itself. Public because
	 * it is half of the "hand it a template" story: anything added under this node before the first
	 * rebuild rides into every row.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> RowTemplateNode = nullptr;

	/** The template's label. Every row has a copy of it, found by display name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamWidget> RowLabelNode = nullptr;

	/** A real Native.ScrollBar, so the handle geometry the anchor rule dictates exists once. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UDreamScrollBar> ScrollBarNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TObjectPtr<UUIScrollView> ScrollBehaviour = nullptr;

	/**
	 * The row widgets that exist, in pool order -- which is visual order only while the list is not
	 * recycling. RowSourceIndices says what each one is currently showing.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TArray<TObjectPtr<UDreamWidget>> RowNodes;

	/** Parallel to RowNodes: which source item each pool row stands for, or -1 while it is parked. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TArray<int32> RowSourceIndices;

	/** Replace the text source and rebuild. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetItems(const TArray<FText>& InItems);

	/** Replace the object source and rebuild. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetItemObjects(const TArray<UObject*>& InItems);

	/** How many items the source holds -- objects if it has any, texts otherwise. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetItemCount() const;

	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** Moves the highlight and fires both selection events. Out of range selects nothing. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectedIndex(int32 InIndex);

	/** The same move, silently: for pushing an authored value in, which is not the user choosing. */
	UFUNCTION(BlueprintCallable, Category = "List")
	void SetSelectedIndexWithoutNotify(int32 InIndex);

	/**
	 * How many rows the list is SHOWING -- for a tree the visible count, which is the point of a
	 * tree. Not the number of widgets: see GetRealizedRowCount for that.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRowCount() const { return VisibleItemIndices.Num(); }

	/** How many row widgets exist. Equal to GetRowCount until the list starts recycling. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRealizedRowCount() const { return RowNodes.Num(); }

	/** True while the list is showing a window of widgets rather than one per item. */
	UFUNCTION(BlueprintPure, Category = "List")
	bool IsVirtualizing() const { return bVirtualizing; }

	/**
	 * The row standing for a source item, or null when that item has no row right now -- collapsed
	 * under a tree node, gone from the source, or (while recycling) scrolled out of the window.
	 */
	UFUNCTION(BlueprintPure, Category = "List")
	UDreamWidget* GetRowWidget(int32 InItemIndex) const;

	/** Which source item a POOL row is currently showing, or -1 while it is parked. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRowItemIndex(int32 InPoolIndex) const;

	/**
	 * Scroll the least distance that brings an item's row fully into the viewport.
	 *
	 * Computed from the row pitch rather than from a widget, so it answers for an item whose row is
	 * not realized -- which is the only version of this that means anything while recycling.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	bool ScrollItemIntoView(int32 InItemIndex, bool bInAnimate = true);

	/**
	 * Throw the rows away and build them again from the source. Called for you by ApplyStyle -- row
	 * geometry and row colour are both style, so there is no such thing as re-styling without it --
	 * and by every setter that moves the source.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RebuildRows();

	/** Re-resolve the gutter, the bar and the realized window after this control is resized. */
	void HandleDimensionsChanged(bool bPivotChanged, bool bWidthChanged, bool bHeightChanged);

private:
	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	virtual void OnPartsReady() override;

	/**
	 * The list half of this control's style. Each concrete control resolves its own family and hands
	 * back a reference into it -- the sheet's entry or this instance's Style, both of which outlive
	 * the call. The base never sees which.
	 */
	virtual FDreamListStyle ResolveListStyle() const;

	/** Which source items get rows, in order. All of them, for a flat list. */
	virtual void CollectVisibleItemIndices(TArray<int32>& OutIndices) const;

	/** A source item's depth. Always zero here; the tree is the only thing that indents. */
	virtual int32 GetItemDepth(int32 InItemIndex) const { return 0; }

	/**
	 * How far in a row's content starts, on top of the row's own padding. The base writes it into
	 * the label's slot; what it MEANS is the subclass's (the tree makes it depth times indent, plus
	 * room for the twisty).
	 */
	virtual float GetRowContentInset(int32 InItemIndex) const { return 0.0f; }

	/** Add to the row template, once, before the first row is copied from it. */
	virtual void DecorateRowTemplate(UDreamWidget& InTemplate) {}

	/**
	 * A pool row was just created. ONCE in that widget's life, which is what anything permanent
	 * belongs in -- a click handler above all: subscribing from DecorateRow instead would add one
	 * more every time the row came round to another item.
	 *
	 * A handler that needs to know which item it is acting on asks GetRowItemIndex(InPoolIndex) at
	 * the moment it fires, because the answer changes underneath it.
	 */
	virtual void DecorateNewRow(UDreamWidget& InRow, int32 InPoolIndex) {}

	/** A pool row was just bound to an item. Runs after the base has skinned, sized and placed it. */
	virtual void DecorateRow(UDreamWidget& InRow, int32 InPoolIndex, int32 InItemIndex) {}

	/** The label a row shows: the matching text, else the item object's name, else nothing. */
	FText GetItemLabel(int32 InItemIndex) const;

	/** The object a row stands for, if the source has one. */
	UObject* GetItemObject(int32 InItemIndex) const;

	/** Re-push every row's resting colour without rebuilding anything. What selection actually moves. */
	void RefreshRowColors();

	/** Between a row's edge and its content -- the row's own inset, not the viewport's Padding. */
	static FMargin GetRowPadding();

	/** Row height plus the gap under it: what one step down the column costs. */
	float GetRowPitch() const;

	/** Where a row sits in the column, as an offset from the column's top edge. */
	float GetRowTopOffset(int32 InDisplayIndex) const;

	/** The display positions of the items the source is showing, in order. */
	UPROPERTY(Transient)
	TArray<int32> VisibleItemIndices;

private:
	void HandleRowClicked(int32 InPoolIndex);
	void HandleScrollViewMoved(FVector2D InProgress);

	/** Duplicate one row widget out of the template and wire what it keeps for life. */
	UDreamWidget* CreatePoolRow(int32 InPoolIndex);

	/** Point a pool row at an item: label, colour, inset, rect, and both decoration hooks. */
	void BindRow(int32 InPoolIndex, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle);

	/** Put a pool row to sleep: no item, no draw, no place in anything. */
	void ParkRow(int32 InPoolIndex);

	void ApplyRowColor(UDreamWidget* InRow, int32 InDisplayIndex, int32 InItemIndex, const FDreamListStyle& InStyle);

	/** Grow or shrink the pool to exactly this many widgets. */
	void ResizePool(int32 InPoolSize);

	/** Bind the pool to whatever the current scroll offset makes visible. The recycling pass. */
	void RefreshVisibleWindow();

	/** The gutter, the bar's rect and the scroll range -- everything that follows from the row count. */
	void RefreshScrollFurniture(const FDreamListStyle& InStyle);

	/** True while the bar has something to say: shown at all, and either permanent or overflowing. */
	bool ShouldShowScrollBar() const;

	/** How many widgets the window needs: the viewport's worth, plus overscan at both edges. */
	int32 ResolveWindowSize() const;

	/** Set while the pool is a window onto the source rather than a widget per item. */
	UPROPERTY(Transient)
	bool bVirtualizing = false;

	/**
	 * The authored row class the pool was built with. A rebind can carry a new style into an existing
	 * row but not a new CLASS -- the instance lives inside the row and is made once per pool row --
	 * so this is one of the two things that still forces a teardown. See RebuildRows for why a
	 * teardown is worth avoiding at all.
	 */
	UPROPERTY(Transient)
	TSubclassOf<UDreamUserWidget> PoolRowTemplateClass = nullptr;

	/** The display index the pool's first row currently shows. Zero while not recycling. */
	UPROPERTY(Transient)
	int32 WindowStart = 0;
};

/**
 * A list whose hierarchy is code, not an asset: rows built from a source, in a scrolling viewport.
 *
 * UMG's ListView in the DreamGUI idiom -- and deliberately smaller than it. There is no entry-widget
 * protocol to implement and no data source to write: the two properties a designer actually reaches
 * for (a set of items, and which one is selected) are UPROPERTYs, so .dui, the designer, Blueprint
 * and a `<->` binding can all drive them without anyone writing a line of glue.
 *
 * See UDreamListViewBase for the shape of the tree, why the rows are placed rather than arranged,
 * when it starts recycling them, and how a Native.List sits beside the `each` language feature.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream List View")
class DREAMGUI_API UDreamListView : public UDreamListViewBase
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why it
	 * stays editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "List")
	FDreamListStyle Style;

protected:
	virtual FDreamListStyle ResolveListStyle() const override;
};
