// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetAnimationEditorWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUserWidget.h"

#include "Animation/DreamWidgetAnimation.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelEditorSequencerIntegration.h"
#include "SSCSEditor.h"
#include "EditorUndoClient.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Utils/DreamUIUtils.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "DreamWidgetBlueprint.h"
#include "LevelEditor.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamText.h"
#include "Controls/DreamUIControl.h"
#include "Designer/DreamWidgetHierarchyPickerView.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Materials/MaterialInterface.h"
#include "Animation/MovieSceneDreamUIMaterialTrack.h"

#define LOCTEXT_NAMESPACE "DreamWidgetAnimationEditorWidget"

/**
 * Whether a track may be bound to InWidget at all.
 *
 * A widget instanced by a sub-prefab is not serialized into this prefab, so the only persistent half
 * of a binding -- the direct HelperWidget pointer -- is dropped when the prefab is saved. The track
 * animates correctly right up until the next load, then silently binds nothing; a cooked build does
 * not even carry the editor-only HelperWidgetPath that "Try fix object reference" repairs from. The
 * animation host search in DreamUIPrefabSequenceEditor.cpp skips sub-prefab widgets for the same
 * reason.
 *
 * Declared here rather than in the widget's header because that header describes the Slate class,
 * which no headless test can construct; DreamPanelsAuditAutomationTests declares this prototype.
 */
bool DreamWidgetAnimation_CanBindWidgetToSequencer(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	// The refusal this used to make was "a sub-prefab widget's binding does not survive a save", and
	// when the prefab model went it was rewritten to allow everything -- correct at the time, because
	// nothing was nested yet. Nesting came back as widget blueprint instances, and the same objection
	// with it: a binding is a chain of DISPLAY NAMES resolved through Children at play time, so one
	// that reaches into a nested instance is a name path through somebody else's asset. That asset's
	// author renames a widget and every host animating it silently drives nothing -- the host's
	// compiler never looks inside a class it merely places.
	//
	// Named-slot content is deliberately NOT caught by this. It sits inside the nested instance's
	// subtree but is outered to the HOST's tree, because the host authored it; it is the host's
	// widget standing in a hole, and animating your own widget is the whole point.
	for (const UDreamWidget* Ancestor = InWidget->GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		const UDreamUserWidget* Nested = Cast<UDreamUserWidget>(Ancestor);
		if (Nested == nullptr)
		{
			continue;
		}
		// The first widget blueprint instance above this widget. If it is the one being edited, the
		// widget is part of what is being edited and is bindable.
		if (DreamWidget_ShouldEditorExpandContents(Ancestor))
		{
			return true;
		}
		return !InWidget->IsIn(Nested->GetWidgetTree());
	}
	return true;
}

class SDreamWidgetAnimationEditorWidgetImpl : public SCompoundWidget, public FEditorUndoClient
{
public:

	bool bUpdatingSequencerSelection = false;

	SLATE_BEGIN_ARGS(SDreamWidgetAnimationEditorWidgetImpl){}
	SLATE_END_ARGS();

	void Close()
	{
		if (Sequencer.IsValid())
		{
			Sequencer->SetShowCurveEditor(false);
			FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());
			Sequencer->Close();
			Sequencer = nullptr;
		}

