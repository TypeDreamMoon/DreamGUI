// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "Framework/Views/TreeFilterHandler.h"
#include "Misc/TextFilter.h"

class FLGUIPrefabEditor;
class ULexWidget;

struct SLexWidgetHierarchyPickerView_DataItem
{
	FString DisplayText;
	TWeakObjectPtr<UObject> Object;
	bool bContainsValidObject = false;
	TArray<TSharedPtr<SLexWidgetHierarchyPickerView_DataItem>> Children;

	SLexWidgetHierarchyPickerView_DataItem(FString InDisplayText, TWeakObjectPtr<UObject> InObject)
	{
		this->DisplayText = InDisplayText;
		this->Object = InObject;
	}
};

DECLARE_DELEGATE_OneParam(FOnSelectItem, UObject*);

class SLexWidgetHierarchyPickerView : public SCompoundWidget
{
public:
	typedef TSharedPtr<SLexWidgetHierarchyPickerView_DataItem> DataType;
	typedef TTextFilter<DataType> WidgetTextFilter;
public:
	SLATE_BEGIN_ARGS(SLexWidgetHierarchyPickerView)
	{}
		SLATE_EVENT(FOnSelectItem, OnSelectItem)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InManager, UClass* InObjectClass);
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

	TWeakPtr<FLGUIPrefabEditor> Manager;
	TSharedPtr< TreeFilterHandler< DataType > > FilterHandler;
	TArray< DataType > RootWidgets;
	TArray< DataType > TreeRootWidgets;
	TSharedPtr<SBorder> TreeViewArea;
	TSharedPtr< STreeView< DataType > > WidgetTreeView;
	/** The search box used to update the filter text */
	TSharedPtr<class SSearchBox> SearchBoxPtr;
	/** The filter used by the search box */
	TSharedPtr<WidgetTextFilter> SearchBoxWidgetFilter;

	bool bRefreshRequested;
	FOnSelectItem OnSelectItem;
	UClass* ObjectClass;
};

