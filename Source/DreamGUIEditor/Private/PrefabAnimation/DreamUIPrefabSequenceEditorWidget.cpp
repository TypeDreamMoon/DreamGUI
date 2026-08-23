// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabSequenceEditorWidget.h"

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
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
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "LevelEditor.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamText.h"
#include "PrefabEditor/DreamWidgetHierarchyPickerView.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "PrefabSystem/PrefabAnimation/MovieSceneDreamUIMaterialTrack.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabSequenceEditorWidget"

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
 * which no headless test can construct; DreamPrefabPanelsAutomationTests declares this prototype.
 */
bool DreamUIPrefabSequence_CanBindWidgetToSequencer(UDreamUIPrefabHelperObject* InPrefabHelper, const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))return false;
	if (!IsValid(InPrefabHelper))return true;//outside a prefab editor there is no sub-prefab to be part of
	return !InPrefabHelper->IsWidgetBelongsToSubPrefab(InWidget);
}

class SDreamUIPrefabSequenceEditorWidgetImpl : public SCompoundWidget, public FEditorUndoClient
{
public:

	bool bUpdatingSequencerSelection = false;

	SLATE_BEGIN_ARGS(SDreamUIPrefabSequenceEditorWidgetImpl){}
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

	~SDreamUIPrefabSequenceEditorWidgetImpl()
	{
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
		// A fallback only: the prefab editor injects its own host via SetToolkitHost so side panels
		// (the curve editor) open in its window rather than the level editor's.
		ToolkitHost = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();

		// Register sequencer menu extenders.
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		{
			int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
				FAssetEditorExtender::CreateRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::GetAddTrackSequencerExtender));
			SequencerAddTrackExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();
		}
	}


	virtual void PostUndo(bool bSuccess) override
	{
		if (!GetPrefabSequence())
		{
			Close();
		}
	}

	FText GetDisplayLabel() const
	{
		UDreamUIPrefabSequence* Sequence = WeakSequence.Get();
		return Sequence ? Sequence->GetDisplayName() : LOCTEXT("DefaultSequencerLabel", "Sequencer");
	}

	TSharedPtr<ISequencer> GetSequencer() const
	{
		return Sequencer;
	}

	UDreamUIPrefabSequence* GetPrefabSequence() const
	{
		return WeakSequence.Get();
	}

	UObject* GetPlaybackContext() const
	{
		if (auto LocalPrefabSequence = GetPrefabSequence())
		{
			auto Component = LocalPrefabSequence->GetTypedOuter<UDreamUIPrefabSequenceComponent>();
			return Component->GetWidget();
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
		static UDreamUIPrefabSequence* NullSequence = nullptr;
		if (!NullSequence)
		{
			NullSequence = NewObject<UDreamUIPrefabSequence>(GetTransientPackage(), NAME_None);
			NullSequence->AddToRoot();
			NullSequence->GetMovieScene()->SetDisplayRate(FFrameRate(30, 1));
		}
		return NullSequence;
	}

	void SetDreamUIPrefabSequence(UDreamUIPrefabSequence* NewSequence)
	{
		if (UDreamUIPrefabSequence* OldSequence = WeakSequence.Get())
		{
			if (OnSequenceChangedHandle.IsValid())
			{
				OldSequence->OnSignatureChanged().Remove(OnSequenceChangedHandle);
			}
		}

		WeakSequence = NewSequence;

		if (NewSequence)
		{
			OnSequenceChangedHandle = NewSequence->OnSignatureChanged().AddSP(this, &SDreamUIPrefabSequenceEditorWidgetImpl::OnSequenceChanged);
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
			TWeakObjectPtr<UDreamUIPrefabSequence> LocalWeakSequence = NewSequence;

			SequencerInitParams.RootSequence = NewSequence ? NewSequence : GetNullSequence();
			SequencerInitParams.EventContexts = TAttribute<TArray<UObject*>>(this, &SDreamUIPrefabSequenceEditorWidgetImpl::GetEventContexts);
			SequencerInitParams.PlaybackContext = TAttribute<UObject*>(this, &SDreamUIPrefabSequenceEditorWidgetImpl::GetPlaybackContext);

			TSharedRef<FExtender> AddMenuExtender = MakeShareable(new FExtender);

			AddMenuExtender->AddMenuExtension("AddTracks", EExtensionHook::Before, nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::AddPossessMenuExtensions)
			);

			SequencerInitParams.ViewParams.bReadOnly = !NewSequence->IsEditable();
			SequencerInitParams.ViewParams.AddMenuExtender = AddMenuExtender;
			SequencerInitParams.ViewParams.UniqueName = "EmbeddedDreamUIPrefabSequenceEditor";
			SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
			SequencerInitParams.ViewParams.OnReceivedFocus.BindRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::OnSequencerReceivedFocus);
			SequencerInitParams.ViewParams.OnBuildCustomContextMenuForGuid = FOnBuildCustomContextMenuForGuid::CreateSP(this, &SDreamUIPrefabSequenceEditorWidgetImpl::BuildBindingContextMenu);
			SequencerInitParams.bEditWithinLevelEditor = false;
			SequencerInitParams.ToolkitHost = ToolkitHost;
			SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		}

		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
		Content->SetContent(Sequencer->GetSequencerWidget());
		Sequencer->GetSelectionChangedObjectGuids().AddSP(this, &SDreamUIPrefabSequenceEditorWidgetImpl::SyncSelectedWidgetsWithSequencerSelection);
		ObserveWidgetSelection();
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
		UObject* BindingContext = WeakSequence.Get();
		UDreamWidget* SequencerSelectedWidget = nullptr;
		UDreamUIBehaviour* SequencerSelectedComponent = nullptr;
		for (FGuid Guid : ObjectGuids)
		{
			TArray<UObject*, TInlineAllocator<1>> BoundObjects;
			AnimationSequence->LocateBoundObjects(Guid, BindingContext, MovieSceneHelpers::CreateTransientSharedPlaybackState(BindingContext->GetWorld(), Cast<UMovieSceneSequence>(BindingContext)), BoundObjects);
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

		if (auto Widget = WeakSequence->GetTypedOuter<UDreamWidget>())
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
		UDreamWidget* ContextWidget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<UDreamWidget>() : nullptr;
		UDreamUISelection* Selection = ContextWidget ? UDreamUISelection::GetInstance(ContextWidget->GetWorld()) : nullptr;
		if (Selection)
		{
			ObservedSelection = Selection;
			WidgetSelectionChangedHandle = Selection->OnSelectionChanged.AddSP(this, &SDreamUIPrefabSequenceEditorWidgetImpl::SyncSequencerSelectionWithSelectedWidgets);
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
		UDreamWidget* ContextWidget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<UDreamWidget>() : nullptr;
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
		UDreamUIPrefabHelperObject* Helper = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(ContextWidget);
		return DreamUIPrefabSequence_CanBindWidgetToSequencer(Helper, Widget) ? Widget : nullptr;
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
			FExecuteAction::CreateSP(this, &SDreamUIPrefabSequenceEditorWidgetImpl::ReplaceBindingWithWidget, MakeWeakObjectPtr(SelectedWidget), ObjectBinding));
	}

	void ReplaceBindingWithWidget(TWeakObjectPtr<UDreamWidget> InWidget, FGuid ObjectBinding)
	{
		UDreamWidget* Widget = InWidget.Get();
		UDreamUIPrefabSequence* Sequence = WeakSequence.Get();
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
								if (!DreamUIPrefabSequence_CanBindWidgetToSequencer(
									UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget), TargetWidget))
								{
									FDreamUIUtils::EditorNotification(LOCTEXT("SubPrefabWidgetNotBindable"
										, "This widget belongs to a sub-prefab, so a track bound to it would be lost the next time the prefab loads. Animate it inside its own prefab instead."), false, 8);
									return;
								}
								const FScopedTransaction Transaction(LOCTEXT("AddWidgetToSequencer", "Add Widget to Sequencer"));
								Sequencer->GetHandleToObject(InItem, true);
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
		if (Widget)
		{
			if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
			{
				PrefabHelperObject->SetAnythingDirty();
			}
		}
	}
