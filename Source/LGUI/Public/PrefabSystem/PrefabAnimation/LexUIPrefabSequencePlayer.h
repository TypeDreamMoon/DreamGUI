// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "LexUIPrefabSequencePlayer.generated.h"

/**
 * ULexUIPrefabSequencePlayer is used to actually "play" an actor sequence asset at runtime.
 */
UCLASS(BlueprintType)
class LGUI_API ULexUIPrefabSequencePlayer
	: public UMovieSceneSequencePlayer
{
public:
	GENERATED_BODY()

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;
};

