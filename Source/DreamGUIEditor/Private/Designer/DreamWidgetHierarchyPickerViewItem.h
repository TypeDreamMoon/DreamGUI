// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetHierarchyPickerView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

class SDreamWidgetEditorHierarchyView;
class UDreamWidget;
class FDreamWidgetBlueprintEditor;

class SDreamWidgetHierarchyPickerViewItem : public STableRow<SDreamWidgetHierarchyPickerView::DataType>
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectObject, UObject*);
	
	SLATE_BEGIN_ARGS(SDreamWidgetHierarchyPickerViewItem) {}
		SLATE_EVENT(FOnSelectObject, OnSelectObject)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SDreamWidgetHierarchyPickerView::DataType InModel
		, UClass* InObjectClass);

	virtual ~SDreamWidgetHierarchyPickerViewItem();

private:	
	TWeakPtr<SDreamWidgetEditorHierarchyView> HierarchyView;
	SDreamWidgetHierarchyPickerView::DataType Model;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	FMenuBuilder* MenuBuilder = nullptr;
};
