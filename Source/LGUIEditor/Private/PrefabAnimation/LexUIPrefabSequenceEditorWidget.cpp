// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabSequenceEditorWidget.h"

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelEditorSequencerIntegration.h"
#include "SSCSEditor.h"
#include "EditorUndoClient.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Utils/LexUIUtils.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "LevelEditor.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexText.h"
#include "PrefabEditor/LexWidgetHierarchyPickerView.h"
#include "Core/Components/LexVisualBatchMesh.h"
#include "PrefabSystem/PrefabAnimation/MovieSceneLexUIMaterialTrack.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabSequenceEditorWidget"

class SLexUIPrefabSequenceEditorWidgetImpl : public SCompoundWidget, public FEditorUndoClient
{
public:

	bool bUpdatingSequencerSelection = false;

	SLATE_BEGIN_ARGS(SLexUIPrefabSequenceEditorWidgetImpl){}
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

	~SLexUIPrefabSequenceEditorWidgetImpl()
	{
		Close();

		// Un-Register sequencer menu extenders.
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
			{
				return SequencerAddTrackExtenderHandle == Extender.GetHandle();
			});
	}
	
	TSharedRef<SDockTab> SpawnCurveEditorTab(const FSpawnTabArgs&)
	{
		const FSlateIcon SequencerGraphIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCurveEditor.TabIcon");
		auto Tab = SNew(SDockTab)
			.Label(NSLOCTEXT("Sequencer", "SequencerMainGraphEditorTitle", "Sequencer Curves"))
			[
				SNullWidget::NullWidget
			];
		Tab->SetTabIcon(SequencerGraphIcon.GetIcon());
		return Tab;
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
		ToolkitHost = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor").GetFirstLevelEditor();

		// Register sequencer menu extenders.
		ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
		{
			int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
				FAssetEditorExtender::CreateRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::GetAddTrackSequencerExtender));
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
		ULexUIPrefabSequence* Sequence = WeakSequence.Get();
		return Sequence ? Sequence->GetDisplayName() : LOCTEXT("DefaultSequencerLabel", "Sequencer");
	}

	TSharedPtr<ISequencer> GetSequencer() const
	{
		return Sequencer;
	}

	ULexUIPrefabSequence* GetPrefabSequence() const
	{
		return WeakSequence.Get();
	}

	UObject* GetPlaybackContext() const
	{
		if (auto LocalPrefabSequence = GetPrefabSequence())
		{
			auto Component = LocalPrefabSequence->GetTypedOuter<ULexUIPrefabSequenceComponent>();
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
		static ULexUIPrefabSequence* NullSequence = nullptr;
		if (!NullSequence)
		{
			NullSequence = NewObject<ULexUIPrefabSequence>(GetTransientPackage(), NAME_None);
			NullSequence->AddToRoot();
			NullSequence->GetMovieScene()->SetDisplayRate(FFrameRate(30, 1));
		}
		return NullSequence;
	}

	void SetLexUIPrefabSequence(ULexUIPrefabSequence* NewSequence)
	{
		if (ULexUIPrefabSequence* OldSequence = WeakSequence.Get())
		{
			if (OnSequenceChangedHandle.IsValid())
			{
				OldSequence->OnSignatureChanged().Remove(OnSequenceChangedHandle);
			}
		}

		WeakSequence = NewSequence;

		if (NewSequence)
		{
			OnSequenceChangedHandle = NewSequence->OnSignatureChanged().AddSP(this, &SLexUIPrefabSequenceEditorWidgetImpl::OnSequenceChanged);
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
			TWeakObjectPtr<ULexUIPrefabSequence> LocalWeakSequence = NewSequence;

			SequencerInitParams.RootSequence = NewSequence ? NewSequence : GetNullSequence();
			SequencerInitParams.EventContexts = TAttribute<TArray<UObject*>>(this, &SLexUIPrefabSequenceEditorWidgetImpl::GetEventContexts);
			SequencerInitParams.PlaybackContext = TAttribute<UObject*>(this, &SLexUIPrefabSequenceEditorWidgetImpl::GetPlaybackContext);

			TSharedRef<FExtender> AddMenuExtender = MakeShareable(new FExtender);

			AddMenuExtender->AddMenuExtension("AddTracks", EExtensionHook::Before, nullptr,
				FMenuExtensionDelegate::CreateRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::AddPossessMenuExtensions)
			);

			SequencerInitParams.ViewParams.bReadOnly = !NewSequence->IsEditable();
			SequencerInitParams.ViewParams.AddMenuExtender = AddMenuExtender;
			SequencerInitParams.ViewParams.UniqueName = "EmbeddedLexUIPrefabSequenceEditor";
			SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
			SequencerInitParams.ViewParams.OnReceivedFocus.BindRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::OnSequencerReceivedFocus);
			SequencerInitParams.bEditWithinLevelEditor = false;
			SequencerInitParams.ToolkitHost = ToolkitHost;
			SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		}

		Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
		Content->SetContent(Sequencer->GetSequencerWidget());
		Sequencer->GetSelectionChangedObjectGuids().AddSP(this, &SLexUIPrefabSequenceEditorWidgetImpl::SyncSelectedWidgetsWithSequencerSelection);
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
					if (auto Widget = Cast<ULexWidget>(ObjectArray[0]))
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
		ULexWidget* SequencerSelectedWidget = nullptr;
		ULexUIBehaviour* SequencerSelectedComponent = nullptr;
		for (FGuid Guid : ObjectGuids)
		{
			TArray<UObject*, TInlineAllocator<1>> BoundObjects;
			AnimationSequence->LocateBoundObjects(Guid, BindingContext, MovieSceneHelpers::CreateTransientSharedPlaybackState(BindingContext->GetWorld(), Cast<UMovieSceneSequence>(BindingContext)), BoundObjects);
			if (BoundObjects.Num() == 0)
				continue;

			if (auto BoundWidget = Cast<ULexWidget>(BoundObjects[0]))
			{
				SequencerSelectedWidget = BoundWidget;
			}
			else if (auto BoundComponent = Cast<ULexUIBehaviour>(BoundObjects[0]))
			{
				SequencerSelectedComponent = BoundComponent;
			}
		}
		if (SequencerSelectedComponent && !SequencerSelectedWidget)
		{
			SequencerSelectedWidget = SequencerSelectedComponent->GetWidget();
		}

		if (auto Widget = WeakSequence->GetTypedOuter<ULexWidget>())
		{
			// Sync Selection
			if (auto Selection = ULexUISelection::GetInstance(Widget->GetWorld()))
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
					auto Widget = WeakSequence->GetTypedOuter<ULexWidget>();
					SubMenuBuilder.AddWidget(
						SNew(SBox)
						.Padding(4, 0)
						[
							SNew(SLexWidgetHierarchyPickerView, Widget->GetWorld(), ULexWidget::StaticClass(), Widget)
							.OnSelectItem_Lambda([=, this](UObject* InItem)
							{
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
		auto Widget = WeakSequence.IsValid() ? WeakSequence->GetTypedOuter<ULexWidget>() : nullptr;
		if (Widget)
		{
			if (auto PrefabHelperObject = ULexUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget))
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
			FMenuExtensionDelegate::CreateRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::ExtendSequencerAddTrackMenu, ContextSensitiveObjects));
		return AddTrackMenuExtender;
	}

	void ExtendSequencerAddTrackMenu(FMenuBuilder& AddTrackMenuBuilder, const TArray<UObject*> ContextObjects)
	{
		if (ContextObjects.Num() != 1)return;

		if (auto Widget = Cast<ULexWidget>(ContextObjects[0]))
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
		
		if (auto Image = Cast<ULexImage>(ContextObjects[0]))
		{
			if (Image != nullptr && Cast<UMaterialInterface>(Image->GetBrush().GetResourceObject()) != nullptr)
			{
				if (auto BrushStructProperty = FindFProperty<FStructProperty>(Image->GetClass(), ULexImage::GetPropertyName_Brush()))
				{
					if (auto ResourceObjectProperty = FindFProperty<FProperty>(Image->GetClass(), FLexUIImageBrush::GetPropertyName_ResourceObject()))
					{
						AddTrackMenuBuilder.BeginSection("Materials", LOCTEXT("MaterialsSection", "Materials"));
						{
							FText DisplayNameText = ResourceObjectProperty->GetDisplayNameText();
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::AddMaterialTrack, (UObject*)Image, ResourceObjectProperty, DisplayNameText));
							FText AddMaterialLabel = DisplayNameText;
							FText AddMaterialToolTip = FText::Format(LOCTEXT("ImageBrushMaterialToolTipFormat", "Add a material track for the {0} property."), DisplayNameText);
							AddTrackMenuBuilder.AddMenuEntry(AddMaterialLabel, AddMaterialToolTip, FSlateIcon(), AddMaterialAction);
						}
						AddTrackMenuBuilder.EndSection();
					}
				}
			}
		}
		else if (auto Text = Cast<ULexText>(ContextObjects[0]))
		{
			if (Text != nullptr && Text->GetOverrideMaterial() != nullptr)
			{
				{
					if (auto MaterialProperty = FindFProperty<FProperty>(Text->GetClass(), ULexText::GetPropertyName_OverrideMaterial()))
					{
						AddTrackMenuBuilder.BeginSection("Materials", LOCTEXT("MaterialsSection", "Materials"));
						{
							FText DisplayNameText = MaterialProperty->GetDisplayNameText();
							FUIAction AddMaterialAction(FExecuteAction::CreateRaw(this, &SLexUIPrefabSequenceEditorWidgetImpl::AddMaterialTrack, (UObject*)Text, MaterialProperty, DisplayNameText));
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
			if (MovieScene->FindTrack(UMovieSceneLexUIMaterialTrack::StaticClass(), WidgetHandle, MaterialPropertyNamePath) == nullptr)
			{
				const FScopedTransaction Transaction(LOCTEXT("AddWidgetMaterialTrack", "Add widget material track"));

				MovieScene->Modify();

				auto NewTrack = Cast<UMovieSceneLexUIMaterialTrack>(MovieScene->AddTrack(UMovieSceneLexUIMaterialTrack::StaticClass(), WidgetHandle));
				NewTrack->Modify();
				NewTrack->SetPropertyName(MaterialPropertyNamePath);
				NewTrack->SetDisplayName(FText::Format(LOCTEXT("TrackDisplayNameFormat", "{0}"), MaterialPropertyDisplayName));

				Sequencer->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
			}
		}
	}
private:
	TWeakObjectPtr<ULexUIPrefabSequence> WeakSequence;

	TSharedPtr<SBox> Content;
	TSharedPtr<ISequencer> Sequencer;

	FDelegateHandle OnSequenceChangedHandle;

	TSharedPtr<STextBlock> NoAnimationTextBlock;

	/** The asset editor that created this Sequencer if any */
	TSharedPtr<IToolkitHost> ToolkitHost;

	FDelegateHandle SequencerAddTrackExtenderHandle;
};

void SLexUIPrefabSequenceEditorWidget::Construct(const FArguments&)
{
	ChildSlot
	[
		SAssignNew(Impl, SLexUIPrefabSequenceEditorWidgetImpl)
	];
}

FText SLexUIPrefabSequenceEditorWidget::GetDisplayLabel() const
{
	return Impl.Pin()->GetDisplayLabel();
}

TSharedPtr<ISequencer> SLexUIPrefabSequenceEditorWidget::GetSequencer() const
{
	return Impl.Pin()->GetSequencer();
}

void SLexUIPrefabSequenceEditorWidget::AssignSequence(ULexUIPrefabSequence* NewLexUIPrefabSequence)
{
	Impl.Pin()->SetLexUIPrefabSequence(NewLexUIPrefabSequence);
}

ULexUIPrefabSequence* SLexUIPrefabSequenceEditorWidget::GetSequence() const
{
	return Impl.Pin()->GetPrefabSequence();
}

#undef LOCTEXT_NAMESPACE
