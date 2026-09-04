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
	// No subscriptions here: World is still null at construction, so the manager lookup never found
	// anything and the handlers this used to install were never installed. AssignWorld is where the
	// tab learns which world it is showing, and that is where it subscribes.
	ChildSlot
	[
		SAssignNew(ContentBox, SBox)
	];
}

void SDreamUIWidgetInspector::AssignWorld(UWorld* InWorld)
{
	// The tab is reused across worlds, so the previous world's subscriptions have to come OFF before
	// the next ones go on. Without this the widget accumulated one handler per world it was ever
	// pointed at, and the first EndPlay from any of them blanked the world actually on screen.
	if (UDreamUIManagerWorldSubsystem* PreviousManager = UDreamUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		PreviousManager->OnEndPlay.Remove(EndPlayHandle);
		PreviousManager->OnDreamUIWidgetOutlinerChanged.Remove(OutlinerChangedHandle);
	}
	EndPlayHandle.Reset();
	OutlinerChangedHandle.Reset();

	World = InWorld;
	if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		const TWeakObjectPtr<UWorld> SubscribedWorld = World;
		EndPlayHandle = DreamUIManager->OnEndPlay.AddSPLambda(this, [this, SubscribedWorld]()
		{
			// A handler that outlived the world it was made for must not clear the current one.
			if (World != SubscribedWorld)
			{
				return;
			}
			World = nullptr;
			RefreshContent();
		});
		OutlinerChangedHandle = DreamUIManager->OnDreamUIWidgetOutlinerChanged.AddSPLambda(this, [this]()
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
		if (const TSharedPtr<SDockTab> Tab = OwnerTab.Pin())
		{
			Tab->RequestCloseTab();
		}
	}
}
#undef LOCTEXT_NAMESPACE