		GEditor->UnregisterForUndo(this);
	}

	~SDreamWidgetAnimationEditorWidgetImpl()
	{
		if (GEditor != nullptr)
		{
			GEditor->OnBlueprintPreCompile().Remove(BlueprintPreCompileHandle);
			GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle);
		}

		Close();

		// Un-Register sequencer menu extenders.
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
			{
				return SequencerAddTrackExtenderHandle == Extender.GetHandle();
			});
	}
	
	void SetToolkitHost(TSharedPtr<IToolkitHost> InToolkitHost)
	{
		if (InToolkitHost.IsValid())
		{
			ToolkitHost = InToolkitHost;
		}
	}

	void Construct(const FArguments&)
	{
		if (GEditor != nullptr)
		{
			// See EvacuateSequencerEntities: a recompile of the edited asset replaces the preview
			// objects this sequencer binds, and that is only survivable from in front of it.
			BlueprintPreCompileHandle = GEditor->OnBlueprintPreCompile().AddSP(
				this, &SDreamWidgetAnimationEditorWidgetImpl::HandleBlueprintPreCompile);
			BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddSP(
				this, &SDreamWidgetAnimationEditorWidgetImpl::HandleBlueprintCompiled);
		}

		NoAnimationTextBlock =
			SNew(STextBlock)
			.TextStyle(FAppStyle::Get(), "UMGEditor.NoAnimationFont")
			.Text(LOCTEXT("NoAnimationSelected", "No Animation Selected"));

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Content, SBox)
				.MinDesiredHeight(200)
			]
			+SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				NoAnimationTextBlock.ToSharedRef()
			]
		];

		GEditor->RegisterForUndo(this);
		// A fallback only: the designer injects its own host via SetToolkitHost so side panels
		// (the curve editor) open in its window rather than the level editor's.
		ToolkitHost = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();

		// Register sequencer menu extenders.
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		{
			int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
				FAssetEditorExtender::CreateRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::GetAddTrackSequencerExtender));
			SequencerAddTrackExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
		}
	}


	virtual void PostUndo(bool bSuccess) override
	{
		if (!GetAnimation())
		{
			Close();
		}
	}

	FText GetDisplayLabel() const
	{
		UDreamWidgetAnimation* Sequence = WeakSequence.Get();
		return Sequence ? Sequence->GetDisplayName() : LOCTEXT("DefaultSequencerLabel", "Sequencer");
	}

	TSharedPtr<ISequencer> GetSequencer() const
	{
		return Sequencer;
	}

	UDreamWidgetAnimation* GetAnimation() const
	{
		return WeakSequence.Get();
	}

	/**
	 * The object the sequencer may actually possess, given one the author picked.
	 *
	 * The two trees are the whole of it. An animation is authored data, so it lives on the ASSET's
	 * tree, and everything that offers a widget to bind -- the hierarchy picker, the designer
	 * selection -- offers one from there. Playback is against the PREVIEW, because that is the copy
	 * that is alive and can be scrubbed. UDreamWidgetAnimation::CanPossessObject requires the object
	 * and the context to share a world, and an authoring widget lives in no world at all, so handing
	 * it straight to GetHandleToObject was refused -- silently, with no log, which is what "adding a
	 * track does nothing" looked like from the outside.
	 *
	 * Translating here rather than relaxing CanPossessObject: the world check is what keeps one
	 * designer's sequencer from possessing another's widgets, and a binding stored against an object
	 * that is not in the playback context resolves to nothing at play time anyway. The binding is
	 * name-based, so the preview and the authored widget produce the same path -- which is exactly
	 * why the sequence can answer for either tree once the handle is taken against the live one.
	 */
	UObject* ResolveBindableObject(UObject* InPicked) const
	{
		UDreamWidget* Widget = Cast<UDreamWidget>(InPicked);
		if (Widget == nullptr && InPicked != nullptr)
		{
			Widget = InPicked->GetTypedOuter<UDreamWidget>();
		}
		if (Widget == nullptr)
		{
			return InPicked;
		}
		// Already the live copy -- the selection can hand us either, depending on which panel it
		// came from -- so nothing to translate.
		if (Widget->GetWorld() != nullptr)
		{
			return InPicked;
		}
		UDreamWidget* Preview = FDreamWidgetBlueprintEditor::FindPreviewForAnimationContext(Widget);
		if (Preview == nullptr)
		{
			return nullptr;
		}
		// A sub-object of the widget (a visual, a component) has to follow its widget across, and
		// the only honest way back is by the same name the picker showed.
		if (Widget == InPicked)
		{
			return Preview;
		}
		return Preview->GetVisual() != nullptr && Preview->GetVisual()->GetClass() == InPicked->GetClass()
			? static_cast<UObject*>(Preview->GetVisual())
			: static_cast<UObject*>(Preview);
	}

	UObject* GetPlaybackContext() const
	{
		// While evacuating (see EvacuateSequencerEntities) the sequencer must see no context: the
		// forced evaluation against nowhere is what makes the engine let go of everything it holds.
		if (bPlaybackContextSuppressed)
		{
			return nullptr;
		}
		if (auto LocalAnimation = GetAnimation())
		{
			auto Component = LocalAnimation->GetTypedOuter<UDreamWidgetAnimationComponent>();
			UDreamWidget* Owner = Component != nullptr ? Component->GetWidget() : nullptr;
			// The animation is authored on the asset, where nothing is alive to watch it on. Scrub
			// against the PREVIEW instead -- possible at all because bindings resolve by name against
			// their context, so one sequence answers for either tree.
			if (UDreamWidget* Preview = FDreamWidgetBlueprintEditor::FindPreviewForAnimationContext(Owner))
			{
				return Preview;
			}
			return Owner;
		}
		
		return nullptr;
	}

	TArray<UObject*> GetEventContexts() const
	{
		TArray<UObject*> Contexts;
		if (auto* Context = GetPlaybackContext())
		{
			Contexts.Add(Context);
		}
		return Contexts;
	}

	auto GetNullSequence()
	{
		static UDreamWidgetAnimation* NullSequence = nullptr;
		if (!NullSequence)
		{
			NullSequence = NewObject<UDreamWidgetAnimation>(GetTransientPackage(), NAME_None);
			NullSequence->AddToRoot();
			NullSequence->GetMovieScene()->SetDisplayRate(FFrameRate(30, 1));
		}
		return NullSequence;
	}

	void SetDreamWidgetAnimation(UDreamWidgetAnimation* NewSequence)
	{
		if (UDreamWidgetAnimation* OldSequence = WeakSequence.Get())
		{
			if (OnSequenceChangedHandle.IsValid())
			{
				OldSequence->OnSignatureChanged().Remove(OnSequenceChangedHandle);
			}
		}

		WeakSequence = NewSequence;

		if (NewSequence)
		{
			OnSequenceChangedHandle = NewSequence->OnSignatureChanged().AddSP(this, &SDreamWidgetAnimationEditorWidgetImpl::OnSequenceChanged);
		}

		if (NewSequence == nullptr)
		{
			Content->SetEnabled(false);
			NoAnimationTextBlock->SetVisibility(EVisibility::Visible);
		}
		else
		{
			Content->SetEnabled(true);
			NoAnimationTextBlock->SetVisibility(EVisibility::Collapsed);
		}

		// If we already have a sequencer open, just assign the sequence
		if (Sequencer.IsValid() && NewSequence)
		{
			if (Sequencer->GetRootMovieSceneSequence() != NewSequence)
			{
				Sequencer->ResetToNewRootSequence(*NewSequence);
			}
			return;
		}

		// If we're setting the sequence to none, destroy sequencer
		if (!NewSequence)
		{
			if (Sequencer.IsValid())
			{
				StopObservingWidgetSelection();
				StopObservingPreviewRebuild();
				Sequencer->SetShowCurveEditor(false);
				FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());
				Sequencer->Close();
				Sequencer = nullptr;
			}

			Content->SetContent(SNew(STextBlock).Text(LOCTEXT("NothingSelected", "Select a sequence")));
			return;
		}

		// We need to initialize a new sequencer instance
		FSequencerInitParams SequencerInitParams;
		{
			TWeakObjectPtr<UDreamWidgetAnimation> LocalWeakSequence = NewSequence;

			SequencerInitParams.RootSequence = NewSequence ? NewSequence : GetNullSequence();
			SequencerInitParams.EventContexts = TAttribute<TArray<UObject*>>(this, &SDreamWidgetAnimationEditorWidgetImpl::GetEventContexts);
			SequencerInitParams.PlaybackContext = TAttribute<UObject*>(this, &SDreamWidgetAnimationEditorWidgetImpl::GetPlaybackContext);

			TSharedRef<FExtender> AddMenuExtender = MakeShareable(new FExtender);

			AddMenuExtender->AddMenuExtension("AddTracks", EExtensionHook::Before, nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::AddPossessMenuExtensions)
			);

			SequencerInitParams.ViewParams.bReadOnly = !NewSequence->IsEditable();
			SequencerInitParams.ViewParams.AddMenuExtender = AddMenuExtender;
			SequencerInitParams.ViewParams.UniqueName = "EmbeddedDreamWidgetAnimationEditor";
			SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
			SequencerInitParams.ViewParams.OnReceivedFocus.BindRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::OnSequencerReceivedFocus);
			SequencerInitParams.ViewParams.OnBuildCustomContextMenuForGuid = FOnBuildCustomContextMenuForGuid::CreateSP(this, &SDreamWidgetAnimationEditorWidgetImpl::BuildBindingContextMenu);
			SequencerInitParams.bEditWithinLevelEditor = false;
			SequencerInitParams.ToolkitHost = ToolkitHost.Pin();
			SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		}

		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
		Content->SetContent(Sequencer->GetSequencerWidget());
		Sequencer->GetSelectionChangedObjectGuids().AddSP(this, &SDreamWidgetAnimationEditorWidgetImpl::SyncSelectedWidgetsWithSequencerSelection);
		ObserveWidgetSelection();
		ObservePreviewRebuild();
		Sequencer->OnMovieSceneBindingsChanged().AddLambda([=, this]() {
			if (!WeakSequence.IsValid())return;
			auto MovieScene = WeakSequence->GetMovieScene();
			if (!IsValid(MovieScene))return;
			auto& Bindings = static_cast<const UMovieScene*>(MovieScene)->GetBindings();
			for (auto& BindingItem : Bindings)
			{
				auto ObjectArray = Sequencer->FindObjectsInCurrentSequence(BindingItem.GetObjectGuid());
				if (ObjectArray.Num() > 0)
				{
					if (auto Widget = Cast<UDreamWidget>(ObjectArray[0]))
					{
						MovieScene->SetObjectDisplayName(BindingItem.GetObjectGuid(), FText::FromString(Widget->GetDisplayName()));
					}
				}
			}
			});

		FLevelEditorSequencerIntegrationOptions Options;
		Options.bRequiresLevelEvents = false;
		Options.bRequiresActorEvents = false;
		Options.bForceRefreshDetails = false;

		FLevelEditorSequencerIntegration::Get().AddSequencer(Sequencer.ToSharedRef(), Options);
	}

	// sequence select widget handler
	void SyncSelectedWidgetsWithSequencerSelection(TArray<FGuid> ObjectGuids)
	{
		if (Sequencer == nullptr || bUpdatingSequencerSelection)
		{
			return;
		}

		TGuardValue<bool> Guard(bUpdatingSequencerSelection, true);

		UMovieSceneSequence* AnimationSequence = Sequencer->GetFocusedMovieSceneSequence();
		// The context a binding resolves against is a WIDGET -- UDreamWidgetAnimation::LocateBoundObjects
		// casts it to one and walks the name path from there -- and the world context is an object that
		// has a world. This passed the SEQUENCE for both: the cast to UDreamWidget silently failed, so
		// resolution fell back to the stored pointer, and CreateTransientSharedPlaybackState's
		// verify(WorldContext && Sequence) took a null, because an animation asset lives in no world.
		//
		// It had never run. Selecting a track row is what calls this, and until bindings could be
		// created there were no rows to select -- so making the picker work is what woke it up.
		UObject* PlaybackContext = GetPlaybackContext();
		if (AnimationSequence == nullptr || PlaybackContext == nullptr)
		{
			return;
		}
		UDreamWidget* SequencerSelectedWidget = nullptr;
		UDreamUIBehaviour* SequencerSelectedComponent = nullptr;
		for (FGuid Guid : ObjectGuids)
		{
			TArray<UObject*, TInlineAllocator<1>> BoundObjects;
			AnimationSequence->LocateBoundObjects(Guid, PlaybackContext,
				MovieSceneHelpers::CreateTransientSharedPlaybackState(PlaybackContext, AnimationSequence), BoundObjects);
			if (BoundObjects.Num() == 0)
				continue;

			if (auto BoundWidget = Cast<UDreamWidget>(BoundObjects[0]))
			{
				SequencerSelectedWidget = BoundWidget;
			}
			else if (auto BoundComponent = Cast<UDreamUIBehaviour>(BoundObjects[0]))
			{
				SequencerSelectedComponent = BoundComponent;
			}
		}
		if (SequencerSelectedComponent && !SequencerSelectedWidget)
		{
			SequencerSelectedWidget = SequencerSelectedComponent->GetWidget();
		}

		// The live copy again: the selection subsystem is reached through a world, and the sequence's
		// outer is the authored widget, which has none. Even with the resolution above fixed, this
		// half would have gone on selecting nothing.
		if (UDreamWidget* Widget = Cast<UDreamWidget>(PlaybackContext))
		{
			// Sync Selection
			if (auto Selection = UDreamUISelection::GetInstance(Widget->GetWorld()))
			{
				Selection->SelectNone();
				if (SequencerSelectedWidget)
				{
					Selection->SelectWidget(SequencerSelectedWidget);
				}
				if (SequencerSelectedComponent)
				{
					Selection->SelectComponent(SequencerSelectedComponent);
				}
			}
		}
	}

	// ------------------------------------------------------------------ widget selection -> sequencer

	void ObserveWidgetSelection()
	{
		StopObservingWidgetSelection();
		// And the other direction, which never subscribed at all for the same reason -- selecting a
		// widget in the designer has never highlighted its track.
		UDreamWidget* ContextWidget = Cast<UDreamWidget>(GetPlaybackContext());
		UDreamUISelection* Selection = ContextWidget ? UDreamUISelection::GetInstance(ContextWidget->GetWorld()) : nullptr;
		if (Selection)
		{
			ObservedSelection = Selection;
			WidgetSelectionChangedHandle = Selection->OnSelectionChanged.AddSP(this, &SDreamWidgetAnimationEditorWidgetImpl::SyncSequencerSelectionWithSelectedWidgets);
		}
	}

	void StopObservingWidgetSelection()
	{
		if (UDreamUISelection* Selection = ObservedSelection.Get())
		{
			Selection->OnSelectionChanged.Remove(WidgetSelectionChangedHandle);
		}
		ObservedSelection.Reset();
		WidgetSelectionChangedHandle.Reset();
	}

	// ------------------------------------------------------------------ the preview being replaced

	void ObservePreviewRebuild()
	{
		StopObservingPreviewRebuild();
		UDreamWidget* AuthoredWidget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<UDreamWidget>() : nullptr;
		ObservedPreviewHost = FDreamWidgetBlueprintEditor::FindPreviewHostForAnimationContext(AuthoredWidget);
		if (TSharedPtr<FDreamWidgetPreviewHost> Host = ObservedPreviewHost.Pin())
		{
			PreviewAboutToRebuildHandle = Host->OnPreviewAboutToRebuild.AddSP(
				this, &SDreamWidgetAnimationEditorWidgetImpl::EvacuateSequencerEntities);
			PreviewRebuiltHandle = Host->OnPreviewRebuilt.AddSP(
				this, &SDreamWidgetAnimationEditorWidgetImpl::ResumeSequencerEvaluation);
		}
	}

	void StopObservingPreviewRebuild()
	{
		if (TSharedPtr<FDreamWidgetPreviewHost> Host = ObservedPreviewHost.Pin())
		{
			Host->OnPreviewAboutToRebuild.Remove(PreviewAboutToRebuildHandle);
			Host->OnPreviewRebuilt.Remove(PreviewRebuiltHandle);
		}
		ObservedPreviewHost.Reset();
		PreviewAboutToRebuildHandle.Reset();
		PreviewRebuiltHandle.Reset();
	}

	/**
	 * Step the sequencer's entity runtime out of every window in which its bound objects die.
	 *
	 * Sequencer groups animation entities under raw bound-object pointers and keeps two maps of
	 * those keys in lockstep. Nothing repairs them when a preview widget goes away: a preview
	 * rebuild broadcasts no replacement at all, and the map a recompile broadcasts can be worse
	 * than nothing -- re-instancing maps every instance to null when it cannot make a replacement
	 * (a failed compile leaves the class abstract), distinct widgets' keys collapse into one, and
	 * the remap's Add silently overwrites. From then on the maps disagree, and the next
	 * empty-group free fails an ensure deep in engine code with nothing of ours on the stack.
	 *
	 * The maps only desync when keys are rewritten under LIVE entities, so the cure is to not
	 * have entities across either window. Suppressing the playback context and forcing one
	 * evaluation makes the engine itself unlink every entity and free every group through the
	 * same context-switch path it uses every day, while everything is still coherent; resuming
	 * re-resolves against whatever tree exists by then. Two windows, one pair each:
	 *
	 *   recompile -- HandleBlueprintPreCompile / UEditorEngine::OnBlueprintCompiled
	 *   rebuild   -- OnPreviewAboutToRebuild   / OnPreviewRebuilt
	 *
	 * While suppressed, bindings resolve through their authored-widget fallback, so the interim
	 * instance briefly holds the authoring tree instead. That is harmless: those widgets are
	 * native-class instances no recompile replaces, and what evaluation writes onto them is
	 * written back by the same pre-animated restore the context switch performs on resume.
	 *
	 * RestorePreAnimatedState runs before each evacuation because writing saved values back
	 * needs the objects they were saved onto; a tick later is an object too late.
	 */
	void EvacuateSequencerEntities()
	{
		if (!Sequencer.IsValid() || bPlaybackContextSuppressed)
		{
			return;
		}
		Sequencer->RestorePreAnimatedState();
		bPlaybackContextSuppressed = true;
		Sequencer->ForceEvaluate();
	}

	void ResumeSequencerEvaluation()
	{
		if (!bPlaybackContextSuppressed)
		{
			return;
		}
		bPlaybackContextSuppressed = false;
		if (Sequencer.IsValid())
		{
			// Resolved bindings still name the dead tree; UMG drops them at the same moment, at
			// the end of FWidgetBlueprintEditor::UpdatePreview.
			Sequencer->GetEvaluationState()->ClearObjectCaches(*Sequencer);
			Sequencer->ForceEvaluate();
		}
	}

	void HandleBlueprintPreCompile(UBlueprint* InBlueprint)
	{
		// Only this asset's recompile replaces objects this sequencer binds: bound objects are
		// preview widgets, and the preview's only reinstanced parts are instances of this class.
		if (InBlueprint == nullptr || !WeakSequence.IsValid()
			|| InBlueprint != WeakSequence->GetTypedOuter<UDreamWidgetBlueprint>())
		{
			return;
		}
		EvacuateSequencerEntities();
	}

	void HandleBlueprintCompiled()
	{
		ResumeSequencerEvaluation();
	}

	/** The other half of SyncSelectedWidgetsWithSequencerSelection: picking a widget highlights its tracks. */
	void SyncSequencerSelectionWithSelectedWidgets()
	{
		if (!Sequencer.IsValid() || bUpdatingSequencerSelection)
		{
			return;
		}
		UDreamUISelection* Selection = ObservedSelection.Get();
		if (Selection == nullptr)
		{
			return;
		}
		TGuardValue<bool> Guard(bUpdatingSequencerSelection, true);
		Sequencer->EmptySelection();
		for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : Selection->GetSelectedWidgets())
		{
			if (UDreamWidget* Widget = WeakWidget.Get())
			{
				const FGuid Existing = Sequencer->GetHandleToObject(Widget, /*bCreateHandleIfMissing*/false);
				if (Existing.IsValid())
				{
					Sequencer->SelectObject(Existing);
				}
			}
		}
	}

	// ------------------------------------------------------------------ binding right-click menu

	/** The designer-selected widget, if it may be bound into this sequence at all. */
	UDreamWidget* GetBindableSelectedWidget() const
	{
		// The PLAYBACK context, not the sequence's outer. The selection subsystem is found through a
		// world, and the sequence's outer is the authored widget, which has none -- so asking it for
		// the selection always answered null and every entry that reads the selection was dead.
		UDreamWidget* ContextWidget = Cast<UDreamWidget>(GetPlaybackContext());
		if (ContextWidget == nullptr)
		{
			return nullptr;
		}
		UDreamUISelection* Selection = UDreamUISelection::GetInstance(ContextWidget->GetWorld());
		if (Selection == nullptr || Selection->GetSelectedWidgets().Num() != 1)
		{
			return nullptr;
		}
		UDreamWidget* Widget = Selection->GetSelectedWidgets()[0].Get();
		if (Widget == nullptr || (Widget != ContextWidget && !Widget->IsChildOf(ContextWidget)))
		{
			return nullptr;
		}
		return DreamWidgetAnimation_CanBindWidgetToSequencer(Widget) ? Widget : nullptr;
	}

	void BuildBindingContextMenu(FMenuBuilder& MenuBuilder, FGuid ObjectBinding)
	{
		UDreamWidget* SelectedWidget = GetBindableSelectedWidget();
		if (SelectedWidget == nullptr || !Sequencer.IsValid())
		{
			return;
		}
		// Already bound elsewhere in this animation: replacing would silently merge two bindings.
		const FGuid ExistingId = Sequencer->GetHandleToObject(SelectedWidget, /*bCreateHandleIfMissing*/false);
		if (ExistingId.IsValid() && ExistingId != ObjectBinding)
		{
			return;
		}
		MenuBuilder.AddMenuSeparator();
		MenuBuilder.AddMenuEntry(
			FText::Format(LOCTEXT("ReplaceBindingWithWidget", "Replace with {0}"), FText::FromString(SelectedWidget->GetDisplayName())),
			LOCTEXT("ReplaceBindingWithWidgetTooltip", "Rebind this track to the widget selected in the designer, keeping every section and key."),
			FSlateIcon(),
			FExecuteAction::CreateSP(this, &SDreamWidgetAnimationEditorWidgetImpl::ReplaceBindingWithWidget, MakeWeakObjectPtr(SelectedWidget), ObjectBinding));
	}

	void ReplaceBindingWithWidget(TWeakObjectPtr<UDreamWidget> InWidget, FGuid ObjectBinding)
	{
		UDreamWidget* Widget = InWidget.Get();
		UDreamWidgetAnimation* Sequence = WeakSequence.Get();
		if (Widget == nullptr || Sequence == nullptr || !Sequencer.IsValid())
		{
			return;
		}
		UMovieScene* MovieScene = Sequence->GetMovieScene();
		const FScopedTransaction Transaction(LOCTEXT("ReplaceBinding_Transaction", "Replace Animation Binding"));
		Sequence->Modify();
		MovieScene->Modify();
		const FGuid NewGuid = Sequencer->GetHandleToObject(Widget, /*bCreateHandleIfMissing*/true);
		if (!NewGuid.IsValid() || NewGuid == ObjectBinding)
		{
			return;
		}
		MovieScene->MoveBindingContents(ObjectBinding, NewGuid);
		MovieScene->RemovePossessable(ObjectBinding);
		Sequence->UnbindPossessableObjects(ObjectBinding);
		Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemsChanged);
	}



	void OnSequencerReceivedFocus()
	{
		if (Sequencer.IsValid())
		{
			FLevelEditorSequencerIntegration::Get().OnSequencerReceivedFocus(Sequencer.ToSharedRef());
		}
	}

	void AddPossessMenuExtensions(FMenuBuilder& MenuBuilder)
	{
		if (!WeakSequence.IsValid())return;

		Sequencer->GetEvaluationState()->ClearObjectCaches(*Sequencer);
		TSet<UObject*> AllBoundObjects;
		UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			FMovieScenePossessable& Possessable = MovieScene->GetPossessable(Index);
			for (TWeakObjectPtr<> WeakObject : Sequencer->FindBoundObjects(Possessable.GetGuid(), Sequencer->GetFocusedTemplateID()))
			{
				if (UObject* Object = WeakObject.Get())
				{
					AllBoundObjects.Add(Object);
				}
			}
		}

		// The selection is where the author already is. Walking a tree picker to find the widget they
		// just clicked on is the kind of step that gets done a hundred times an afternoon, and the
		// binding context menu already reads the selection for "Replace with" -- this is the same
		// question asked before there is a binding to replace.
		if (UDreamWidget* SelectedWidget = GetBindableSelectedWidget())
		{
			const bool bAlreadyBound = AllBoundObjects.Contains(SelectedWidget);
			MenuBuilder.AddMenuEntry(
				FText::Format(LOCTEXT("AddSelectedWidget_Label", "Add Track for {0}"), FText::FromString(SelectedWidget->GetDisplayName())),
				bAlreadyBound
					? LOCTEXT("AddSelectedWidgetBound_Tooltip", "The widget selected in the designer already has a binding in this animation.")
					: LOCTEXT("AddSelectedWidget_Tooltip", "Bind the widget selected in the designer, without hunting for it in the tree below."),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateSPLambda(this, [this, WeakSelected = MakeWeakObjectPtr(SelectedWidget)]()
					{
						UDreamWidget* Widget = WeakSelected.Get();
						if (Widget == nullptr || !Sequencer.IsValid())
						{
							return;
						}
						UObject* Bindable = ResolveBindableObject(Widget);
						if (Bindable == nullptr)
						{
							return;
						}
						const FScopedTransaction Transaction(LOCTEXT("AddSelectedWidgetToSequencer", "Add Selected Widget to Sequencer"));
						Sequencer->GetHandleToObject(Bindable, true);
					}),
					FCanExecuteAction::CreateLambda([bAlreadyBound]() { return !bAlreadyBound; })));
		}

		//widget menu
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("AddWidget_Label", "Add Widget Track"),
				LOCTEXT("AddWidget_Tooltip", "Add a binding to one of widget and allow it to be animated by Sequencer"),
				FNewMenuDelegate::CreateSPLambda(this, [this](FMenuBuilder& SubMenuBuilder)
				{
					SubMenuBuilder.BeginSection("ChooseWidgetSection", LOCTEXT("ChooseWidget", "Choose Widget:"));
					auto Widget = WeakSequence->GetTypedOuter<UDreamWidget>();
					SubMenuBuilder.AddWidget(
						SNew(SBox)
						.Padding(4, 0)
						[
							SNew(SDreamWidgetHierarchyPickerView, Widget->GetWorld(), UDreamWidget::StaticClass(), Widget)
							.OnSelectItem_Lambda([=, this](UObject* InItem)
							{
								UDreamWidget* TargetWidget = Cast<UDreamWidget>(InItem);
								if (TargetWidget == nullptr && IsValid(InItem))
								{
									TargetWidget = InItem->GetTypedOuter<UDreamWidget>();
								}
								if (!DreamWidgetAnimation_CanBindWidgetToSequencer(TargetWidget))
								{
									FDreamUIUtils::EditorNotification(LOCTEXT("NestedWidgetNotBindable"
										, "This widget belongs to a nested widget blueprint, so a track bound to it would follow a name path through another asset and break the moment that asset renames it. Animate it inside its own blueprint instead."), false, 8);
									return;
								}
								UObject* Bindable = ResolveBindableObject(InItem);
								if (Bindable == nullptr)
								{
									FDreamUIUtils::EditorNotification(LOCTEXT("NoPreviewForWidget"
										, "This widget has no preview to animate against yet. Compile the Blueprint, or reopen the designer, and try again."), false, 8);
									return;
								}
								const FScopedTransaction Transaction(LOCTEXT("AddWidgetToSequencer", "Add Widget to Sequencer"));
								Sequencer->GetHandleToObject(Bindable, true);
							})
						]
						, FText::GetEmpty()
						, true
					);
					SubMenuBuilder.EndSection();
				}),
				false,
				FSlateIcon()
			);
		}
	}

	void OnSequenceChanged()
	{
		auto Widget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<UDreamWidget>() : nullptr;
		// The prefab helper was what a sequence change had to mark dirty. A Widget Blueprint's own
		// dirty state is the Blueprint's, and the sequence lives on it.
	}