private:
	TSharedRef<FExtender> GetAddTrackSequencerExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
	{
		TSharedRef<FExtender> AddTrackMenuExtender(new FExtender());
		AddTrackMenuExtender->AddMenuExtension(
			SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
			EExtensionHook::Before,
			CommandList,
			FMenuExtensionDelegate::CreateRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::ExtendSequencerAddTrackMenu, ContextSensitiveObjects));
		return AddTrackMenuExtender;
	}

	void ExtendSequencerAddTrackMenu(FMenuBuilder& AddTrackMenuBuilder, const TArray<UObject*> ContextObjects)
	{
		if (ContextObjects.Num() != 1)return;
		// This extender is registered with the process-wide manager, so it fires for every sequencer
		// in the editor -- another prefab editor's, the level editor's, anyone's. The entries below
		// write into *this* editor's sequencer, so only the widgets of this editor's own prefab may
		// see them; anything else got duplicate menu items whose click landed in the wrong sequence
		// (or dereferenced a null sequencer).
		{
			UDreamWidget* ContextWidget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<UDreamWidget>() : nullptr;
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
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::AddMaterialTrack, (UObject*)Image, ResourceObjectProperty, DisplayNameText));
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
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SDreamUIPrefabSequenceEditorWidgetImpl::AddMaterialTrack, (UObject*)Text, MaterialProperty, DisplayNameText));
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
	TWeakObjectPtr<UDreamUIPrefabSequence> WeakSequence;
	TWeakObjectPtr<UDreamUISelection> ObservedSelection;
	FDelegateHandle WidgetSelectionChangedHandle;

	TSharedPtr<SBox> Content;
	TSharedPtr<ISequencer> Sequencer;

	FDelegateHandle OnSequenceChangedHandle;

	TSharedPtr<STextBlock> NoAnimationTextBlock;

	/** The asset editor that created this Sequencer if any */
	TSharedPtr<IToolkitHost> ToolkitHost;

	FDelegateHandle SequencerAddTrackExtenderHandle;
};

