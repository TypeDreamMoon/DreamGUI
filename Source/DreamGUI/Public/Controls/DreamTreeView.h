// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "DreamTreeView.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamTreeExpansionChangedEvent, int32, ItemIndex, bool, bExpanded);

/**
 * Who an item's children are. UMG's FOnGetItemChildrenDynamic, with its signature.
 *
 * Single-cast, like UMG's and unlike every other delegate on this control: this one is a QUESTION,
 * and two answers to a question is not more information, it is an ambiguity nothing can resolve.
 */
DECLARE_DYNAMIC_DELEGATE_TwoParams(FDreamTreeGetItemChildren, UObject*, Item, TArray<UObject*>&, OutChildren);

/**
 * A tree whose hierarchy is code, not an asset: a list whose rows carry an indent and a twisty.
 *
 * A tree IS a list that indents, and this class is written to be exactly that much more than
 * UDreamListView -- both derive from UDreamListViewBase, which owns the viewport, the scrolled
 * column, the row template and the rows; this one adds a depth per item, a per-row indent, and a
 * twisty that expands and collapses. FDreamTreeViewStyle is built the same way round: it carries a
 * whole FDreamListStyle rather than restating its fields, so styling a project's lists styles its
 * trees' rows too.
 *
 * THE SOURCE IS FLAT, AND ON PURPOSE
 * ----------------------------------
 * Items (or ItemObjects) plus ItemDepths: the pre-order walk of the tree, with each item's depth
 * beside it. That is the shape UUITreeView flattens ITS source into before it can lay a row out
 * (Items + ItemDepths, rebuilt whenever expansion moves), and it is the only shape a control can
 * take from a designer -- a nested source needs an interface on the item class, which is a contract
 * a .dui author cannot sign and a details panel cannot draw.
 *
 * What the flat form buys, beyond being authorable: the subtree of a row is exactly the run of rows
 * that follow it at a greater depth, so collapsing is a skip rather than a graph walk, and no item
 * ever needs to be asked who its children are.
 *
 * Everything is expanded to begin with -- the authored list IS what you see -- and CollapsedItems
 * is the set of exceptions. Selection stays an index into the SOURCE, so it survives a collapse.
 *
 * OnGetItemChildren BUILDS THE FLAT SOURCE; IT DOES NOT REPLACE IT
 * ----------------------------------------------------------------
 * UMG's TreeView asks each item for its children and walks the answer, and so does this one --
 * SetRootItems plus OnGetItemChildren (or IUITreeViewItem::GetTreeChildren, which this plugin's
 * behaviour-side tree already speaks) flattens the hierarchy in pre-order into exactly the Items +
 * ItemDepths pair above.
 *
 * Flattening rather than walking lazily is the whole design, and it is what keeps every index in
 * this control's API meaning something: SelectedIndex, ItemDepths, GetRowWidget, ScrollItemIntoView
 * and CollapsedItems are all indices into the SOURCE, and a source that only materialises what is
 * currently expanded would renumber itself every time a twisty moved. The walk therefore takes the
 * WHOLE tree, including the children of collapsed nodes, and collapsing stays what it has always
 * been here: a skip over a run of deeper rows at display time. A collapsed parent keeps its twisty
 * because its children are still in the source to be counted.
 *
 * The cost of that decision is stated rather than hidden: an unbounded or lazily-loaded hierarchy
 * cannot be expressed this way, because the walk visits every node once. A tree that large wants the
 * recycling stack (UUITreeView) instead, which is why that class stays.
 *
 * Cycles are survivable: an item already visited is not visited again, so a graph produces a tree
 * rather than a hang.
 *
 * See UDreamListViewBase for the shape of the tree it builds, why it hosts the plain scroll view
 * rather than the recycling one, and how these controls sit beside the `each` language feature.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Tree View")
class DREAMGUI_API UDreamTreeView : public UDreamListViewBase
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why it
	 * stays editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View")
	FDreamTreeViewStyle Style;

	/**
	 * One depth per item, parallel to Items / ItemObjects: 0 is a root, 1 is a child of the nearest
	 * preceding 0, and so on. Missing entries read as 0, so a tree given no depths at all is simply
	 * a flat list -- which is the right thing for it to be.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View")
	TArray<int32> ItemDepths;

	/**
	 * The collapsed items, by source index. An exception set rather than an expanded set because a
	 * tree that opens showing nothing is a tree nobody can see they authored.
	 *
	 * An index is only where an item HAPPENS to be, so this set cannot survive a new source on its
	 * own -- see CollapsedItemObjects for the half that can, and OnSourceChanged for what happens to
	 * each of them when the source moves.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View")
	TSet<int32> CollapsedItems;

	/**
	 * The same exceptions, by IDENTITY, for a source that has ItemObjects.
	 *
	 * The index set is what the row builder reads and what an author can write; this is what lets it
	 * mean the same thing after the source is replaced. Collapse "Weapons", hand the tree a re-sorted
	 * list, and the fold follows the node rather than staying on whatever is now third.
	 *
	 * Transient: it is derived from the authored set the first time an object source is bound, and a
	 * text-only tree never fills it -- there is nothing to be identical TO.
	 */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Tree View")
	TSet<TObjectPtr<UObject>> CollapsedItemObjects;

	/** Per depth level, in whichever direction the twisty was clicked. */
	UPROPERTY(BlueprintAssignable, Category = "Tree View")
	FDreamTreeExpansionChangedEvent OnItemExpansionChanged;

	/** The template's twisty. Every row has a copy of it, found by display name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tree View")
	TObjectPtr<UDreamWidget> TwistyTemplateNode = nullptr;

	/** The glyph inside it -- the stand-in for a state whose brush holds no image. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tree View")
	TObjectPtr<UDreamWidget> TwistyGlyphTemplateNode = nullptr;

	/**
	 * The top of the hierarchy, for a tree that is authored as a GRAPH rather than as a flat array.
	 *
	 * Set it (or call SetRootItems) and the control walks it with OnGetItemChildren, writing the flat
	 * Items + ItemDepths pair the rest of this class is written in. Empty is the flat road, where
	 * ItemObjects / Items and ItemDepths are authored directly and nothing walks anything.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View")
	TArray<TObjectPtr<UObject>> RootItems;

	/**
	 * Who an item's children are. UMG's OnGetItemChildren, with its signature and its job.
	 *
	 * Unbound, the walk falls back to IUITreeViewItem::GetTreeChildren -- the interface the
	 * behaviour-side UUITreeView has always used -- so an item class that already implements it needs
	 * no delegate at all. With neither, every root is a leaf.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Tree View")
	FDreamTreeGetItemChildren OnGetItemChildren;

	/** Replace the roots and re-walk. The hierarchical counterpart of SetItemsWithDepths. */
	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void SetRootItems(const TArray<UObject*>& InRootItems);

	/**
	 * Walk the roots again and rebuild the flat source from what the children provider says now.
	 *
	 * For a hierarchy that changed underneath the control -- a node gained a child, a provider now
	 * answers differently. Folds survive it: they are kept by item identity, so a node that is still
	 * in the tree is still folded wherever the walk puts it.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void RefreshTree();

	/** Replace the source and its depths together, which is the only way they are ever coherent. */
	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void SetItemsWithDepths(const TArray<FText>& InItems, const TArray<int32>& InDepths);

	/** Replace the depths alone, for a source that did not move. */
	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void SetItemDepths(const TArray<int32>& InDepths);

	/** How far in this item's row starts, in pixels: its depth times the style's IndentPerLevel. */
	UFUNCTION(BlueprintPure, Category = "Tree View")
	float GetRowIndent(int32 InItemIndex) const;

	/** True when the next item is deeper -- which, in a pre-order flat list, is what a parent is. */
	UFUNCTION(BlueprintPure, Category = "Tree View")
	bool ItemHasChildren(int32 InItemIndex) const;

	UFUNCTION(BlueprintPure, Category = "Tree View")
	bool IsItemExpanded(int32 InItemIndex) const;

	/** Moves the twisty, rebuilds the visible rows, then says so. A no-op if it was already there. */
	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void SetItemExpanded(int32 InItemIndex, bool bInExpanded);

	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void ToggleItemExpansion(int32 InItemIndex);

	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void ExpandAll();

	UFUNCTION(BlueprintCallable, Category = "Tree View")
	void CollapseAll();

