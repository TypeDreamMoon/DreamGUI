// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Misc/TextFilter.h"

class ULexWidget;

struct FLexWidgetHierarchyPickerView_ValidObjectData
{
	TArray<TWeakObjectPtr<UObject>> ValidObjectArray;
	TArray<FLexWidgetHierarchyPickerView_ValidObjectData> ChildDataArray;
};

struct FLexWidgetHierarchyPickerView_DataItem
{
	FString DisplayText;
	TWeakObjectPtr<ULexWidget> Widget;
	bool bContainsValidObject = false;
	TArray<TSharedPtr<FLexWidgetHierarchyPickerView_DataItem>> Children;
	TArray<TWeakObjectPtr<UObject>> ValidObjectArray;

	TSharedPtr<FLexWidgetHierarchyPickerView_ValidObjectData> ValidActor;
	TArray<TSharedPtr<FLexWidgetHierarchyPickerView_ValidObjectData>> ValidComponentArray;

	FLexWidgetHierarchyPickerView_DataItem(FString InDisplayText, TWeakObjectPtr<ULexWidget> InWidget)
	{
		this->DisplayText = InDisplayText;
		this->Widget = InWidget;
	}
};

DECLARE_DELEGATE_OneParam(FOnSelectItem, UObject*);

class SLexWidgetHierarchyPickerView : public SCompoundWidget
{
public:
	typedef TSharedPtr<FLexWidgetHierarchyPickerView_DataItem> DataType;
	typedef TTextFilter<DataType> WidgetTextFilter;
public:
	SLATE_BEGIN_ARGS(SLexWidgetHierarchyPickerView)
	{}
		SLATE_EVENT(FOnSelectItem, OnSelectItem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UWorld* InPrefabWorld, UClass* InObjectClass, ULexWidget* InRootWidget = nullptr);
	virtual ~SLexWidgetHierarchyPickerView();

	// Begin SWidget
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	// End SWidget

	void RefreshImmediately();

	void RecursiveExpand(DataType Model);
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
	ULexWidget* SpecificRootWidget = nullptr;
};

