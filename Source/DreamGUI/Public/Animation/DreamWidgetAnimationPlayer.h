// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "DreamWidgetAnimationPlayer.generated.h"

/**
 * UDreamWidgetAnimationPlayer is used to actually "play" an actor sequence asset at runtime.
 */
UCLASS(BlueprintType, DisplayName="DreamUI Widget Animation Player")
class DREAMGUI_API UDreamWidgetAnimationPlayer
	: public UMovieSceneSequencePlayer
{
public:
	GENERATED_BODY()

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;
};

