// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "DreamWidgetAnimationPlayer.generated.h"

/**
 * UDreamWidgetAnimationPlayer is used to actually "play" a widget animation at runtime.
 *
 * Always created with a UDreamWidgetAnimationComponent as its outer, and that outer is what it
 * plays for: the playback context is the component's widget whichever sequence is loaded -- an
 * embedded UDreamWidgetAnimation or a standalone UDreamUISequence asset -- so bindings resolve
 * against the widget hosting the player.
 */
UCLASS(BlueprintType, DisplayName="DreamUI Widget Animation Player")
class DREAMGUI_API UDreamWidgetAnimationPlayer
	: public UMovieSceneSequencePlayer
{
public:
	GENERATED_BODY()

	/** How many more times to loop after the current pass; -1 loops indefinitely. Takes effect at the next loop boundary. */
	void SetLoopCount(int32 InLoopCount);

	/** Runs any evaluation this player has queued for the frame, so a value set now is on the widget before this returns. */
	void FlushQueuedEvaluation();

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;
};
