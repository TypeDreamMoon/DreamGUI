// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "DreamUIPrefabSequencePlayer.generated.h"

/**
 * UDreamUIPrefabSequencePlayer is used to actually "play" an actor sequence asset at runtime.
 */
UCLASS(BlueprintType, DisplayName="DreamUI Prefab Sequence Player")
class DREAMGUI_API UDreamUIPrefabSequencePlayer
	: public UMovieSceneSequencePlayer
{
public:
	GENERATED_BODY()

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;
};

