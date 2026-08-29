// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Misc/TextFilter.h"

class UDreamWidget;

struct FDreamWidgetHierarchyPickerView_ValidObjectData
{
	TArray<TWeakObjectPtr<UObject>> ValidObjectArray;
	TArray<FDreamWidgetHierarchyPickerView_ValidObjectData> ChildDataArray;
};

struct FDreamWidgetHierarchyPickerView_DataItem
{
	FString DisplayText;
	TWeakObjectPtr<UDreamWidget> Widget;
	bool bContainsValidObject = false;
	TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>> Children;
	TArray<TWeakObjectPtr<UObject>> ValidObjectArray;

	TSharedPtr<FDreamWidgetHierarchyPickerView_ValidObjectData> ValidActor;
	TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_ValidObjectData>> ValidComponentArray;

	FDreamWidgetHierarchyPickerView_DataItem(FString InDisplayText, TWeakObjectPtr<UDreamWidget> InWidget)
	{
		this->DisplayText = InDisplayText;
		this->Widget = InWidget;
	}
};

/**
 * One data item per root hierarchy, each carrying the objects of InObjectClass that can be picked
 * under it.
 *
 * Every root gets walked, not only the first. A root whose ValidObjectArray stayed empty is drawn as
 * a disabled leaf with no expander, so before this took a list, nothing living in a second hierarchy
 * could be bound at all.
 */
void DreamWidgetHierarchyPicker_BuildRoots(const TArray<UDreamWidget*>& InRootWidgets, UClass* InObjectClass
	, TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>>& OutRoots);

DECLARE_DELEGATE_OneParam(FOnSelectItem, UObject*);

class SDreamWidgetHierarchyPickerView : public SCompoundWidget
{
public:
	typedef TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem> DataType;
	typedef TTextFilter<DataType> WidgetTextFilter;
public:
	SLATE_BEGIN_ARGS(SDreamWidgetHierarchyPickerView)
	{}
		SLATE_EVENT(FOnSelectItem, OnSelectItem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWorld* InPrefabWorld, UClass* InObjectClass, UDreamWidget* InRootWidget = nullptr);
	virtual ~SDreamWidgetHierarchyPickerView();

	// Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End SWidget

	void RefreshImmediately();

	void RecursiveExpand(DataType Model, bool bInExpansionState);
	void SetItemExpansionRecursive(DataType Model, bool bInExpansionState);
private:
	/** Rebuilds the tree structure based on the current filter options */
	void RefreshTree();
	void RebuildTreeView();
protected:
	TSharedRef< ITableRow > OnGenerateRow(DataType InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void OnGetChildren(DataType InParent, TArray<DataType>& OutChildren);
	void GetWidgetFilterStrings(DataType Item, TArray<FString>& OutStrings);
	void OnSearchChanged(const FText& InFilterText);
	void UpdateItemsExpansionFromModel();
	void OnSelectionChanged(DataType SelectedItem, ESelectInfo::Type SelectInfo);

	TWeakObjectPtr<UWorld> PrefabWorld;
	TSharedPtr< TreeFilterHandler< DataType > > FilterHandler;
	TArray< DataType > RootWidgets;
	TArray< DataType > TreeRootWidgets;
	TSharedPtr<SBorder> TreeViewArea;
	TSharedPtr< STreeView< DataType > > WidgetTreeView;
	/** The search box used to update the filter text */
	TSharedPtr<class SSearchBox> SearchBoxPtr;
	/** The filter used by the search box */
	TSharedPtr<WidgetTextFilter> SearchBoxWidgetFilter;

	bool bRefreshRequested = true;
	FOnSelectItem OnSelectItem;
	UClass* ObjectClass = nullptr;
	UDreamWidget* SpecificRootWidget = nullptr;
};