private:
	TSharedRef<FExtender> GetAddTrackSequencerExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
	{
		TSharedRef<FExtender> AddTrackMenuExtender(new FExtender());
		AddTrackMenuExtender->AddMenuExtension(
			SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
			EExtensionHook::Before,
			CommandList,
			FMenuExtensionDelegate::CreateRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::ExtendSequencerAddTrackMenu, ContextSensitiveObjects));
		return AddTrackMenuExtender;
	}

	void ExtendSequencerAddTrackMenu(FMenuBuilder& AddTrackMenuBuilder, const TArray<UObject*> ContextObjects)
	{
		if (ContextObjects.Num() != 1)return;
		// This extender is registered with the process-wide manager, so it fires for every sequencer
		// in the editor -- another designer's, the level editor's, anyone's. The entries below
		// write into *this* editor's sequencer, so only the widgets of this editor's own prefab may
		// see them; anything else got duplicate menu items whose click landed in the wrong sequence
		// (or dereferenced a null sequencer).
		{
			// The PLAYBACK context, for the same reason the selection helper uses it: a binding's
			// object is the live PREVIEW widget, and asking whether that is a child of the ASSET's
			// authored root is asking about two different trees -- the answer is always no, so this
			// returned before adding anything and every binding's track menu came up with the
			// Components and SubObjects sections missing. They were not empty; they were never built.
			UDreamWidget* ContextWidget = Cast<UDreamWidget>(GetPlaybackContext());
			UDreamWidget* TargetWidget = Cast<UDreamWidget>(ContextObjects[0]);
			if (TargetWidget == nullptr && ContextObjects[0] != nullptr)
			{
				TargetWidget = ContextObjects[0]->GetTypedOuter<UDreamWidget>();
			}
			if (!Sequencer.IsValid() || ContextWidget == nullptr || TargetWidget == nullptr
				|| !(TargetWidget == ContextWidget || TargetWidget->IsChildOf(ContextWidget)))
			{
				return;
			}
		}

		if (auto Widget = Cast<UDreamWidget>(ContextObjects[0]))
		{
			//component
			AddTrackMenuBuilder.BeginSection("Components", LOCTEXT("ComponentsSection", "Components"));
			for (auto Component : Widget->GetAllComponents())
			{
				if (Component)
				{
					AddTrackMenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("ComponentLabelFormat", "{0} ({1})"), FText::FromString(Component->GetName()), FText::FromString(Component->GetClass()->GetName())),
						FText::FromString(Component->GetPathDisplayName()), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							const FScopedTransaction Transaction(LOCTEXT("AddComponentToSequencer", "Add component to Sequencer"));
							Sequencer->GetHandleToObject(Component, true);
							}))
					);
				}
			}
			AddTrackMenuBuilder.EndSection();

			//sub objects
			AddTrackMenuBuilder.BeginSection("SubObjects", LOCTEXT("SubObjectsSection", "SubObjects"));
			{
				if (auto Visual = Widget->GetVisual())
				{
					AddTrackMenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("WidgetVisualLabelFormat", "{0} ({1})"), FText::FromString(Visual->GetName()), FText::FromString(Visual->GetClass()->GetName())),
						FText::FromString(Visual->GetPathDisplayName()), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							const FScopedTransaction Transaction(LOCTEXT("AddVisualToSequencer", "Add visual to Sequencer"));
							Sequencer->GetHandleToObject(Visual, true);
							}))
					);
				}
				if (auto LayoutContainer = Widget->GetLayoutContainer())
				{
					AddTrackMenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("WidgetLayoutContainerLabelFormat", "{0} ({1})"), FText::FromString(LayoutContainer->GetName()), LayoutContainer->GetClass()->GetDisplayNameText()),
						FText::FromString(LayoutContainer->GetPathDisplayName()), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							const FScopedTransaction Transaction(LOCTEXT("AddLayoutContainerToSequencer", "Add LayoutContainer to Sequencer"));
							Sequencer->GetHandleToObject(LayoutContainer, true);
							}))
					);
				}
				if (auto LayoutSelf = Widget->GetLayoutSelf())
				{
					AddTrackMenuBuilder.AddMenuEntry(
						FText::Format(LOCTEXT("WidgetLayoutSelfLabelFormat", "{0} ({1})"), FText::FromString(LayoutSelf->GetName()), FText::FromString(LayoutSelf->GetClass()->GetName())),
						FText::FromString(LayoutSelf->GetPathDisplayName()), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([=, this]() {
							const FScopedTransaction Transaction(LOCTEXT("AddLayoutSelfToSequencer", "Add LayoutSelf to Sequencer"));
							Sequencer->GetHandleToObject(LayoutSelf, true);
							}))
					);
				}
			}
			AddTrackMenuBuilder.EndSection();

			// A control's own root is bare by construction -- UDreamButton IS its face, and the rect
			// block, the size box and the UUIButton all sit on the child named "Face" -- so the two
			// sections above come out EMPTY for one, and Sequencer has no track editor for the Style
			// struct that is nearly all a control declares. Binding a control therefore produced a
			// row with nothing that could be animated on it, which reads as "controls cannot be
			// animated" rather than as "you are one level too high".
			//
			// Offering the parts is safe where offering the whole subtree is not: a part is named by
			// CollectParts in C++, so a binding path through one is a compile-time contract, while
			// the rest of a nested instance's tree is another asset's names. The path resolves at
			// play time through the real Children -- ResolveWidgetPath never consulted the editor's
			// nested-instance boundary -- so a part binding works exactly like any other.
			if (UDreamUIControl* Control = Cast<UDreamUIControl>(Widget))
			{
				TArray<TPair<FName, UDreamWidget*>> Parts;
				Control->GetBoundParts(Parts);
				if (Parts.Num() > 0)
				{
					AddTrackMenuBuilder.BeginSection("ControlParts", LOCTEXT("ControlPartsSection", "Control Parts"));
					for (const TPair<FName, UDreamWidget*>& Part : Parts)
					{
						UDreamWidget* PartWidget = Part.Value;
						AddTrackMenuBuilder.AddMenuEntry(
							FText::Format(LOCTEXT("ControlPartLabelFormat", "{0} ({1})"),
								FText::FromName(Part.Key), FText::FromString(PartWidget->GetClass()->GetName())),
							FText::Format(LOCTEXT("ControlPartTooltip", "Bind {0}, the part this control builds under that name. Its own visual and components can then be tracked from its row."),
								FText::FromName(Part.Key)),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([=, this]()
							{
								const FScopedTransaction Transaction(LOCTEXT("AddControlPartToSequencer", "Add Control Part to Sequencer"));
								Sequencer->GetHandleToObject(PartWidget, true);
							})));
					}
					AddTrackMenuBuilder.EndSection();
				}
			}
		}
		
		if (auto Image = Cast<UDreamImage>(ContextObjects[0]))
		{
			if (Image != nullptr && Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()) != nullptr)
			{
				if (auto BrushStructProperty = FindFProperty<FStructProperty>(Image->GetClass(), UDreamImage::GetPropertyName_Brush()))
				{
					if (auto ResourceObjectProperty = FindFProperty<FProperty>(Image->GetClass(), FDreamUIImageBrush::GetPropertyName_ResourceObject()))
					{
						AddTrackMenuBuilder.BeginSection("Materials", LOCTEXT("MaterialsSection", "Materials"));
						{
							FText DisplayNameText = ResourceObjectProperty->GetDisplayNameText();
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::AddMaterialTrack, (UObject*)Image, ResourceObjectProperty, DisplayNameText));
							FText AddMaterialLabel = DisplayNameText;
							FText AddMaterialToolTip = FText::Format(LOCTEXT("ImageBrushMaterialToolTipFormat", "Add a material track for the {0} property."), DisplayNameText);
							AddTrackMenuBuilder.AddMenuEntry(AddMaterialLabel, AddMaterialToolTip, FSlateIcon(), AddMaterialAction);
						}
						AddTrackMenuBuilder.EndSection();
					}
				}
			}
		}
		else if (auto Text = Cast<UDreamText>(ContextObjects[0]))
		{
			if (Text != nullptr && Text->GetOverrideMaterial() != nullptr)
			{
				{
					if (auto MaterialProperty = FindFProperty<FProperty>(Text->GetClass(), UDreamText::GetPropertyName_OverrideMaterial()))
					{
						AddTrackMenuBuilder.BeginSection("Materials", LOCTEXT("MaterialsSection", "Materials"));
						{
							FText DisplayNameText = MaterialProperty->GetDisplayNameText();
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SDreamWidgetAnimationEditorWidgetImpl::AddMaterialTrack, (UObject*)Text, MaterialProperty, DisplayNameText));
							FText AddMaterialLabel = DisplayNameText;
							FText AddMaterialToolTip = FText::Format(LOCTEXT("TextMaterialToolTipFormat", "Add a material track for the {0} property."), DisplayNameText);
							AddTrackMenuBuilder.AddMenuEntry(AddMaterialLabel, AddMaterialToolTip, FSlateIcon(), AddMaterialAction);
						}
						AddTrackMenuBuilder.EndSection();
					}
				}
			}
		}
	}

	void AddMaterialTrack(UObject* Object, FProperty* MaterialProperty, FText MaterialPropertyDisplayName)
	{
		FGuid WidgetHandle = Sequencer->GetHandleToObject(Object);
		if (WidgetHandle.IsValid())
		{
			UMovieScene* MovieScene = Sequencer->GetFocusedMovieSceneSequence()->GetMovieScene();

			if (MovieScene->IsReadOnly())
			{
				return;
			}

			FName MaterialPropertyNamePath = MaterialProperty->GetFName();
			if (MovieScene->FindTrack(UMovieSceneDreamUIMaterialTrack::StaticClass(), WidgetHandle, MaterialPropertyNamePath) == nullptr)
			{
				const FScopedTransaction Transaction(LOCTEXT("AddWidgetMaterialTrack", "Add widget material track"));

				MovieScene->Modify();

				auto NewTrack = Cast<UMovieSceneDreamUIMaterialTrack>(MovieScene->AddTrack(UMovieSceneDreamUIMaterialTrack::StaticClass(), WidgetHandle));
				NewTrack->Modify();
				NewTrack->SetPropertyName(MaterialPropertyNamePath);
				NewTrack->SetDisplayName(FText::Format(LOCTEXT("TrackDisplayNameFormat", "{0}"), MaterialPropertyDisplayName));

				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
			}
		}
	}
