// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIAnimEventTrackEditor.h"
#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamUISequence.h"
#include "MovieScene.h"
#include "ISequencerSection.h"
#include "SequencerSectionPainter.h"
#include "ScopedTransaction.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "DreamUIAnimEventTrackEditor"

TSharedRef<ISequencerTrackEditor> FDreamUIAnimEventTrackEditor::CreateTrackEditor(TSharedRef<ISequencer> InSequencer)
{
	return MakeShared<FDreamUIAnimEventTrackEditor>(InSequencer);
}

FDreamUIAnimEventTrackEditor::FDreamUIAnimEventTrackEditor(TSharedRef<ISequencer> InSequencer)
	: FMovieSceneTrackEditor(InSequencer)
{
}

bool FDreamUIAnimEventTrackEditor::SupportsSequence(UMovieSceneSequence* InSequence) const
{
	// Both DreamUI sequence forms. The trigger resolves through the hosting sequence component, and a
	// standalone .DreamUISequence asset reaches one just as an embedded animation does: it is played
	// by a component, whose widget is the playback context, and UDreamUIAnimEventSection::Trigger
	// falls back to that context for exactly this case. Refusing the asset form here is what made
	// that fallback unreachable -- a branch written for a shape the editor would not author.
	return InSequence != nullptr && (InSequence->IsA<UDreamWidgetAnimation>() || InSequence->IsA<UDreamUISequence>());
}

bool FDreamUIAnimEventTrackEditor::SupportsType(TSubclassOf<UMovieSceneTrack> Type) const
{
	return Type == UDreamUIAnimEventTrack::StaticClass();
}

void FDreamUIAnimEventTrackEditor::BuildAddTrackMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("AddEventTrack", "DreamUI Event"),
		LOCTEXT("AddEventTrackTooltip", "A row of named triggers, broadcast through the sequence component's On Animation Event while the animation plays."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Sequencer.Tracks.Event"),
		FUIAction(FExecuteAction::CreateRaw(this, &FDreamUIAnimEventTrackEditor::HandleAddEventTrack)));
}

TSharedRef<ISequencerSection> FDreamUIAnimEventTrackEditor::MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding)
{
	return MakeShared<FSequencerSection>(SectionObject);
}

void FDreamUIAnimEventTrackEditor::HandleAddEventTrack()
{
	UMovieScene* MovieScene = GetFocusedMovieScene();
	if (MovieScene == nullptr || MovieScene->IsReadOnly())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("AddEventTrack_Transaction", "Add DreamUI Event Track"));
	MovieScene->Modify();
	UDreamUIAnimEventTrack* Track = MovieScene->AddTrack<UDreamUIAnimEventTrack>();
	checkf(Track, TEXT("AddTrack returned null for the event track"));
	Track->AddSection(*Track->CreateNewSection());
	if (const TSharedPtr<ISequencer> SequencerPtr = GetSequencer())
	{
		SequencerPtr->NotifyMovieSceneDataChanged(EMovieSceneDataChangeType::MovieSceneStructureItemAdded);
	}
}

#undef LOCTEXT_NAMESPACE
