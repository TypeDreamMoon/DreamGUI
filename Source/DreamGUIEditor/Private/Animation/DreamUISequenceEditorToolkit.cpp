// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUISequenceEditorToolkit.h"
#include "Core/DreamUserWidget.h"
#include "Animation/DreamUISequence.h"
#include "Animation/DreamUIWidgetBinding.h"
#include "Preview/DreamWidgetDesignerScene.h"
#include "Animation/DreamUISequencePreviewViewport.h"
#include "ISequencerModule.h"
#include "ISequencer.h"
#include "LevelEditorSequencerIntegration.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "PropertyEditorModule.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneBindingReferences.h"
#include "IDetailsView.h"
#include "KeyPropertyParams.h"
#include "PropertyPath.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceEditorToolkit"

const FName FDreamUISequenceEditorToolkit::ViewportTabId(TEXT("DreamUISequenceEditor_Viewport"));
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
	// PreviewScene is declared first in the class, so it goes down after the viewport client that
	// renders it -- no member here may outlive it except the ones declared above it.
}

UWorld* FDreamUISequenceEditorToolkit::GetPreviewWorld() const
{
	return PreviewScene.IsValid() ? PreviewScene->GetWorld() : nullptr;
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
	SelectedPreviewWidgets.Reset();
}

void FDreamUISequenceEditorToolkit::RebuildPreviewTree()
{
	DestroyPreviewTree();
	UClass* WidgetClass = Sequence != nullptr ? Sequence->PreviewWidgetClass.LoadSynchronous() : nullptr;
	if (WidgetClass == nullptr || !PreviewScene.IsValid())
	{
		return;
	}
	// The root agent carries the canvas (design size, render mode) exactly the way the designer sets
	// it up; instantiating under it is what makes the tree lay out and draw. The size is the
	// designer's own default rather than anything on the asset: an animation is authored against a
	// class, and a class does not carry the screen it was drawn for.
	UDreamWidget* ParentAgent = PreviewScene->EnsureRootAgent(
		FIntPoint(1920, 1080), EDreamRenderMode::ScreenSpaceOverlay, FIntPoint(1920, 1080));
	UDreamUserWidget* Instance = ParentAgent != nullptr
		? CreateDreamWidget(PreviewScene->GetWorld(), WidgetClass, ParentAgent) : nullptr;
	UDreamWidget* Root = Instance;
	if (Root == nullptr)
	{
		return;
	}
	// Scratch objects: they must never be saved anywhere.
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(Root, AllWidgets, true);
	for (UDreamWidget* Widget : AllWidgets)
	{
		Widget->SetFlags(RF_Transient);
	}
	PreviewRoot = Root;
	Sequence->SetPreviewRoot(Root);
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	if (Sequencer.IsValid())
	{
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}
	if (PreviewViewport.IsValid() && PreviewViewport->GetPreviewClient().IsValid())
	{
		PreviewViewport->GetPreviewClient()->FocusOnPreview();
	}
}

void FDreamUISequenceEditorToolkit::OnObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	if (InObject == Sequence
		&& InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UDreamUISequence, PreviewWidgetClass))
	{
		// EnsureRootAgent is idempotent -- it hands back the agent it already made -- so the scene has
		// to go for the new class to get a canvas of its own rather than the last one's.
		DestroyPreviewTree();
		PreviewScene.Reset();
		PreviewScene = MakeUnique<FDreamWidgetDesignerScene>(
			FDreamWidgetDesignerScene::ConstructionValues()
			.AllowAudioPlayback(false)
			.ShouldSimulatePhysics(false)
			.SetEditor(true));
		RebuildPreviewTree();
	}
}

