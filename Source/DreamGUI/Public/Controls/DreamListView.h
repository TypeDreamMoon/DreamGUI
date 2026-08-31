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
 * bar, the scrolled column, the row template, the row-building loop, the row colours and the one
 * selected index live here, and each concrete control adds only what makes it itself -- its typed
 * style, and (for the tree) a depth per item, a per-row indent and a twisty. FDreamTreeViewStyle is
 * built the same way round, carrying a whole FDreamListStyle rather than restating its fields.
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
 * That last part is why this does not simply nest a UDreamScrollBox, which is otherwise the same
 * control: a scroll box takes its content's extent from the stack's MEASURE, and a row measures as
 * its label's line height (see UDreamPanelLayoutBase::GetDesiredSize -- an authored size is only
 * consulted for an axis nothing else claimed, and a row's overlay always claims one). A list whose
 * rows were as tall as their text would not be a list with a row height.
 *
 * WHY THE PLAIN SCROLL VIEW, NOT UUIListView
 * ------------------------------------------
 * The recycling stack (UUIRecyclableScrollView, and UUIListView on top of it) is the right answer
 * for a hundred thousand rows and the wrong one for a control, for four reasons that are all in
 * those files:
 *
 *  - its unit is a UObject* item, because IUIRecyclableScrollViewDataSource is a UObject protocol.
 *    A Native.List's natural item is a line of text -- the same call FDreamDropdownStyle's options
 *    make -- and feeding the recycler would mean minting a UObject per label;
 *  - a cell only exists after Start(), which needs a live world, a registered tree AND an arranged
 *    viewport: InitializeOnDataSource sizes the cell pool from the content parent's local-space
 *    extents. A control has to be correct the instant its properties are set -- in the designer's
 *    preview, in a headless test, before anything ticks;
 *  - the pool is sized to what is VISIBLE, so "one row per item" is never true of it. Half of what
 *    a consumer asks a list control ("give me the widget for item 7") has no answer there;
 *  - and UUIListView::Awake hands its data-source seat to whoever claimed it first, which is how an
 *    `each` block's adapter gets in. A control hosting one would be a third party to that seat.
 *
 * So the rows are built here, from a template, into a column inside a plain UUIScrollView -- which
 * is what UDreamDropdown's option list does, because it is the same problem. The fixes that file
 * paid for are re-applied here rather than re-earned: absolute sizes with point anchors on anything
 * positioned, a zero size DELTA (never a width) on the stretched column, both scroll axes stated
 * explicitly, and rows sized through their SLOTS against a column of known height instead of
 * through authored heights an Auto content measure would outvote.
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
 *   like every other list in the project because its rows come from the style sheet -- placed from
 *   .dui, C++, Blueprint or the far side of a `<->` binding, with no `each` block and no generated
 *   class needed. Rows come from Items (texts) or ItemObjects (your objects); RowTemplateClass or
 *   OnRowGenerated fill in a richer row when the built-in label is not enough.
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
	 * A row's CONTENT, authored elsewhere: one instance of this class is created inside every row,
	 * filling it, and the built-in label steps aside. The row's face, height, hover and selection
	 * stay the control's, so a template only has to draw an item -- it does not have to re-implement
	 * being a row.
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
	 * One per row, as it is built, with the row widget and the item it stands for. The hook for a
	 * consumer whose rows are richer than a label but who would rather not author a whole class:
	 * everything under the row is reachable from here by display name.
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

	/** The live rows, in visual order. Empty until the first rebuild. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "List")
	TArray<TObjectPtr<UDreamWidget>> RowNodes;

	/** Parallel to RowNodes: which source item each row stands for. Identity for a flat list. */
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

	/** How many rows exist. For a tree this is the VISIBLE count, which is the point of a tree. */
	UFUNCTION(BlueprintPure, Category = "List")
	int32 GetRowCount() const { return RowNodes.Num(); }

	/** The row standing for a source item, or null when that item has no row (collapsed, or gone). */
	UFUNCTION(BlueprintPure, Category = "List")
	UDreamWidget* GetRowWidget(int32 InItemIndex) const;

	/** Scroll the least distance that brings an item's row fully into the viewport. */
	UFUNCTION(BlueprintCallable, Category = "List")
	bool ScrollItemIntoView(int32 InItemIndex, bool bInAnimate = true);

	/**
	 * Throw the rows away and build them again from the source. Called for you by ApplyStyle -- row
	 * geometry and row colour are both style, so there is no such thing as re-styling without it --
	 * and by every setter that moves the source.
	 */
	UFUNCTION(BlueprintCallable, Category = "List")
	void RebuildRows();

	virtual void ApplyStyle() override;

protected:
	virtual void NativeOnInitialized() override;

	/**
	 * The list half of this control's style. Each concrete control resolves its own family and hands
	 * back a reference into it -- the sheet's entry or this instance's Style, both of which outlive
	 * the call. The base never sees which.
	 */
	virtual const FDreamListStyle& ResolveListStyle() const;

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

	/** Fix up one freshly built row. Runs after the base has skinned, sized and wired it. */
	virtual void DecorateRow(UDreamWidget& InRow, int32 InRowIndex, int32 InItemIndex) {}

	/** The label a row shows: the matching text, else the item object's name, else nothing. */
	FText GetItemLabel(int32 InItemIndex) const;

	/** The object a row stands for, if the source has one. */
	UObject* GetItemObject(int32 InItemIndex) const;

	/** Re-push every row's resting colour without rebuilding anything. What selection actually moves. */
	void RefreshRowColors();

	/** Between a row's edge and its content -- the row's own inset, not the viewport's Padding. */
	static FMargin GetRowPadding();

private:
	void HandleRowClicked(int32 InItemIndex);
	void BuildRow(int32 InRowIndex, int32 InItemIndex, const FDreamListStyle& InStyle);
	void ApplyRowColor(UDreamWidget* InRow, int32 InRowIndex, int32 InItemIndex, const FDreamListStyle& InStyle);

	/** The gutter, the bar's rect and the scroll range -- everything that follows from the row count. */
	void RefreshScrollFurniture(const FDreamListStyle& InStyle);

	/** True while the bar has something to say: shown at all, and either permanent or overflowing. */
	bool ShouldShowScrollBar() const;
};

/**
 * A list whose hierarchy is code, not an asset: rows built from a source, in a scrolling viewport.
 *
 * UMG's ListView in the DreamGUI idiom -- and deliberately smaller than it. There is no entry-widget
 * protocol to implement and no data source to write: the two properties a designer actually reaches
 * for (a set of items, and which one is selected) are UPROPERTYs, so .dui, the designer, Blueprint
 * and a `<->` binding can all drive them without anyone writing a line of glue.
 *
 * See UDreamListViewBase for the shape of the tree, why it hosts the plain scroll view rather than
 * the recycling one, and how a Native.List sits beside the `each` language feature.
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
	virtual const FDreamListStyle& ResolveListStyle() const override;
};
