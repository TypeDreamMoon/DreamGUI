// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamListView.h"
#include "DreamTreeView.generated.h"

class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamTreeExpansionChangedEvent, int32, ItemIndex, bool, bExpanded);

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
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tree View")
	TSet<int32> CollapsedItems;

	/** Per depth level, in whichever direction the twisty was clicked. */
	UPROPERTY(BlueprintAssignable, Category = "Tree View")
	FDreamTreeExpansionChangedEvent OnItemExpansionChanged;

	/** The template's twisty. Every row has a copy of it, found by display name. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tree View")
	TObjectPtr<UDreamWidget> TwistyTemplateNode = nullptr;

	/** The glyph inside it -- the stand-in for a state whose brush holds no image. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Tree View")
	TObjectPtr<UDreamWidget> TwistyGlyphTemplateNode = nullptr;

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
	virtual void CollectVisibleItemIndices(TArray<int32>& OutIndices) const override;
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
};