void FDreamUISequenceEditorToolkit::HealStrayBindings()
{
	if (Sequence == nullptr)
	{
		return;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	const FGuid RootGuid = Sequence->EnsureRootBinding();
	for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
	{
		FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
		if (Possessable.GetGuid() != RootGuid && !Possessable.GetParent().IsValid())
		{
			Possessable.SetParent(RootGuid, MovieScene);
		}
	}
}

FGuid FDreamUISequenceEditorToolkit::FindBindingForWidget(const UDreamWidget* InWidget) const
{
	if (Sequence == nullptr || InWidget == nullptr || PreviewRoot.Get() == nullptr)
	{
		return FGuid();
	}
	const FMovieSceneBindingReferences* References = Sequence->GetBindingReferences();
	if (References == nullptr)
	{
		return FGuid();
	}
	const FString Path = UDreamUIWidgetBinding::BuildWidgetPathFromRoot(PreviewRoot.Get(), InWidget);
	for (const FMovieSceneBindingReference& Reference : References->GetAllReferences())
	{
		const UDreamUIWidgetBinding* WidgetBinding = Cast<UDreamUIWidgetBinding>(Reference.CustomBinding);
		if (WidgetBinding != nullptr && WidgetBinding->WidgetPath == Path)
		{
			return Reference.ID;
		}
	}
	return FGuid();
}

void FDreamUISequenceEditorToolkit::SelectWidgetFromViewport(UDreamWidget* InWidget, bool bAppend)
{
	if (!bAppend)
	{
		SelectedPreviewWidgets.Reset();
	}
	if (InWidget != nullptr)
	{
		SelectedPreviewWidgets.AddUnique(InWidget);
	}

	// Mirror into the Sequencer; the guard keeps the echo from re-entering the viewport selection.
	if (Sequencer.IsValid() && InWidget != nullptr)
	{
		const FGuid BindingGuid = FindBindingForWidget(InWidget);
		if (BindingGuid.IsValid())
		{
			++SelectionSyncGuard;
			Sequencer->SelectObject(BindingGuid);
			--SelectionSyncGuard;
		}
	}
	if (PreviewViewport.IsValid() && PreviewViewport->GetPreviewClient().IsValid())
	{
		PreviewViewport->GetPreviewClient()->Invalidate();
	}
}

void FDreamUISequenceEditorToolkit::HandleSequencerSelectionChanged(TArray<FGuid> InObjectGuids)
{
	if (SelectionSyncGuard != 0 || Sequence == nullptr)
	{
		return;
	}
	const FMovieSceneBindingReferences* References = Sequence->GetBindingReferences();
	UDreamWidget* Root = PreviewRoot.Get();
	if (References == nullptr || Root == nullptr)
	{
		return;
	}
	SelectedPreviewWidgets.Reset();
	for (const FGuid& Guid : InObjectGuids)
	{
		for (const FMovieSceneBindingReference& Reference : References->GetReferences(Guid))
		{
			const UDreamUIWidgetBinding* WidgetBinding = Cast<UDreamUIWidgetBinding>(Reference.CustomBinding);
			if (WidgetBinding == nullptr)
			{
				continue;
			}
			if (UDreamWidget* Widget = UDreamUIWidgetBinding::ResolveWidgetPath(Root, WidgetBinding->WidgetPath))
			{
				SelectedPreviewWidgets.AddUnique(Widget);
			}
		}
	}
	if (PreviewViewport.IsValid() && PreviewViewport->GetPreviewClient().IsValid())
	{
		PreviewViewport->GetPreviewClient()->Invalidate();
	}
}

void FDreamUISequenceEditorToolkit::KeyTransformProperties(const TArray<UDreamWidget*>& InWidgets, bool bRenderTranslation, bool bRelativeLocation, bool bRotation, bool bScale)
{
	if (!Sequencer.IsValid())
	{
		return;
	}
	TArray<FName, TInlineAllocator<4>> PropertyNames;
	if (bRenderTranslation)
	{
		PropertyNames.Add(TEXT("RenderTranslation"));
	}
	if (bRelativeLocation)
	{
		PropertyNames.Add(TEXT("RelativeLocation"));
	}
	if (bRotation)
	{
		// The quaternion is not animatable; the euler mirror is the rotation entry point Sequencer drives.
		PropertyNames.Add(TEXT("RelativeRotationEuler"));
	}
	if (bScale)
	{
		PropertyNames.Add(TEXT("RelativeScale"));
	}
	if (PropertyNames.IsEmpty() || InWidgets.IsEmpty())
	{
		return;
	}

	for (UDreamWidget* Widget : InWidgets)
	{
		if (Widget == nullptr)
		{
			continue;
		}
		// Make sure a binding exists whatever the auto-key setting; the key itself still obeys it.
		Sequencer->GetHandleToObject(Widget, true);
		for (const FName& PropertyName : PropertyNames)
		{
			FProperty* Property = UDreamWidget::StaticClass()->FindPropertyByName(PropertyName);
			if (Property == nullptr)
			{
				continue;
			}
			FPropertyPath PropertyPath;
			PropertyPath.AddProperty(FPropertyInfo(Property));
			FKeyPropertyParams KeyParams(TArray<UObject*>{ Widget }, PropertyPath, ESequencerKeyMode::AutoKey);
			Sequencer->KeyProperty(KeyParams);
		}
	}
	// GetHandleToObject's engine create path does not parent under the root binding; adopt now so
	// the re-rooting scheme keeps working without waiting for the next editor open.
	HealStrayBindings();
}

void FDreamUISequenceEditorToolkit::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UDreamUISequence* InSequence)
{
	Sequence = InSequence;

	// Built before InitAssetEditor: the viewport tab spawns inside it and its client asks for this
	// world immediately.
	PreviewScene = MakeUnique<FDreamWidgetDesignerScene>(
		FDreamWidgetDesignerScene::ConstructionValues()
		.AllowAudioPlayback(false)
		.ShouldSimulatePhysics(false)
		.SetEditor(true));

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_DreamUISequenceEditor_Layout_v3")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation(Orient_Horizontal)
				->SetSizeCoefficient(0.55f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.65f)
					->SetHideTabWell(true)
					->AddTab(ViewportTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.35f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.45f)
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
		// Standalone opens hand in a null InitToolkitHost; InitAssetEditor above has created this
		// toolkit's own host by now, and CreateSequencer asserts on a null one.
		SequencerInitParams.ToolkitHost = InitToolkitHost.IsValid() ? InitToolkitHost : GetToolkitHost();
		SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		// The toolkit's private preview world: the bindings resolve against the preview tree in it,
		// so scrubbing previews in this editor's own viewport and never touches any open level.
		SequencerInitParams.PlaybackContext = TAttribute<UObject*>::CreateLambda([this]() -> UObject*
		{
			return GetPreviewWorld();
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

	Sequencer->GetSelectionChangedObjectGuids().AddSP(this, &FDreamUISequenceEditorToolkit::HandleSequencerSelectionChanged);

	// The tab may have spawned before the sequencer existed; fill it now.
	if (const TSharedPtr<SDockTab> Tab = TabManager->FindExistingLiveTab(SequencerMainTabId))
	{
		Tab->SetContent(Sequencer->GetSequencerWidget());
	}

	// Bindings created before the parenting fix (or by other tools) float free; the re-rooting
	// scheme needs every widget binding under the root, so adopt strays on open.
	HealStrayBindings();

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
	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FDreamUISequenceEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"));
	InTabManager->RegisterTabSpawner(SequencerMainTabId, FOnSpawnTab::CreateSP(this, &FDreamUISequenceEditorToolkit::SpawnTab_Sequencer))
		.SetDisplayName(LOCTEXT("SequencerTab", "Sequencer"));
	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FDreamUISequenceEditorToolkit::SpawnTab_Details))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"));
}

void FDreamUISequenceEditorToolkit::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(SequencerMainTabId);
	InTabManager->UnregisterTabSpawner(ViewportTabId);
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);
}

TSharedRef<SDockTab> FDreamUISequenceEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	if (!PreviewViewport.IsValid())
	{
		PreviewViewport = SNew(SDreamUISequencePreviewViewport, SharedThis(this));
	}
	return SNew(SDockTab)
		.Label(LOCTEXT("ViewportTabLabel", "Viewport"))
		[
			PreviewViewport.ToSharedRef()
		];
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
