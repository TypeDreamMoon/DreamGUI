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

	/** Whether this player evaluates its animation itself rather than through the sequencer (see UpdateMovieSceneInstance). */
	bool IsEvaluatingDirectly() const { return DirectEvaluation.IsValid(); }

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;

	using Super::UpdateMovieSceneInstance;
	/**
	 * The sequencer's player keeps time, loops, direction, pauses and finishing, and hands each evaluation to its runner
	 * here. An animation made only of plain property tracks is evaluated here instead, straight from its channels
	 * (FDreamUIDirectAnimationEvaluation): the same values through the same setters, without the entity system's passes.
	 * Anything else -- and anything with weights, or a completion mode the component did not choose -- goes on to the
	 * sequencer as before. DreamUI.Animation.DirectEvaluation 0 sends everything to the sequencer.
	 */
	virtual void UpdateMovieSceneInstance(FMovieSceneEvaluationRange InRange, EMovieScenePlayerStatus::Type PlayerStatus, const FMovieSceneUpdateArgs& Args) override;
	/** Restores or forgets the values a direct evaluation wrote, as the sequencer's completion mode says. */
	virtual void OnStopped() override;

private:
	/** Evaluates InRange directly if this player can; false when the sequencer has to. */
	bool TryEvaluateDirectly(const FMovieSceneEvaluationRange& InRange, EMovieScenePlayerStatus::Type PlayerStatus);

	TSharedPtr<class FDreamUIDirectAnimationEvaluation> DirectEvaluation;
	/**
	 * The sequence whether to evaluate directly was decided for, at its first evaluation after a stop, when the playback
	 * settings are final. A player the component re-initializes with another sequence decides again.
	 */
	TWeakObjectPtr<const UMovieSceneSequence> DirectEvaluationDecidedFor;
};
