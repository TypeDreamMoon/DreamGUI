// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Window/DreamUIWidgetInspector.h"

#include "DetailLayoutBuilder.h"
#include "Core/DreamUIManager.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "Designer/DreamWidgetEditorHierarchyView.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DreamUIWidgetInspector"

void SDreamUIWidgetInspector::Construct(const FArguments& Args, TSharedPtr<SDockTab> InOwnerTab)
{
	OwnerTab = InOwnerTab;
	InOwnerTab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateSP(this, &SDreamUIWidgetInspector::CloseTabCallback));
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		DreamUIManager->OnEndPlay.AddSPLambda(this, [this, InOwnerTab]()
		{
			InOwnerTab->RequestCloseTab();
		});
		DreamUIManager->OnDreamUIWidgetOutlinerChanged.AddSPLambda(this, [this]()
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

void SDreamUIWidgetInspector::AssignWorld(UWorld* InWorld)
{
	World = InWorld;
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		DreamUIManager->OnEndPlay.AddSPLambda(this, [this]()
		{
			World = nullptr;
			RefreshContent();
		});
		DreamUIManager->OnDreamUIWidgetOutlinerChanged.AddSPLambda(this, [this]()
		{
			if (HierarchyView.IsValid())
			{
				HierarchyView->RequestRefresh();
			}
		});
	}
	RefreshContent();
}

void SDreamUIWidgetInspector::CloseTabCallback(TSharedRef<SDockTab> TabClosed)
{
	if (auto Selection = UDreamUISelection::GetInstance(World.Get()))
	{
		Selection->SelectNone();
	}
}
void SDreamUIWidgetInspector::RefreshContent()
{
	if (World.IsValid())
	{
		ContentBox->SetContent(
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
			+ SSplitter::Slot()
			[
				SAssignNew(HierarchyView, SDreamWidgetEditorHierarchyView, World.Get())
			]
			+ SSplitter::Slot()
			[
				SNew(SDreamWidgetDesignerDetails, World.Get())
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