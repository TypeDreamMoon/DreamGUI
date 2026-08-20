// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Views/STableViewBase.h"
#pragma once

class SDreamWidgetEditorHierarchyView;
/**
 * 
 */
class SDreamUIWidgetInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIWidgetInspector) {}
	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDockTab> InOwnerTab);

	UWorld* GetWorld()const { return World.Get(); }
	void AssignWorld(UWorld* InWorld);
private:
	TWeakObjectPtr<UWorld> World;
	TSharedPtr<SBox> ContentBox;
	TWeakPtr<SDockTab> OwnerTab;
	void CloseTabCallback(TSharedRef<SDockTab> TabClosed);
	TSharedPtr<SDreamWidgetEditorHierarchyView> HierarchyView;
	void RefreshContent();
};
