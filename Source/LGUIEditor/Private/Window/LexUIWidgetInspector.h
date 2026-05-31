// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Views/STableViewBase.h"
#pragma once

class SLexWidgetEditorHierarchyView;
/**
 * 
 */
class SLexUIWidgetInspector : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIWidgetInspector) {}
	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDockTab> InOwnerTab);

	static TWeakObjectPtr<UWorld> CurrentSelectedWorld;
	TWeakObjectPtr<UWorld> World;
private:
	void CloseTabCallback(TSharedRef<SDockTab> TabClosed);
	TSharedPtr<SLexWidgetEditorHierarchyView> HierarchyView;
};