protected:
	virtual FDreamListStyle ResolveListStyle() const override;
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void CollectVisibleItemIndices(TArray<int32>& OutIndices) const override;
	/**
	 * Re-locate the expansion state onto the new source, or drop it.
	 *
	 * By object where there are objects; dropped where there are not, because a flat text source has
	 * no identity and index 3 of the new tree is a different node than index 3 of the old one --
	 * keeping the number leaves a subtree folded that nobody folded.
	 */
	virtual void OnSourceChanged(const TArray<TObjectPtr<UObject>>& InPreviousItemObjects) override;
	virtual int32 GetItemDepth(int32 InItemIndex) const override;
	virtual float GetRowContentInset(int32 InItemIndex) const override;
	virtual void DecorateRowTemplate(UDreamWidget& InTemplate) override;
	/**
	 * The twisty's click handler, subscribed ONCE per row widget.
	 *
	 * Not from DecorateRow, which runs on every bind: a recycled row that had shown ten items would
	 * be carrying ten subscriptions, and one click would toggle ten different rows' expansion. The
	 * handler asks GetRowItemIndex at the moment it fires instead of capturing an item index, because
	 * which item this slot shows changes underneath it.
	 */
	virtual void DecorateNewRow(UDreamWidget& InRow, int32 InPoolIndex) override;
	virtual void DecorateRow(UDreamWidget& InRow, int32 InPoolIndex, int32 InItemIndex) override;

private:
	/** The whole style. ResolveListStyle hands the base the List half of this same answer. */
	FDreamTreeViewStyle ResolveTreeStyle() const;

	/** The children provider, whichever of the two roads answered. Empty when neither does. */
	void GetChildrenOf(UObject* InItem, TArray<UObject*>& OutChildren) const;

	/**
	 * One node and everything under it, in pre-order, with depths beside it.
	 *
	 * Visited guards cycles: an item reached twice is skipped the second time, so a graph flattens to
	 * a tree instead of hanging. UUITreeView::AppendItemRecursive makes the same guard for the same
	 * reason -- this is that walk, writing into the flat pair rather than into a recycler's source.
	 */
	void AppendItemAndChildren(UObject* InItem, int32 InDepth, TArray<UObject*>& OutItems,
		TArray<int32>& OutDepths, TSet<UObject*>& InOutVisited) const;
};