void SDreamUIPrefabSequenceEditorWidget::Construct(const FArguments&)
{
	ChildSlot
	[
		SAssignNew(Impl, SDreamUIPrefabSequenceEditorWidgetImpl)
	];
}

FText SDreamUIPrefabSequenceEditorWidget::GetDisplayLabel() const
{
	return Impl.Pin()->GetDisplayLabel();
}

TSharedPtr<ISequencer> SDreamUIPrefabSequenceEditorWidget::GetSequencer() const
{
	return Impl.Pin()->GetSequencer();
}

void SDreamUIPrefabSequenceEditorWidget::SetToolkitHost(TSharedPtr<IToolkitHost> InToolkitHost)
{
	if (TSharedPtr<SDreamUIPrefabSequenceEditorWidgetImpl> PinnedImpl = Impl.Pin())
	{
		PinnedImpl->SetToolkitHost(InToolkitHost);
	}
}

void SDreamUIPrefabSequenceEditorWidget::AssignSequence(UDreamUIPrefabSequence* NewDreamUIPrefabSequence)
{
	Impl.Pin()->SetDreamUIPrefabSequence(NewDreamUIPrefabSequence);
}

UDreamUIPrefabSequence* SDreamUIPrefabSequenceEditorWidget::GetSequence() const
{
	return Impl.Pin()->GetPrefabSequence();
}

#undef LOCTEXT_NAMESPACE