private:
	TWeakObjectPtr<UDreamWidgetAnimation> WeakSequence;
	TWeakObjectPtr<UDreamUISelection> ObservedSelection;
	FDelegateHandle WidgetSelectionChangedHandle;
	/** Weak, because the host outlives nothing here and this panel must not keep a designer alive. */
	TWeakPtr<FDreamWidgetPreviewHost> ObservedPreviewHost;
	FDelegateHandle PreviewAboutToRebuildHandle;
	FDelegateHandle PreviewRebuiltHandle;
	FDelegateHandle BlueprintPreCompileHandle;
	FDelegateHandle BlueprintCompiledHandle;
	/** True from an evacuation to its resume; GetPlaybackContext answers null throughout. */
	bool bPlaybackContextSuppressed = false;

	TSharedPtr<SBox> Content;
	TSharedPtr<ISequencer> Sequencer;

	FDelegateHandle OnSequenceChangedHandle;

	TSharedPtr<STextBlock> NoAnimationTextBlock;

	/** The asset editor that created this Sequencer if any. Weak on purpose: the designer's host
	 *  strong-owns its toolkit, which strong-owns this widget -- a strong pointer here closes that loop
	 *  and the toolkit never destructs, leaving the asset registered as open after its window is gone. */
	TWeakPtr<IToolkitHost> ToolkitHost;

	FDelegateHandle SequencerAddTrackExtenderHandle;
};

