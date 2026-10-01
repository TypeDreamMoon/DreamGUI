// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Misc/QualifiedFrameTime.h"
#include "MovieSceneTimeController.h"

/**
 * The clock DreamGUI's animation component gives its players (UDreamWidgetAnimationComponent::ApplyTimeControl): the engine's
 * tick controller, FMovieSceneTimeController_Tick -- the time is where the play started, plus what the ticks it was given
 * add up to -- with that sum and that start kept where a player can read them: in the player it is given to (AttachTo). A
 * player ticking itself advances it and reads the time off it in line (UDreamWidgetAnimationPlayer::TickLite), which for a
 * wall of playing widgets was two calls into the engine and two virtual calls each, every frame, and then a read of the clock
 * from memory; the sequencer's update gets the same answers through the virtuals.
 */
struct FDreamUIAnimationClock : FMovieSceneTimeController
{
	FDreamUIAnimationClock() = default;
	/** Never copied: the state may be kept in a player, which a copy would not know to leave. */
	FDreamUIAnimationClock(const FDreamUIAnimationClock&) = delete;
	FDreamUIAnimationClock& operator=(const FDreamUIAnimationClock&) = delete;

	/**
	 * A tick adds the delta it is given, as the engine's tick controller's does. Not so for a clock that changes the delta
	 * first -- one that undoes the world's time dilation -- whose tick the player leaves to the virtual.
	 */
	bool bTicksAsGiven = true;

	/** The state is kept in InPlayerState from now on, as it is now. */
	void AttachTo(FDreamUIAnimationClockState& InPlayerState)
	{
		InPlayerState = *State;
		State = &InPlayerState;
	}
	/** The state is kept in the clock again, as it is now: the player it was kept in is going, or trusts another clock. */
	void Detach()
	{
		OwnState = *State;
		State = &OwnState;
	}

	/** Tick, in line, on InState. Only for a clock that ticks as given (bTicksAsGiven). */
	static void TickInLine(FDreamUIAnimationClockState& InState, float DeltaSeconds, float InPlayRate)
	{
		InState.OffsetSeconds += DeltaSeconds * InPlayRate;
	}
	/**
	 * RequestCurrentTime, in line, from InState: the start, and what the ticks added up to, in the rate of InCurrentTime;
	 * InCurrentTime itself while not playing. The display rate RequestCurrentTime keeps is not kept: nothing of this clock
	 * reads it.
	 */
	static FFrameTime TimeInLine(const FDreamUIAnimationClockState& InState, const FQualifiedFrameTime& InCurrentTime)
	{
		if (!InState.StartTime.IsSet())
		{
			return InCurrentTime.Time;
		}
		return InState.StartTime->ConvertTo(InCurrentTime.Rate) + InState.OffsetSeconds * InCurrentTime.Rate;
	}

protected:
	virtual void OnTick(float DeltaSeconds, float InPlayRate) override
	{
		TickInLine(*State, DeltaSeconds, InPlayRate);
	}
	virtual void OnStartPlaying(const FQualifiedFrameTime& InStartTime) override
	{
		State->OffsetSeconds = 0.0;
		State->StartTime = InStartTime;
	}
	virtual void OnStopPlaying(const FQualifiedFrameTime& InStopTime) override
	{
		State->StartTime.Reset();
	}
	virtual FFrameTime OnRequestCurrentTime(const FQualifiedFrameTime& InCurrentTime, float InPlayRate) override
	{
		return TimeInLine(*State, InCurrentTime);
	}

private:
	/** The state while no player keeps it. */
	FDreamUIAnimationClockState OwnState;
	/** Where the state is kept now: here, or in the player it was given to. */
	FDreamUIAnimationClockState* State = &OwnState;
};
