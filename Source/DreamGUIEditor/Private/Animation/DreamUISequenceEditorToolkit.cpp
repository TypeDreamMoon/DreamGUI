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
#include "Framework/Notifications/NotificationManager.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Docking/SDockTab.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "PropertyEditorModule.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneBindingReferences.h"
#include "IDetailsView.h"
#include "KeyPropertyParams.h"
#include "Misc/ITransaction.h"//FTransactionContext (MatchesContext)
#include "Misc/TransactionObjectEvent.h"//FTransactionObjectEvent (MatchesContext)
#include "PropertyPath.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceEditorToolkit"

const FName FDreamUISequenceEditorToolkit::ViewportTabId(TEXT("DreamUISequenceEditor_Viewport"));
const FName FDreamUISequenceEditorToolkit::SequencerMainTabId(TEXT("DreamUISequenceEditor_Sequencer"));
const FName FDreamUISequenceEditorToolkit::DetailsTabId(TEXT("DreamUISequenceEditor_Details"));

namespace DreamUISequenceEditorToolkitLocal
{
	/**
	 * The preview scene, made the same way when the editor opens and on every change of preview class.
	 *
	 * Not transactional, like the engine's thumbnail scenes: the world, its level and its world settings are never
	 * recorded by a transaction. That alone does not keep the scene out of one -- the actors a new world spawns for
	 * itself afterwards (the default physics volume, the builder brush, the gameplay debugger's manager) are spawned
	 * transactional whatever the scene asked -- so a change of preview class builds its scene with recording switched
	 * off as well (RebuildPreviewForNewClass). Nothing in a preview needs undo: all of it is rebuilt from the asset.
	 */
	TUniquePtr<FDreamWidgetDesignerScene> MakePreviewScene()
	{
		return MakeUnique<FDreamWidgetDesignerScene>(
			FDreamWidgetDesignerScene::ConstructionValues()
			.AllowAudioPlayback(false)
			.ShouldSimulatePhysics(false)
			.SetTransactional(false)
			.SetEditor(true));
	}
}

// Defaulted here rather than left implicit: the implicit one would instantiate the TUniquePtr deleter
// for the forward-declared FDreamWidgetDesignerScene in every translation unit that creates a toolkit,
// which a unity build hid by having this .cpp in the same blob.
FDreamUISequenceEditorToolkit::FDreamUISequenceEditorToolkit() = default;

FDreamUISequenceEditorToolkit::~FDreamUISequenceEditorToolkit()
{
	if (UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get())
	{
		EditorSubsystem->UnregisterPreview(this);
	}
	if (PropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	}
	// The sequencer closes FIRST, while the widgets it has bound are still alive. Its entity groups
	// are keyed on raw bound-object pointers and nothing repairs those keys when the objects go, so
	// destroying the preview tree under a live sequencer left it holding keys that named freed
	// widgets -- the same failure the embedded animation editor evacuates around.
	if (Sequencer.IsValid())
	{
		FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());
		Sequencer->Close();
		Sequencer.Reset();
	}
	DestroyPreviewTree();
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

void FDreamUISequenceEditorToolkit::EvacuateSequencerEntities()
{
	if (!Sequencer.IsValid() || bPlaybackContextSuppressed || Sequence == nullptr)
	{
		return;
	}
	if (!SuppressionContext.IsValid())
	{
		SuppressionContext = TStrongObjectPtr<UObject>(
			NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transient));
	}
	// Before the evaluation, not after: writing saved values back needs the objects they were saved
	// onto, and a moment later those objects are gone.
	Sequencer->RestorePreAnimatedState();
	bPlaybackContextSuppressed = true;
	// A childless root is what makes every path in this sequence resolve to nothing; the forced
	// evaluation below is what makes the engine unlink the entities keyed on the old tree.
	Sequence->SetPreviewRoot(Cast<UDreamWidget>(SuppressionContext.Get()));
	Sequencer->ForceEvaluate();
}

void FDreamUISequenceEditorToolkit::ResumeSequencerEvaluation()
{
	if (!bPlaybackContextSuppressed)
	{
		return;
	}
	bPlaybackContextSuppressed = false;
	if (Sequence != nullptr && Sequence->GetPreviewRoot() == Cast<UDreamWidget>(SuppressionContext.Get()))
	{
		// Nothing put a real tree back -- the class is unset, or instantiation failed -- so the
		// sentinel must not be left standing in for one.
		Sequence->SetPreviewRoot(PreviewRoot.Get());
	}
	if (Sequencer.IsValid())
	{
		// Resolved bindings still name the tree that has just been destroyed.
		Sequencer->GetEvaluationState()->ClearObjectCaches(*Sequencer);
		Sequencer->ForceEvaluate();
	}
}