void SDreamWidgetAnimationEditorWidget::Construct(const FArguments&)
{
	ChildSlot
	[
		SAssignNew(Impl, SDreamWidgetAnimationEditorWidgetImpl)
	];
}

FText SDreamWidgetAnimationEditorWidget::GetDisplayLabel() const
{
	return Impl.Pin()->GetDisplayLabel();
}

TSharedPtr<ISequencer> SDreamWidgetAnimationEditorWidget::GetSequencer() const
{
	return Impl.Pin()->GetSequencer();
}

void SDreamWidgetAnimationEditorWidget::SetToolkitHost(TSharedPtr<IToolkitHost> InToolkitHost)
{
	if (TSharedPtr<SDreamWidgetAnimationEditorWidgetImpl> PinnedImpl = Impl.Pin())
	{
		PinnedImpl->SetToolkitHost(InToolkitHost);
	}
}

void SDreamWidgetAnimationEditorWidget::AssignSequence(UDreamWidgetAnimation* NewDreamWidgetAnimation)
{
	Impl.Pin()->SetDreamWidgetAnimation(NewDreamWidgetAnimation);
}

UDreamWidgetAnimation* SDreamWidgetAnimationEditorWidget::GetSequence() const
{
	return Impl.Pin()->GetAnimation();
}

#undef LOCTEXT_NAMESPACE
