// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUISequenceTrackEditor.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUISequenceTrack.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "MovieScene.h"
#include "ScopedTransaction.h"
#include "TrackEditors/SubTrackEditorBase.h"
#include "SequencerUtilities.h"
#include "GameFramework/Actor.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Modules/ModuleManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "DreamUISequenceTrackEditor"

TSharedRef<ISequencerTrackEditor> FDreamUISequenceTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FDreamUISequenceTrackEditor>(InSequencer);
}

FDreamUISequenceTrackEditor::FDreamUISequenceTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

bool FDreamUISequenceTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UDreamUISequenceTrack::StaticClass();
}

void FDreamUISequenceTrackEditor::BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass)
{
	if (ObjectClass == nullptr)
	{
		return;
	}
	bool bRelevant = ObjectClass->IsChildOf(UDreamWidget::StaticClass()) || ObjectClass->IsChildOf(UDreamWidgetPresenterComponentBase::StaticClass());
	// An actor binding is relevant only when the actor actually carries a presenter; resolving the
	// instance here keeps the menu off every other actor in the sequence.
	if (!bRelevant && ObjectClass->IsChildOf(AActor::StaticClass()) && !ObjectBindings.IsEmpty())
	{
		if (const TSharedPtr<ISequencer> SequencerPtr = GetSequencer())
		{
			for (TWeakObjectPtr<> WeakObject : SequencerPtr->FindObjectsInCurrentSequence(ObjectBindings[0]))
			{
				const AActor* Actor = Cast<AActor>(WeakObject.Get());
				if (Actor != nullptr && Actor->FindComponentByClass<UDreamWidgetPresenterComponentBase>() != nullptr)
				{
					bRelevant = true;
					break;
				}
			}
		}
	}
	if (!bRelevant)
	{
		return;
	}
	MenuBuilder.AddSubMenu(
		LOCTEXT("BindPrefabWidget", "Bind Prefab Widget"),
		LOCTEXT("BindPrefabWidgetTooltip", "Possess a widget inside the prefab this binding presents; property tracks can then key it directly."),
		FNewMenuDelegate::CreateRaw(this, &FDreamUISequenceTrackEditor::AddWidgetPickerSubMenu, TArray<FGuid>(ObjectBindings)));
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddDreamUISequence", "DreamUI Animation"),
		LOCTEXT("AddDreamUISequenceTooltip", "Play a DreamUI animation asset on this widget: the asset's root binding resolves as this binding's object."),
		FNewMenuDelegate::CreateRaw(this, &FDreamUISequenceTrackEditor::AddAssetSubMenu, TArray<FGuid>(ObjectBindings)));
}

TSharedRef<ISequencerSection> FDreamUISequenceTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShared<TSubSectionMixin<>>(GetSequencer(), *CastChecked<UDreamUISequenceSection>(&SectionObject));
}

void FDreamUISequenceTrackEditor::AddWidgetPickerSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (!SequencerPtr.IsValid() || ObjectBindings.IsEmpty())
	{
		return;
	}
	// The bound object leads to the loaded tree: a presenter directly, an actor through its
	// presenter component, a widget through its root.
	UDreamWidget* Root = nullptr;
	for (TWeakObjectPtr<> WeakObject : SequencerPtr->FindObjectsInCurrentSequence(ObjectBindings[0]))
	{
		UObject* Object = WeakObject.Get();
		if (const UDreamWidgetPresenterComponentBase* Presenter = Cast<UDreamWidgetPresenterComponentBase>(Object))
		{
			Root = Presenter->GetLoadedWidget();
		}
		else if (const AActor* Actor = Cast<AActor>(Object))
		{
			if (const UDreamWidgetPresenterComponentBase* ActorPresenter = Actor->FindComponentByClass<UDreamWidgetPresenterComponentBase>())
			{
				Root = ActorPresenter->GetLoadedWidget();
			}
		}
		else if (UDreamWidget* Widget = Cast<UDreamWidget>(Object))
		{
			Root = Widget;
			while (Root->GetParent() != nullptr)
			{
				Root = Root->GetParent();
			}
		}
		if (Root != nullptr)
		{
			break;
		}
	}
	if (Root == nullptr)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("NoLoadedWidgetTree", "No loaded widget tree"), FText::GetEmpty(), FSlateIcon(), FUIAction());
		return;
	}

	// Flat, indented listing; deep trees read fine and the menu stays one level.
	struct FWalker
	{
		FMenuBuilder& Menu;
		FDreamUISequenceTrackEditor* Self;
		int32 Count = 0;
		void Walk(UDreamWidget* Widget, int32 Depth)
		{
			if (Widget == nullptr || Count >= 64)
			{
				return;
			}
			++Count;
			FString Label;
			for (int32 Index = 0; Index < Depth; ++Index)
			{
				Label += TEXT("    ");
			}
			Label += Widget->GetDisplayName();
			Menu.AddMenuEntry(FText::FromString(Label), FText::GetEmpty(), FSlateIcon(),
				FUIAction(FExecuteAction::CreateRaw(Self, &FDreamUISequenceTrackEditor::HandleWidgetPicked, MakeWeakObjectPtr(Widget))));
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				Walk(Child, Depth + 1);
			}
		}
	};
	FWalker Walker{ MenuBuilder, this };
	Walker.Walk(Root, 0);
	if (Walker.Count >= 64)
	{
		MenuBuilder.AddMenuEntry(LOCTEXT("WidgetListTruncated", "... (truncated at 64)"), FText::GetEmpty(), FSlateIcon(), FUIAction());
	}
}

