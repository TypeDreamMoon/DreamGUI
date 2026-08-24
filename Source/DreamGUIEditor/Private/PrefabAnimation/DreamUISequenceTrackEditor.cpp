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
	if (ObjectClass == nullptr
		|| !(ObjectClass->IsChildOf(UDreamWidget::StaticClass()) || ObjectClass->IsChildOf(UDreamWidgetPresenterComponentBase::StaticClass())))
	{
		return;
	}
	MenuBuilder.AddSubMenu(
		LOCTEXT("AddDreamUISequence", "DreamUI Animation"),
		LOCTEXT("AddDreamUISequenceTooltip", "Play a DreamUI animation asset on this widget: the asset's root binding resolves as this binding's object."),
		FNewMenuDelegate::CreateRaw(this, &FDreamUISequenceTrackEditor::AddAssetSubMenu, TArray<FGuid>(ObjectBindings)));
}

TSharedRef<ISequencerSection> FDreamUISequenceTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShared<TSubSectionMixin<>>(GetSequencer(), *CastChecked<UDreamUISequenceSection>(&SectionObject));
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
