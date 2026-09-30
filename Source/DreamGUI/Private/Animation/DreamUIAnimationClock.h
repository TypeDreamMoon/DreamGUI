// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/QualifiedFrameTime.h"
#include "MovieSceneTimeController.h"

/**
 * The clock DreamGUI's animation component gives its players (UDreamWidgetAnimationComponent::ApplyTimeControl): the engine's
 * tick controller, FMovieSceneTimeController_Tick -- the time is where the play started, plus what the ticks it was given
 * add up to -- with that sum and that start where a player can read them. A player ticking itself advances it and reads the
 * time off it in line (UDreamWidgetAnimationPlayer::TickLite), which for a wall of playing widgets was two calls into the
 * engine and two virtual calls each, every frame; the sequencer's update gets the same answers through the virtuals.
 */
struct FDreamUIAnimationClock : FMovieSceneTimeController
{
	/**
	 * A tick adds the delta it is given, as the engine's tick controller's does. Not so for a clock that changes the delta
	 * first -- one that undoes the world's time dilation -- whose tick the player leaves to the virtual.
	 */
	bool bTicksAsGiven = true;
	/** What the ticks since the play started add up to, in seconds at the rate played: the engine's CurrentOffsetSeconds. */
	double OffsetSeconds = 0.0;
	/** Where the play started, while it plays: the base's playback start time, which it keeps to itself. */
	TOptional<FQualifiedFrameTime> StartTime;

	/** Tick, in line. Only for a clock that ticks as given (bTicksAsGiven). */
	void TickInLine(float DeltaSeconds, float InPlayRate)
	{
		OffsetSeconds += DeltaSeconds * InPlayRate;
	}
	/**
	 * RequestCurrentTime, in line: the start, and what the ticks added up to, in the rate of InCurrentTime; InCurrentTime
	 * itself while not playing. The display rate RequestCurrentTime keeps is not kept: nothing of this clock reads it.
	 */
	FFrameTime TimeInLine(const FQualifiedFrameTime& InCurrentTime) const
	{
		if (!StartTime.IsSet())
		{
			return InCurrentTime.Time;
		}
		return StartTime->ConvertTo(InCurrentTime.Rate) + OffsetSeconds * InCurrentTime.Rate;
	}

protected:
	virtual void OnTick(float DeltaSeconds, float InPlayRate) override
	{
		TickInLine(DeltaSeconds, InPlayRate);
	}
	virtual void OnStartPlaying(const FQualifiedFrameTime& InStartTime) override
	{
		OffsetSeconds = 0.0;
		StartTime = InStartTime;
	}
	virtual void OnStopPlaying(const FQualifiedFrameTime& InStopTime) override
	{
		StartTime.Reset();
	}
	virtual FFrameTime OnRequestCurrentTime(const FQualifiedFrameTime& InCurrentTime, float InPlayRate) override
	{
		return TimeInLine(InCurrentTime);
	}
};