void FDreamUISequenceEditorToolkit::RebuildPreviewTree()
{
	// Every widget this sequencer has bound is about to be destroyed; step its entity runtime out
	// first. No-op when an outer window (a PreviewWidgetClass change) has already evacuated.
	EvacuateSequencerEntities();
	DestroyPreviewTree();
	// What this build is FROM, whether or not it succeeds: an undo compares against it (HandleUndoRedo), and a
	// class that fails to load must not be retried on every undo that leaves it in place.
	BuiltPreviewClassPath = Sequence != nullptr ? Sequence->PreviewWidgetClass.ToSoftObjectPath() : FSoftObjectPath();
	UClass* WidgetClass = Sequence != nullptr ? Sequence->PreviewWidgetClass.LoadSynchronous() : nullptr;
	if (WidgetClass == nullptr || !PreviewScene.IsValid())
	{
		ResumeSequencerEvaluation();
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
		ResumeSequencerEvaluation();
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
	// The tree the bindings resolve against exists again, so the sequencer may hold entities keyed on
	// it once more. Before NotifyMovieSceneDataChanged, so the structure change is told about the new
	// tree rather than about the sentinel.
	ResumeSequencerEvaluation();
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
		RebuildPreviewForNewClass();
	}
}

void FDreamUISequenceEditorToolkit::RebuildPreviewForNewClass()
{
	// Recorded by no transaction, the way the engine reruns construction scripts (AActor::RerunConstructionScripts).
	// The details panel reports a PreviewWidgetClass edit while its transaction is still open, and in the editor every
	// transactional object constructed under an open transaction is recorded as created by it
	// (StaticConstructObject_Internal): the new world's own actors among them. Undoing the edit marked them garbage, and
	// redoing it brought them back and registered their components into the world the undo's rebuild (HandleUndoRedo)
	// had already destroyed. Only the preview is built and torn down below; the asset keeps nothing of it but its
	// preview root, a plain pointer. An undo or a redo has no transaction open, so there this changes nothing.
	TGuardValue<ITransaction*> SuppressTransaction(GUndo, nullptr);
	// The whole scene goes here, preview tree included, so the sequencer has to be stepped out of
	// its entities BEFORE the destruction starts -- RebuildPreviewTree's own evacuation at the end
	// of this function would come too late for the widgets destroyed on the next line.
	EvacuateSequencerEntities();
	// EnsureRootAgent is idempotent -- it hands back the agent it already made -- so the scene has
	// to go for the new class to get a canvas of its own rather than the last one's.
	DestroyPreviewTree();
	PreviewScene.Reset();
	PreviewScene = DreamUISequenceEditorToolkitLocal::MakePreviewScene();
	RebuildPreviewTree();
}

bool FDreamUISequenceEditorToolkit::MatchesContext(const FTransactionContext& InContext,
	const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjectContexts) const
{
	if (Sequence == nullptr)
	{
		return false;
	}
	if (TransactionObjectContexts.Num() == 0)
	{
		// A transaction that names nothing says nothing about what it touched, and the engine's default for
		// the context-less case is to tell everyone. Kept, as the designer keeps it.
		return true;
	}
	// Only a transaction that touched this asset: the rebuild an undo can cause is a whole preview scene, and
	// an undo of an actor in the level, or of another asset, is none of this editor's business.
	const UPackage* OwnPackage = Sequence->GetOutermost();
	for (const TPair<UObject*, FTransactionObjectEvent>& Entry : TransactionObjectContexts)
	{
		if (Entry.Key != nullptr && Entry.Key->GetOutermost() == OwnPackage)
		{
			return true;
		}
	}
	return false;
}

void FDreamUISequenceEditorToolkit::PostUndo(bool bSuccess)
{
	HandleUndoRedo(bSuccess);
}

void FDreamUISequenceEditorToolkit::PostRedo(bool bSuccess)
{
	HandleUndoRedo(bSuccess);
}

