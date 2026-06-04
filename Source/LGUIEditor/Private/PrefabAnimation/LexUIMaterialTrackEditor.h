// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ISequencer.h"
#include "MovieSceneTrack.h"
#include "ISequencerTrackEditor.h"
#include "TrackEditors/MaterialTrackEditor.h"

/**
 * A specialized material track editor for LGUI custom materials
 */
class FLexUIMaterialTrackEditor
	: public FMaterialTrackEditor
{
public:

	FLexUIMaterialTrackEditor( TSharedRef<ISequencer> InSequencer );

	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor( TSharedRef<ISequencer> OwningSequencer );

public:

	// ISequencerTrackEditor interface

	virtual bool SupportsType( TSubclassOf<UMovieSceneTrack> Type ) const override;

protected:

	// FMaterialtrackEditor interface

	virtual UMaterialInterface* GetMaterialInterfaceForTrack( FGuid ObjectBinding, UMovieSceneMaterialTrack* MaterialTrack ) override;
};
