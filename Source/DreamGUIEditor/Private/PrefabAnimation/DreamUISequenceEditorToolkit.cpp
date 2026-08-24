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
#include "PrefabSystem/DreamUIPrefab.h"
#include "Core/Components/DreamWidget.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceEditorToolkit"

const FName FDreamUISequenceEditorToolkit::SequencerMainTabId(TEXT("DreamUISequenceEditor_Sequencer"));
const FName FDreamUISequenceEditorToolkit::DetailsTabId(TEXT("DreamUISequenceEditor_Details"));

FDreamUISequenceEditorToolkit::~FDreamUISequenceEditorToolkit()
{
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	}
	DestroyPreviewTree();
	if (Sequencer.IsValid())
	{
		FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());
		Sequencer->Close();
	}
}

void FDreamUISequenceEditorToolkit::DestroyPreviewTree()
{
	if (Sequence != nullptr && Sequence->GetPreviewRoot() == PreviewRoot.Get())
	{
		Sequence->SetPreviewRoot(nullptr);
	}
	if (UDreamWidget* Root = PreviewRoot.Get())
	{
		Root->DestroyWidget();
	}
	PreviewRoot.Reset();
}

void FDreamUISequenceEditorToolkit::RebuildPreviewTree()
{
	DestroyPreviewTree();
	UDreamUIPrefab* Prefab = Sequence != nullptr ? Sequence->PreviewPrefab.LoadSynchronous() : nullptr;
	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (Prefab == nullptr || World == nullptr)
	{
		return;
	}
	UDreamWidget* Root = Prefab->LoadPrefab(World, nullptr);
	if (Root == nullptr)
	{
		return;
	}
	// Scratch objects: they must never be saved into whatever map happens to be open.
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(Root, AllWidgets, true);
	for (UDreamWidget* Widget : AllWidgets)
	{
		Widget->SetFlags(RF_Transient);
	}
	PreviewRoot = Root;
	Sequence->SetPreviewRoot(Root);
	if (Sequencer.IsValid())
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
}

void FDreamUISequenceEditorToolkit::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	if (InObject == Sequence
		&& InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDreamUISequence, PreviewPrefab))
	{
		RebuildPreviewTree();
	}
}

void FDreamUISequenceEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDreamUISequence* InSequence)
{
	Sequence = InSequence;

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_DreamUISequenceEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Horizontal)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.75f)
				->SetHideTabWell(true)
				->AddTab(SequencerMainTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.25f)
				->AddTab(DetailsTabId, ETabState::OpenedTab)
			)
		);

	InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("DreamUISequenceEditorApp")), StandaloneDefaultLayout,
		/*bCreateDefaultStandaloneMenu*/true, /*bCreateDefaultToolbar*/false, InSequence);

	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.RootSequence = Sequence;
		SequencerInitParams.bEditWithinLevelEditor = false;
		// Standalone opens hand in a null InitToolkitHost; InitAssetEditor above has created this
		// toolkit's own host by now, and CreateSequencer asserts on a null one.
		SequencerInitParams.ToolkitHost = InitToolkitHost.IsValid() ? InitToolkitHost : GetToolkitHost();
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

	// The preview tree makes the bindings real while editing; rebuild it when the prefab changes.
	RebuildPreviewTree();
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDreamUISequenceEditorToolkit::OnObjectPropertyChanged);
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
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FDreamUISequenceEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"));
}

void FDreamUISequenceEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(SequencerMainTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FDreamUISequenceEditorToolkit::SpawnTab_Details(const FSpawnTabArgs& Args)
{
	if (!DetailsView.IsValid())
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsView = PropertyModule.CreateDetailView(DetailsViewArgs);
		DetailsView->SetObject(Sequence);
	}
	return SNew(SDockTab)
		.Label(LOCTEXT("DetailsTabLabel", "Details"))
		[
			DetailsView.ToSharedRef()
		];
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
