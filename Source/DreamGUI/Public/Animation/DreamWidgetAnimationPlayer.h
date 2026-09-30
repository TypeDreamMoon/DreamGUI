// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "DreamWidgetAnimationPlayer.generated.h"

class AActor;

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

	/**
	 * The instance of its animation this player is playing, or played last: counted up each time one starts on it
	 * (BeginInstance). A player plays its animation's next instance once one ends (UDreamWidgetAnimationComponent::
	 * SparePlayers), and a handle names the instance it was handed out for (FDreamUIAnimationHandle::Instance).
	 */
	uint32 GetInstance() const { return Instance; }
	/** A new instance starts on this player; see GetInstance. */
	void BeginInstance() { ++Instance; }
	/**
	 * Makes this player, whose last instance ended, ready to play its sequence again with InSettings, as Initialize would
	 * have made a new player -- the sequence's whole range, the start offset, the settings -- without Initialize's
	 * registration and set-up, which it keeps from the first time. False, and nothing touched, when it cannot: still
	 * playing or paused, no longer set up, or InSettings asks for what only Initialize does (another tick interval, dynamic
	 * weighting, a random start time).
	 */
	bool TryPrepareReplay(const FMovieSceneSequencePlaybackSettings& InSettings);

	/**
	 * Vouches for the time controller the player has now as one the player may tick and read itself (see
	 * TickFromSequenceTickManager): the engine's tick controller, or one built on it that changes only what a tick adds, as
	 * the component's unscaled clock does. Neither reads the play rate it is asked for the time with. Held by identity, so
	 * a controller set after this is not the one vouched for, and hands the player back to the sequencer's update. The
	 * component calls it when it gives a player its clock.
	 */
	void TrustTimeController();

	/**
	 * Whether the sequence tick manager's last tick of this player was taken by the player itself rather than by the
	 * sequencer's update (see TickFromSequenceTickManager).
	 */
	bool IsTickingLite() const { return bTickedLite; }

protected:

	//~ IMovieScenePlayer interface
	virtual UObject* GetPlaybackContext() const override;
	virtual TArray<UObject*> GetEventContexts() const override;

	//~ IMovieSceneSequenceTickManagerClient interface
	/**
	 * The sequence tick manager's tick of this player. The sequencer answers it with its whole per-frame update (Update and
	 * UpdateTimeCursorPosition_Internal), which for an animation this player evaluates itself was most of what a playing
	 * widget animation cost a frame. So between loop boundaries the player takes the update's steps itself: it ticks the
	 * time controller, asks it for the time, moves the cursor and hands the range to UpdateMovieSceneInstance, and leaves
	 * the sequencer's state exactly as the sequencer's update would have.
	 *
	 * - The choice is made before anything is touched. Anything else goes to the sequencer's update whole: a player still
	 *   starting, syncing or skipping, a play of no length or without a positive rate, a clock the component did not vouch
	 *   for (TrustTimeController), an animation the sequencer evaluates, weights, an observer or a playback client, and a
	 *   play-rate time warp.
	 * - The time controller is ticked once a frame and is the only source of the time.
	 * - The cursor moves only by PlayTo. The loop count, the direction, the status and a PlayTo pause change only in the
	 *   sequencer's code: at a boundary the player hands the time to UpdateTimeCursorPosition, which does the rest.
	 * - The player stays registered with the tick manager, so its latent actions and its linker are what they would be.
	 * - What is left different is what nothing reads: the game time of the last tick (LastTickGameTimeSeconds).
	 *
	 * A player that is not playing, with nothing to sync or start, gets nothing from the sequencer's update and skips it.
	 * DreamUI.Animation.LitePlayer 0 sends every tick to the sequencer. It applies from the next frame either way, since
	 * the sequencer's state is the player's state whichever update ran.
	 */
	virtual void TickFromSequenceTickManager(float DeltaSeconds, FMovieSceneEntitySystemRunner* Runner) override;
	/** Notes whether this play is the network authority; see TickLite. */
	virtual void OnStartedPlaying() override;

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
	/** Whether the root of the sequence's hierarchy warps time in the play-rate domain; see TryEvaluateDirectly. */
	bool IsRootPlayRateWarped() const;

	/** What the tick manager's tick of this player does this frame; see TickFromSequenceTickManager. */
	enum class ELiteTick : uint8
	{
		/** The sequencer's update, whole. */
		Sequencer,
		/** Nothing: the player is not playing, and the sequencer's update would do nothing either. */
		Idle,
		/** The player's own update of a playing animation (TickLite). */
		Advance,
	};
	ELiteTick ChooseLiteTick() const;
	/** One frame of a playing animation, as the sequencer's update would have played it. */
	void TickLite(float DeltaSeconds);

	TSharedPtr<class FDreamUIDirectAnimationEvaluation> DirectEvaluation;
	/**
	 * The direct evaluation of the play before, kept by OnStopped for the next play of the same sequence: made from the
	 * sequence's bindings, tracks and sections, with the bound objects, their properties and setters found, it is as good
	 * as new while the movie scene is unchanged (FDreamUIDirectAnimationEvaluation::IsStillPlanFor). A wall of widgets
	 * starting an animation together made thousands of them again in one frame.
	 */
	TSharedPtr<class FDreamUIDirectAnimationEvaluation> KeptDirectEvaluation;
	/**
	 * The sequence whether to evaluate directly was decided for, at its first evaluation after a stop, when the playback
	 * settings are final. A player the component re-initializes with another sequence decides again.
	 */
	TWeakObjectPtr<const UMovieSceneSequence> DirectEvaluationDecidedFor;

	/** The time controller vouched for (TrustTimeController). Held, so that no other controller can come to have its address. */
	TSharedPtr<FMovieSceneTimeController> TrustedTimeController;
	/** Whether the actor the sequencer asks is the network authority, as of the start of the play (OnStartedPlaying). */
	bool bAuthorityAtStart = false;
	/** See IsTickingLite. */
	bool bTickedLite = false;
	/** See GetInstance. */
	uint32 Instance = 0;
};
