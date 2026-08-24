// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUISequenceEditorToolkit.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "ISequencerModule.h"
#include "ISequencer.h"
#include "LevelEditorSequencerIntegration.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceEditorToolkit"

const FName FDreamUISequenceEditorToolkit::SequencerMainTabId(TEXT("DreamUISequenceEditor_Sequencer"));

FDreamUISequenceEditorToolkit::~FDreamUISequenceEditorToolkit()
{
	if (Sequencer.IsValid())
	{
		FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());
		Sequencer->Close();
	}
}

void FDreamUISequenceEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDreamUISequence* InSequence)
{
	Sequence = InSequence;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_DreamUISequenceEditor_Layout_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->Split
			(
				FTabManager::NewStack()
				->SetHideTabWell(true)
				->AddTab(SequencerMainTabId, ETabState::OpenedTab)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("DreamUISequenceEditorApp")), StandaloneDefaultLayout,
		/*bCreateDefaultStandaloneMenu*/true, /*bCreateDefaultToolbar*/false, InSequence);

	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.RootSequence = Sequence;
		SequencerInitParams.bEditWithinLevelEditor = false;
		SequencerInitParams.ToolkitHost = InitToolkitHost;
		SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		// The editor world: a level holding the matching presenter resolves the widget bindings
		// live, so scrubbing here previews in the level viewport.
		SequencerInitParams.PlaybackContext = TAttribute<UObject*>::CreateLambda([]() -> UObject*
		{
			return GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
		});
		SequencerInitParams.ViewParams.UniqueName = "DreamUISequenceEditor";
		SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
	}
	Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);

	FLevelEditorSequencerIntegrationOptions Options;
	Options.bRequiresLevelEvents = false;
	Options.bRequiresActorEvents = false;
	Options.bForceRefreshDetails = false;
	FLevelEditorSequencerIntegration::Get().AddSequencer(Sequencer.ToSharedRef(), Options);

	// The tab may have spawned before the sequencer existed; fill it now.
	if (const TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(SequencerMainTabId))
	{
		Tab->SetContent(Sequencer->GetSequencerWidget());
	}
}

FText FDreamUISequenceEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("ToolkitName", "DreamUI Animation Editor");
}

FName FDreamUISequenceEditorToolkit::GetToolkitFName() const
{
	return FName(TEXT("DreamUISequenceEditor"));
}

FString FDreamUISequenceEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "DreamUI Animation ").ToString();
}

void FDreamUISequenceEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(SequencerMainTabId, FOnSpawnTab::CreateSP(this, &FDreamUISequenceEditorToolkit::SpawnTab_Sequencer))
		.SetDisplayName(LOCTEXT("SequencerTab", "Sequencer"));
}

void FDreamUISequenceEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(SequencerMainTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FDreamUISequenceEditorToolkit::SpawnTab_Sequencer(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("SequencerTabLabel", "Sequencer"))
		[
			Sequencer.IsValid() ? Sequencer->GetSequencerWidget() : SNullWidget::NullWidget
		];
}

#undef LOCTEXT_NAMESPACE
