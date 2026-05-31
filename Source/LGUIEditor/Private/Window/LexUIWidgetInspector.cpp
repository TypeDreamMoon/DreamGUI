// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Window/LexUIWidgetInspector.h"

#include "DetailLayoutBuilder.h"
#include "Core/LexUIManager.h"
#include "PrefabEditor/LexUIPrefabEditorDetails.h"
#include "PrefabEditor/LexWidgetEditorHierarchyView.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "LexUIWidgetInspector"

TWeakObjectPtr<UWorld> SLexUIWidgetInspector::CurrentSelectedWorld = nullptr;

void SLexUIWidgetInspector::Construct(const FArguments& Args, TSharedPtr<SDockTab> InOwnerTab)
{
	InOwnerTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &SLexUIWidgetInspector::CloseTabCallback));
	World = CurrentSelectedWorld;
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		LexUIManager->OnDeinitialize.AddSPLambda(this, [this, InOwnerTab]()
		{
			InOwnerTab->RequestCloseTab();
		});
		LexUIManager->OnLexUIWidgetOutlinerChanged.AddSPLambda(this, [this]()
		{
			if (HierarchyView.IsValid())
			{
				HierarchyView->RequestRefresh();
			}
		});
	}
	if (CurrentSelectedWorld == nullptr)
	{
		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoValidWorld", "Not valid world!"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
		InOwnerTab->RequestCloseTab();
	}
	else
	{
		ChildSlot
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			[
				SAssignNew(HierarchyView, SLexWidgetEditorHierarchyView, CurrentSelectedWorld.Get())
			]
			+ SSplitter::Slot()
			[
				SNew(SLexUIPrefabEditorDetails, CurrentSelectedWorld.Get())
			]
		];
	}
}

void SLexUIWidgetInspector::CloseTabCallback(TSharedRef<SDockTab> TabClosed)
{
	if (auto Selection = ULexUISelection::GetInstance(World.Get()))
	{
		Selection->SelectNone();
	}
}
#undef LOCTEXT_NAMESPACE