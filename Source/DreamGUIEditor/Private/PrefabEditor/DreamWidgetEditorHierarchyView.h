// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Misc/TextFilter.h"

class FDreamWidgetBlueprintEditor;
class UDreamWidget;

/**
 * Whether the hierarchy may rename a widget, asked of the model rather than of the row showing it.
 *
 * The row is the wrong place to ask: STableViewBase::WidgetFromItem hands back a null TSharedPtr for
 * a row the tree has not generated, and the Rename command polls its FCanExecuteAction whenever the
 * command state is evaluated -- long before the user has scrolled that row into view. Keeping the
 * policy on the model is also what stops the row's inline edit box and the command from disagreeing
 * about the designer lock.
 */
namespace DreamWidgetHierarchyRename
{
	bool CanRename(const UDreamWidget* Widget, bool bLockedInDesigner);
}

/**
 * What a widget IS, for the search box and for the row's label.
 *
 * Every element in this tree is a UDreamWidget; what makes one a text block and another a horizontal
 * box lives on its Visual, its layout container and its behaviours. A tree keyed on the display name
 * alone therefore shows nothing about type and cannot find it either -- searching "Text" answers
 * nothing unless whoever placed the element happened to type that into its name.
 */
namespace DreamWidgetHierarchyType
{
	/** Everything the search box may match this widget by: its name, and the classes it is made of. */
	void CollectSearchTerms(const UDreamWidget* Widget, TArray<FString>& OutTerms);
	/** The subdued suffix the row prints after the name; empty for a plain widget, which needs none. */
	FString GetTypeLabel(const UDreamWidget* Widget);
}

class SDreamWidgetEditorHierarchyView : public SCompoundWidget
{
public:
	typedef TTextFilter<TWeakObjectPtr<UDreamWidget>> WidgetTextFilter;
public:
	SLATE_BEGIN_ARGS(SDreamWidgetEditorHierarchyView)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWorld* InWorld);
	virtual ~SDreamWidgetEditorHierarchyView()override;

	// Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	// drops into the empty area below the tree (no row target) -- create under the root widget
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// End SWidget

	void RequestRefresh();
	void RefreshImmediately();
	TWeakObjectPtr<UDreamWidget> SetSelectionByNodeObject(UDreamWidget* Element);
	void SetSelectionsByNodeObjects(const TArray<TWeakObjectPtr<UDreamWidget>>& ElementArray);
	void ClearSelection();
	void GetExpandWidgets(TSet<TWeakObjectPtr<UDreamWidget>>& OutExpandWidgets);
	TSharedPtr<SWidget> BuildContextMenu();

private:
	/** Rebuilds the tree structure based on the current filter options */
	void RefreshTree();
	void RebuildTreeView();
	void OnEditorSelectionChanged();
	void OnWidgetHierarchyChanged();
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
protected:
	TSharedRef< ITableRow > OnGenerateRow(TWeakObjectPtr<UDreamWidget> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TWeakObjectPtr<UDreamWidget> InParent, TArray<TWeakObjectPtr<UDreamWidget>>& OutChildren);
	void GetWidgetFilterStrings(TWeakObjectPtr<UDreamWidget> Item, TArray<FString>& OutStrings);
	void OnSearchChanged(const FText& InFilterText);
	/** What the rows highlight -- the live search text, so a match is visible in a name that is longer than it. */
	FText GetSearchText()const;

	/** Sets the expansion state of hierarchy view items based on their model. */
	void UpdateItemsExpansionFromModel();
	/** Stores the names of all currently expanded nodes in the hierarchy view. */
	void SaveItemsExpansion();
	/** Sets the expansion state of hierarchy view items based on the state saved by SaveItemsExpansion. */
	void RestoreItemsExpansion();
	enum class EExpandBehavior : uint8
	{
		NeverExpand,
		AlwaysExpand,
		RestoreFromPrevious,
		FromModel
	};
	/** Recursively expands the models based on the expansion set. */
	void RecursiveExpand(UDreamWidget* Widget, EExpandBehavior ExpandBehavior);
	void OnExpansionChanged(TWeakObjectPtr<UDreamWidget> Item, bool bExpanded);
	void SetItemExpansionRecursive(TWeakObjectPtr<UDreamWidget> Model, bool bInExpansionState);
	/** UMG Hierarchy's Expansion section: apply one expansion state to the whole tree. */
	void SetAllExpansion(bool bExpand);
	void OnSelectionChanged(TWeakObjectPtr<UDreamWidget> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> OnContextMenuOpening();

	bool CanRename() const;
	void BeginRename();

	TWeakObjectPtr<UWorld> World;
	TWeakPtr<FDreamWidgetBlueprintEditor> Manager;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr< TreeFilterHandler< TWeakObjectPtr<UDreamWidget> > > FilterHandler;
	TArray< TWeakObjectPtr<UDreamWidget> > RootWidgets;
	TArray< TWeakObjectPtr<UDreamWidget> > TreeRootWidgets;
	TSharedPtr<SBorder> TreeViewArea;
	TSharedPtr< STreeView< TWeakObjectPtr<UDreamWidget> > > WidgetTreeView;
	/** The unique names of all nodes expanded in the tree view */
	TSet< FString > ExpandedItemNames;
	/** The search box used to update the filter text */
	TSharedPtr<class SSearchBox> SearchBoxPtr;
	/** The filter used by the search box */
	TSharedPtr<WidgetTextFilter> SearchBoxWidgetFilter;
	TMap<UDreamWidget*, bool> ExpansionMap;

	/** Has a full refresh of the tree been requested?  This happens when the user is filtering the tree */
	bool bRefreshRequested = true;
	/** Is the tree in such a changed state that the whole widget needs rebuilding? */
	bool bRebuildTreeRequested = true;
	/** Flag to ignore selections while the hierarchy view is updating the selection. */
	bool bIsUpdatingSelection = false;
};

