// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Window/LexUIWidgetInspector.h"

#include "DetailLayoutBuilder.h"
#include "Core/LexUIManager.h"
#include "PrefabEditor/LexUIPrefabEditorDetails.h"
#include "PrefabEditor/LexWidgetEditorHierarchyView.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "LexUIWidgetInspector"

void SLexUIWidgetInspector::Construct(const FArguments& Args, TSharedPtr<SDockTab> InOwnerTab)
{
	OwnerTab = InOwnerTab;
	InOwnerTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &SLexUIWidgetInspector::CloseTabCallback));
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		LexUIManager->OnEndPlay.AddSPLambda(this, [this, InOwnerTab]()
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
	ChildSlot
	[
		SAssignNew(ContentBox, SBox)
	];
}

void SLexUIWidgetInspector::AssignWorld(UWorld* InWorld)
{
	World = InWorld;
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		LexUIManager->OnEndPlay.AddSPLambda(this, [this]()
		{
			World = nullptr;
			RefreshContent();
		});
	}
	RefreshContent();
}

void SLexUIWidgetInspector::CloseTabCallback(TSharedRef<SDockTab> TabClosed)
{
	if (auto Selection = ULexUISelection::GetInstance(World.Get()))
	{
		Selection->SelectNone();
	}
}
void SLexUIWidgetInspector::RefreshContent()
{
	if (World.IsValid())
	{
		ContentBox->SetContent(
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			[
				SAssignNew(HierarchyView, SLexWidgetEditorHierarchyView, World.Get())
			]
			+ SSplitter::Slot()
			[
				SNew(SLexUIPrefabEditorDetails, World.Get())
			]
			);
	}
	else
	{
		ContentBox->SetContent(
			SNew(SBox)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("NoValidWorld", "Not valid world!"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			);
		OwnerTab.Pin()->RequestCloseTab();
	}
}
#undef LOCTEXT_NAMESPACE