void FDreamUISequenceEditorToolkit::HandleUndoRedo(bool bSuccess)
{
	if (!bSuccess || Sequence == nullptr)
	{
		return;
	}
	// Only when the class the asset names now is not the one the preview was built from: every other undo on
	// this asset -- a key moved, a binding renamed -- leaves the preview tree it resolves against alone.
	if (Sequence->PreviewWidgetClass.ToSoftObjectPath() != BuiltPreviewClassPath)
	{
		RebuildPreviewForNewClass();
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
	PreviewScene = DreamUISequenceEditorToolkitLocal::MakePreviewScene();

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
	// AFTER the rebuild, which is the only thing that ever assigns PreviewRoot. Run before it, the
	// audit's own "is there a preview tree" guard was false every single time an asset was opened,
	// so the one diagnostic a broken binding has never once appeared.
	ReportUnresolvableBindings();
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FDreamUISequenceEditorToolkit::OnObjectPropertyChanged);
	if (UDreamGUIEditorSubsystem* EditorSubsystem = UDreamGUIEditorSubsystem::Get())
	{
		EditorSubsystem->RegisterPreview(this);
	}
}

bool FDreamUISequenceEditorToolkit::UsesClass(const UClass* InClass) const
{
	UDreamWidget* Root = PreviewRoot.Get();
	if (InClass == nullptr || Root == nullptr)
	{
		return false;
	}
	TArray<UDreamWidget*> Widgets;
	UDreamWidget::CollectChildrenWidgets(Root, Widgets, true);
	return Widgets.ContainsByPredicate([InClass](const UDreamWidget* Widget) { return Widget->IsA(InClass); });
}

void FDreamUISequenceEditorToolkit::ReleaseForRecompile()
{
	EvacuateSequencerEntities();
	DestroyPreviewTree();
}

void FDreamUISequenceEditorToolkit::RebuildAfterRecompile()
{
	RebuildPreviewTree();
}

void FDreamUISequenceEditorToolkit::ReportUnresolvableBindings()
{
	// An audit of every widget binding against the live preview tree. A path this cannot resolve is a
	// track that will silently drive nothing -- the classic aftermath of a widget rename that this
	// asset was not loaded for -- and the moment the asset is opened is the one moment someone is
	// looking at it.
	if (Sequence == nullptr || !PreviewRoot.IsValid())
	{
		return;
	}
	TArray<FString> DeadPaths;
	TArray<FString> HealedPaths;
	for (FMovieSceneBindingReference& Reference : Sequence->BindingReferences.GetAllReferences())
	{
		UDreamUIWidgetBinding* WidgetBinding = Cast<UDreamUIWidgetBinding>(Reference.CustomBinding.Get());
		if (WidgetBinding == nullptr || WidgetBinding->WidgetPath.IsEmpty())
		{
			continue;
		}
		UDreamWidget* Resolved = UDreamUIWidgetBinding::ResolveWidgetPath(PreviewRoot.Get(), WidgetBinding->WidgetPath);
		if (Resolved == nullptr)
		{
			DeadPaths.Add(WidgetBinding->WidgetPath);
			continue;
		}
		// Resolution is move-tolerant -- it falls back to the widget's own name when the path stops
		// walking -- so a path that resolved may still be the OLD path. Record the new one while the
		// tree that proves it is in front of us: an asset holds no pointer to heal from later, and
		// the fallback only works while the name stays unique.
		const FString CurrentPath = UDreamUIWidgetBinding::BuildWidgetPathFromRoot(PreviewRoot.Get(), Resolved);
		if (!CurrentPath.IsEmpty() && CurrentPath != WidgetBinding->WidgetPath)
		{
			HealedPaths.Add(FString::Printf(TEXT("%s -> %s"), *WidgetBinding->WidgetPath, *CurrentPath));
			WidgetBinding->Modify();
			WidgetBinding->WidgetPath = CurrentPath;
		}
	}
	if (HealedPaths.Num() > 0)
	{
		Sequence->MarkPackageDirty();
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("DreamUISequenceEditor", "HealedBindings",
				"{0} binding(s) in this sequence named widgets that have since been moved, and have been repointed: {1}. Save the asset to keep the repair."),
			HealedPaths.Num(), FText::FromString(FString::Join(HealedPaths, TEXT(", ")))));
		Info.ExpireDuration = 10.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
	if (DeadPaths.Num() > 0)
	{
		FNotificationInfo Info(FText::Format(
			NSLOCTEXT("DreamUISequenceEditor", "UnresolvableBindings",
				"{0} binding(s) in this sequence name widgets the preview tree does not have: {1}. A rename in the widget class probably happened while this asset was unloaded -- repoint them, or re-add the '(was:)' line and recompile the class with this asset open."),
			DeadPaths.Num(), FText::FromString(FString::Join(DeadPaths, TEXT(", ")))));
		Info.ExpireDuration = 12.0f;
		FSlateNotificationManager::Get().AddNotification(Info);
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