void FDreamUISequenceTrackEditor::HandleWidgetPicked(TWeakObjectPtr<UDreamWidget> InWidget)
{
	UDreamWidget* Widget = InWidget.Get();
	const TSharedPtr<ISequencer> SequencerPtr = GetSequencer();
	if (Widget == nullptr || !SequencerPtr.IsValid())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("BindPrefabWidget_Transaction", "Bind Prefab Widget"));
	// The custom-binding machinery picks UDreamUIWidgetBinding by itself: it is the only binding
	// type that supports creation from a widget, and it derives the presenter and path from it.
	const FGuid NewGuid = FSequencerUtilities::CreateBinding(SequencerPtr.ToSharedRef(), *Widget);
	if (NewGuid.IsValid())
	{
		SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

void FDreamUISequenceTrackEditor::AddAssetSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid> ObjectBindings)
{
	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassPaths.Add(UDreamUISequence::StaticClass()->GetClassPathName());
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateRaw(this, &FDreamUISequenceTrackEditor::HandleAssetSelected, ObjectBindings);

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	MenuBuilder.AddWidget(
		SNew(SBox)
		.WidthOverride(320.0f)
		.HeightOverride(320.0f)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		],
		FText::GetEmpty(), true, false);
}

void FDreamUISequenceTrackEditor::HandleAssetSelected(const FAssetData& AssetData, TArray<FGuid> ObjectBindings)
{
	FSlateApplication::Get().DismissAllMenus();

	UDreamUISequence* Asset = Cast<UDreamUISequence>(AssetData.GetAsset());
	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (Asset == nullptr || MovieScene == nullptr || MovieScene->IsReadOnly())
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("AddDreamUISequence_Transaction", "Add DreamUI Animation"));
	MovieScene->Modify();

	// The asset's own range, expressed in the outer sequence's resolution.
	const TRange<FFrameNumber> InnerRange = Asset->GetMovieScene()->GetPlaybackRange();
	const FFrameTime InnerDuration = FFrameRate::TransformTime(
		UE::MovieScene::DiscreteSize(InnerRange),
		Asset->GetMovieScene()->GetTickResolution(), MovieScene->GetTickResolution());

	for (const FGuid& ObjectBinding : ObjectBindings)
	{
		UDreamUISequenceTrack* Track = MovieScene->FindTrack<UDreamUISequenceTrack>(ObjectBinding);
		if (Track == nullptr)
		{
			Track = Cast<UDreamUISequenceTrack>(MovieScene->AddTrack(UDreamUISequenceTrack::StaticClass(), ObjectBinding));
		}
		if (Track != nullptr)
		{
			const FFrameNumber StartTime = GetSequencer().IsValid() ? GetSequencer()->GetLocalTime().Time.FrameNumber : FFrameNumber(0);
			Track->AddSequence(Asset, StartTime, FMath::Max(1, InnerDuration.CeilToFrame().Value));
		}
	}
	if (const TSharedPtr<ISequencer> SequencerPtr = GetSequencer())
	{
		SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

#undef LOCTEXT_NAMESPACE
