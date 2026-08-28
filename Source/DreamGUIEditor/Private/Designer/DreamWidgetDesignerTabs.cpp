// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetDesignerTabs.h"
#include "PrefabEditor/DreamWidgetBlueprintEditor.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SNullWidget.h"

#define LOCTEXT_NAMESPACE "DreamWidgetDesignerTabs"

const FName FDreamWidgetDesignerTabs::ViewportID(TEXT("DreamWidgetDesigner_Viewport"));
const FName FDreamWidgetDesignerTabs::HierarchyID(TEXT("DreamWidgetDesigner_Hierarchy"));
const FName FDreamWidgetDesignerTabs::PaletteID(TEXT("DreamWidgetDesigner_Palette"));
const FName FDreamWidgetDesignerTabs::DetailsID(TEXT("DreamWidgetDesigner_Details"));
const FName FDreamWidgetDesignerTabs::AnimationsID(TEXT("DreamWidgetDesigner_Animations"));
// Not ours to name: the sequencer invokes this id itself.
const FName FDreamWidgetDesignerTabs::SequencerCurvesID(TEXT("SequencerGraphEditor"));

FDreamWidgetDesignerTabSummoner::FDreamWidgetDesignerTabSummoner(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor,
	FName InTabID, const FText& InLabel, const FSlateIcon& InIcon, FMakeContent InMakeContent)
	: FWorkflowTabFactory(InTabID, InEditor)
	, DesignerEditor(InEditor)
	, MakeContent(MoveTemp(InMakeContent))
{
	TabLabel = InLabel;
	TabIcon = InIcon;
	bIsSingleton = true;
	ViewMenuDescription = InLabel;
	ViewMenuTooltip = InLabel;
}

TSharedRef<SWidget> FDreamWidgetDesignerTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerEditor.Pin();
	if (!Editor.IsValid() || !MakeContent)
	{
		return SNullWidget::NullWidget;
	}
	return MakeContent(*Editor);
}

TSharedRef<SDockTab> FDreamWidgetDesignerTabSummoner::SpawnTab(const FWorkflowTabSpawnInfo& Info) const
{
	TSharedRef<SDockTab> Tab = FWorkflowTabFactory::SpawnTab(Info);
	if (OnClosedCallback)
	{
		TWeakPtr<FDreamWidgetBlueprintEditor> WeakEditor = DesignerEditor;
		TFunction<void(FDreamWidgetBlueprintEditor&)> Callback = OnClosedCallback;
		Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
			[WeakEditor, Callback](TSharedRef<SDockTab>)
			{
				if (TSharedPtr<FDreamWidgetBlueprintEditor> Editor = WeakEditor.Pin())
				{
					Callback(*Editor);
				}
			}));
	}
	return Tab;
}

#undef LOCTEXT_NAMESPACE
