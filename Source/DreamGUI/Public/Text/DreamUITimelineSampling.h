// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameNumber.h"

/**
 * The keys a `timeline` track line becomes: the one definition of it, for the builder that makes the animation from the
 * line and for the editor that reads an edit of that animation back into the line.
 *
 * Each key the line writes is a key at its time. Between a key with a named ease and the next one, the ease is sampled at
 * the display rate into linear keys (DreamUITextBuilder.cpp says why a curve name becomes samples rather than tangents).
 * Reading an edit back means telling those samples from the author's keys, which is only exact when both sides sample
 * the same way -- the same count, the same frames, the same values -- and they did by being two copies of this.
 */
namespace DreamUITimelineSampling
{
	/** The tick resolution and display rate every language-owned timeline is built at. */
	inline constexpr int32 TickResolution = 24000;
	inline constexpr int32 DisplayRate = 60;

	/** A time in seconds as the frame a key stands on, at InTickRate ticks a second. */
	DREAMGUI_API FFrameNumber TimeToFrame(double InSeconds, double InTickRate = TickResolution);

	/** One key of a line, as the builder reads it: when, the value per channel, and the curve to the next key. */
	struct FLineKey
	{
		double Time = 0.0;
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		/** Empty or "Linear" for a straight line to the next key; on the last key it goes nowhere. */
		FString EaseName;
	};

	/** One key of what a line builds: a key of the line itself, or one sample of the ease out of a key of it. */
	struct FSample
	{
		FFrameNumber Frame;
		double Channels[4] = { 0.0, 0.0, 0.0, 0.0 };
		/** The line key this is -- or, for a sample, the line key whose ease it samples. */
		int32 LineKey = INDEX_NONE;
		bool bIsLineKey = false;
	};

	/**
	 * Every key InKeys build, in order, at InTickRate. False, naming it in OutBadEaseName, for an ease name that is no
	 * curve -- the set is EDreamTweenEase's, minus CurveFloat, which names an asset a key has nowhere to hold.
	 */
	DREAMGUI_API bool Sample(TConstArrayView<FLineKey> InKeys, int32 InChannelCount, double InTickRate, TArray<FSample>& OutSamples,
		FString& OutBadEaseName);
}
