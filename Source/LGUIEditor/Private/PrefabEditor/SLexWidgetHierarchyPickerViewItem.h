// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SLexWidgetHierarchyPickerView.h"
#include "Widgets/Views/STreeView.h"
#include "Widgets/Views/STableRow.h"

class SLexWidgetEditorHierarchyView;
class ULexWidget;
class FLGUIPrefabEditor;

class SLexWidgetHierarchyPickerViewItem : public STableRow<SLexWidgetHierarchyPickerView::DataType>
{
public:
	SLATE_BEGIN_ARGS(SLexWidgetHierarchyPickerViewItem) {}
	SLATE_END_ARGS()
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, SLexWidgetHierarchyPickerView::DataType InModel
		, TSharedPtr<FLGUIPrefabEditor> InManager, UClass* InObjectClass);

private:
	FText GetItemText() const;
	FText GetTypeText() const;
	
	TWeakPtr<SLexWidgetEditorHierarchyView> HierarchyView;
	TWeakPtr<FLGUIPrefabEditor> Manager;
	SLexWidgetHierarchyPickerView::DataType Model;
};
