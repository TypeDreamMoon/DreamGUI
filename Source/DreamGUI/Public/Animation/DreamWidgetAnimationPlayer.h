// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "MovieSceneSequencePlayer.h"
#include "DreamWidgetAnimationPlayer.generated.h"

class AActor;
class UDreamUIAnimationTicker;
struct FDreamUIAnimationClock;

/** What DreamGUI's animation clock keeps (FDreamUIAnimationClock): where the play started, and what the ticks added up to since. */
struct FDreamUIAnimationClockState
{
	double OffsetSeconds = 0.0;
	TOptional<FQualifiedFrameTime> StartTime;
};

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
	 * UMovieSceneSequencePlayer::Initialize, after which the next evaluation decides again whether this player evaluates its
	 * animation itself: the sequence it decided for is known by its address (DirectEvaluationDecidedFor), which a sequence
	 * made after this one's end could come to have.
	 */
	void Initialize(UMovieSceneSequence* InSequence, const FMovieSceneSequencePlaybackSettings& InSettings);
	/** As the other Initialize, with the settings the player has. */
	void Initialize(UMovieSceneSequence* InSequence);

	/**
	 * Before Initialize: the player is ticked by InTicker instead of the world's sequence tick manager, and registers with the
	 * ticker's own tick manager (UDreamUIAnimationTicker::GetRegistry), whose linker, runner and latent actions are its own.
	 */
	void UseTicker(UDreamUIAnimationTicker* InTicker);
	/**
	 * Ticked from the ticker's next tick on, if a ticker ticks this player: after anything that starts a play or gives it
	 * something to do. A ticker lets a player go once it has nothing to (HasNothingToTick).
	 */
	void KeepTicked();

	/**
	 * Vouches for the time controller the player has now as one the player may tick and read itself (see
	 * TickFromSequenceTickManager): the engine's tick controller, or one built on it that changes only what a tick adds, as
	 * the component's unscaled clock does. Neither reads the play rate it is asked for the time with. Held by identity, so
	 * a controller set after this is not the one vouched for, and hands the player back to the sequencer's update. The
	 * component calls it when it gives a player its clock.
	 */
	void TrustTimeController();
	/**
	 * Gives the player InClock, DreamGUI's clock, and vouches for it as TrustTimeController does: the player's own tick then
	 * ticks it and reads the time off it in line (FDreamUIAnimationClock). The component calls it when it gives a player its
	 * clock.
	 */
	void TrustClock(const TSharedPtr<FDreamUIAnimationClock>& InClock);

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

	/** Out of its ticker's list, if a ticker ticks it: a player is let go of there only while the ticker ticks otherwise. */
	virtual void BeginDestroy() override;

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
	friend class UDreamUIAnimationTicker;

	/**
	 * Evaluates InRange directly if this player can; false when the sequencer has to. bInOwnTick: from the player's own tick
	 * between boundaries (TickLite), where the pre- and post-evaluation have nothing to do unless the update event is
	 * listened to.
	 */
	bool TryEvaluateDirectly(const FMovieSceneEvaluationRange& InRange, EMovieScenePlayerStatus::Type PlayerStatus, bool bInOwnTick = false);
	/**
	 * Nothing for a tick of this player to do: not playing, its clock ready, nothing to sync -- what the sequencer's update
	 * does nothing for (ChooseLiteTick's Idle). Its ticker lets it go.
	 */
	bool HasNothingToTick() const;
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
	 * settings are final. A player the component re-initializes with another sequence decides again. Only ever compared
	 * with the sequence the player holds, which is alive; forgotten at every stop and every Initialize, so that no other
	 * sequence can come to have its address while it is kept -- every frame of a wall of widgets asked for it by a weak
	 * look-up.
	 */
	const UMovieSceneSequence* DirectEvaluationDecidedFor = nullptr;
	/**
	 * The sequence's display rate as of that decision, which the player's own tick asks the clock for the time in (TickLite):
	 * the sequencer's update reads it from the movie scene every frame, and a wall of widgets each has its own movie scene.
	 */
	FFrameRate LiteDisplayRate;

	/** The time controller vouched for (TrustTimeController). Held, so that no other controller can come to have its address. */
	TSharedPtr<FMovieSceneTimeController> TrustedTimeController;
	/** The vouched-for controller, when it is DreamGUI's clock (TrustClock), which the player's own tick reads in line. */
	FDreamUIAnimationClock* TrustedClock = nullptr;
	/**
	 * The trusted clock's state, kept here while the clock is attached to this player (FDreamUIAnimationClock::AttachTo), and
	 * whether it ticks as given: what the player's own tick reads and writes of the clock, without a read of the clock.
	 */
	FDreamUIAnimationClockState ClockState;
	bool bClockTicksAsGiven = false;
	/** Whether the actor the sequencer asks is the network authority, as of the start of the play (OnStartedPlaying). */
	bool bAuthorityAtStart = false;
	/** See IsTickingLite. */
	bool bTickedLite = false;
	/** See GetInstance. */
	uint32 Instance = 0;

	/** The ticker ticking this player (UseTicker), which it keeps alive while the player lives. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamUIAnimationTicker> Ticker;
	/** Where the player is in its ticker's list, while it is in it. */
	int32 TickerActiveIndex = INDEX_NONE;
	/** The tick of its ticker the player was last ticked in: see UDreamUIAnimationTicker::TickSerial. */
	uint64 LastTickerTick = 0;
};
