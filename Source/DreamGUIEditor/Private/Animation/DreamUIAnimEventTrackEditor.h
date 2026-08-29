// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"

/**
 * Puts the DreamUI event track into the sequencer's + Track menu -- for DreamUI prefab sequences
 * only -- and hands its sections to the default section UI (the string channel's own key editor
 * does the rest).
 */
class FDreamUIAnimEventTrackEditor : public FMovieSceneTrackEditor
{
public:
	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);

	FDreamUIAnimEventTrackEditor(TSharedRef<ISequencer> InSequencer);

	//~ ISequencerTrackEditor interface
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> Type) const override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;

private:
	void HandleAddEventTrack();
};
