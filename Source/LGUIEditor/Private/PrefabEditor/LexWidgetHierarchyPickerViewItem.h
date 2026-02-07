// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "LexWidgetHierarchyPickerView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

class SLexWidgetEditorHierarchyView;
class ULexWidget;
class FLexUIPrefabEditor;

class SLexWidgetHierarchyPickerViewItem : public STableRow<SLexWidgetHierarchyPickerView::DataType>
{
public:
	DECLARE_DELEGATE_OneParam(FOnSelectObject, UObject*);
	
	SLATE_BEGIN_ARGS(SLexWidgetHierarchyPickerViewItem) {}
		SLATE_EVENT(FOnSelectObject, OnSelectObject)
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SLexWidgetHierarchyPickerView::DataType InModel
		, UClass* InObjectClass);

	virtual ~SLexWidgetHierarchyPickerViewItem();

private:	
	TWeakPtr<SLexWidgetEditorHierarchyView> HierarchyView;
	SLexWidgetHierarchyPickerView::DataType Model;
	TSharedPtr<SMenuAnchor> MenuAnchor;
	FMenuBuilder* MenuBuilder = nullptr;
};
