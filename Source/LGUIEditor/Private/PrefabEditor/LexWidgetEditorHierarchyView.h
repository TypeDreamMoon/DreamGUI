// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Misc/TextFilter.h"

class FLexUIPrefabEditor;
class ULexWidget;

class SLexWidgetEditorHierarchyView : public SCompoundWidget
{
public:
	typedef TTextFilter<TWeakObjectPtr<ULexWidget>> WidgetTextFilter;
public:
	SLATE_BEGIN_ARGS(SLexWidgetEditorHierarchyView)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWorld* InWorld);
	virtual ~SLexWidgetEditorHierarchyView()override;

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
	TWeakObjectPtr<ULexWidget> SetSelectionByNodeObject(ULexWidget* Element);
	void SetSelectionsByNodeObjects(const TArray<TWeakObjectPtr<ULexWidget>>& ElementArray);
	void ClearSelection();
	void GetExpandWidgets(TSet<TWeakObjectPtr<ULexWidget>>& OutExpandWidgets);
	TSharedPtr<SWidget> BuildContextMenu();

private:
	/** Rebuilds the tree structure based on the current filter options */
	void RefreshTree();
	void RebuildTreeView();
	void OnEditorSelectionChanged();
	void OnWidgetHierarchyChanged();
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap);
protected:
	TSharedRef< ITableRow > OnGenerateRow(TWeakObjectPtr<ULexWidget> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(TWeakObjectPtr<ULexWidget> InParent, TArray<TWeakObjectPtr<ULexWidget>>& OutChildren);
	void GetWidgetFilterStrings(TWeakObjectPtr<ULexWidget> Item, TArray<FString>& OutStrings);
	void OnSearchChanged(const FText& InFilterText);

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
	void RecursiveExpand(ULexWidget* Widget, EExpandBehavior ExpandBehavior);
	void OnExpansionChanged(TWeakObjectPtr<ULexWidget> Item, bool bExpanded);
	void SetItemExpansionRecursive(TWeakObjectPtr<ULexWidget> Model, bool bInExpansionState);
	void OnSelectionChanged(TWeakObjectPtr<ULexWidget> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedPtr<SWidget> OnContextMenuOpening();

	bool CanRename() const;
	void BeginRename();

	TWeakObjectPtr<UWorld> World;
	TWeakPtr<FLexUIPrefabEditor> Manager;
	TSharedPtr<FUICommandList> CommandList;
	TSharedPtr< TreeFilterHandler< TWeakObjectPtr<ULexWidget> > > FilterHandler;
	TArray< TWeakObjectPtr<ULexWidget> > RootWidgets;
	TArray< TWeakObjectPtr<ULexWidget> > TreeRootWidgets;
	TSharedPtr<SBorder> TreeViewArea;
	TSharedPtr< STreeView< TWeakObjectPtr<ULexWidget> > > WidgetTreeView;
	/** The unique names of all nodes expanded in the tree view */
	TSet< FString > ExpandedItemNames;
	/** The search box used to update the filter text */
	TSharedPtr<class SSearchBox> SearchBoxPtr;
	/** The filter used by the search box */
	TSharedPtr<WidgetTextFilter> SearchBoxWidgetFilter;
	TMap<ULexWidget*, bool> ExpansionMap;

	/** Has a full refresh of the tree been requested?  This happens when the user is filtering the tree */
	bool bRefreshRequested = true;
	/** Is the tree in such a changed state that the whole widget needs rebuilding? */
	bool bRebuildTreeRequested = true;
	/** Flag to ignore selections while the hierarchy view is updating the selection. */
	bool bIsUpdatingSelection = false;
};

