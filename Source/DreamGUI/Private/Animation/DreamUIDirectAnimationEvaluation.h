// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameTime.h"
#include "Misc/Guid.h"
#include "UObject/WeakObjectPtr.h"

class IMovieScenePlayer;
class UMovieSceneSection;
class UMovieSceneSequence;
class FTrackInstancePropertyBindings;
struct FMovieSceneFloatChannel;
struct FMovieSceneDoubleChannel;
struct FMovieSceneBoolChannel;

/**
 * A widget animation evaluated straight from its channels, for animations that need nothing else of the sequencer.
 *
 * The sequencer evaluates every playing animation through its entity system: each player queues an update, a runner
 * turns the updates into entity passes, and the passes blend channel values and hand them to property systems on the
 * game thread. For a thousand widgets each turning one property that is several milliseconds a frame of machinery
 * around a handful of curve evaluations. An animation made only of property tracks -- one active, absolute section each,
 * no easing, covering the whole playback range -- evaluates to the same values without any of it: each channel at the
 * frame, written through the same setter the property systems call (FTrackInstancePropertyBindings).
 *
 * The player still owns time, loops, direction, pausing, events and finishing (UDreamWidgetAnimationPlayer); only the
 * evaluation it hands the runner is done here instead. What cannot be done the way the sequencer would -- other track
 * types, blending, weights, time warps, spawnables -- is left to the sequencer by TryCreate returning nothing.
 */
class FDreamUIDirectAnimationEvaluation
{
public:
	/** A plan for InSequence, or null when anything in it needs the sequencer. */
	static TSharedPtr<FDreamUIDirectAnimationEvaluation> TryCreate(const UMovieSceneSequence& InSequence);

	~FDreamUIDirectAnimationEvaluation();

	/**
	 * Every animated property of every bound object to its value at InTime, in the movie scene's tick resolution. False
	 * when a bound property turns out to be of a type only the sequencer converts to -- found before anything was
	 * written, so the sequencer can take over from there as if this had never run.
	 */
	bool Evaluate(IMovieScenePlayer& InPlayer, FFrameTime InTime);
	/** Back to the values the properties had before they were first written, as the sequencer restores state. */
	void RestoreInitialValues();
	/** The values written stay, and the initial ones are forgotten, as the sequencer keeps state. */
	void DiscardInitialValues();

private:
	enum class EValueKind : uint8
	{
		Float,
		Double,
		Bool,
		Rotator,
		Vector2d,
		Vector3d,
		Vector4d,
		Vector2f,
		Vector3f,
		Vector4f,
		LinearColor,
		Color,
	};

	/** A property value as its channels see it: up to four numbers, in channel order, or a bool. */
	struct FChannelValues
	{
		double Values[4] = { 0.0, 0.0, 0.0, 0.0 };
		bool bValue = false;
	};

	struct FAnimatedProperty
	{
		FGuid BindingId;
		/** Which track class this came from (DreamUIDirectAnimationLocal::ETrackKind). */
		uint8 TrackKind = 0;
		/** The track's one section; its channels are looked up again should it be gone. */
		TWeakObjectPtr<const UMovieSceneSection> Section;
		/** The property's type, known once the first bound object has been looked at. */
		EValueKind Kind = EValueKind::Float;
		bool bKindKnown = false;
		TSharedPtr<FTrackInstancePropertyBindings> Bindings;
		int32 NumChannels = 0;
		const FMovieSceneFloatChannel* FloatChannels[4] = { nullptr, nullptr, nullptr, nullptr };
		const FMovieSceneDoubleChannel* DoubleChannels[4] = { nullptr, nullptr, nullptr, nullptr };
		const FMovieSceneBoolChannel* BoolChannel = nullptr;
		/** Whether every channel has something to say at every time: then the current value never has to be read. */
		bool bEveryChannelAnimated = false;
		/** The objects written so far, with the value each had before the first write. */
		TArray<TPair<TWeakObjectPtr<UObject>, FChannelValues>> InitialValues;
	};

	/** Points the property at its section's channels. */
	static bool BindChannels(FAnimatedProperty& InOutProperty);
	/** The property's type on InObject, if the track's channels can be written to it the way the sequencer would. */
	static bool FindValueKind(FAnimatedProperty& InOutProperty, UObject& InObject);
	/** Reads the property's value as channel values. */
	static bool Read(FAnimatedProperty& InProperty, UObject& InObject, FChannelValues& OutValues);
	static void Write(FAnimatedProperty& InProperty, UObject& InObject, const FChannelValues& InValues);
	/** Overwrites the channels that have a value at InTime. */
	static void EvaluateChannels(const FAnimatedProperty& InProperty, FFrameTime InTime, FChannelValues& InOutValues);

	/** The sequence the channels belong to: they are only read while it is alive. */
	TWeakObjectPtr<const UMovieSceneSequence> Sequence;
	TArray<FAnimatedProperty> Properties;
	/** Whether any property has been written: after that, the sequencer can no longer take over cleanly. */
	bool bWrittenAnything = false;
	/** Set while Evaluate writes. */
	bool bEvaluating = false;
	/**
	 * Set when the initial values are put back or forgotten while Evaluate writes: a listener of a write stopped the
	 * animation, and nothing more is written for it.
	 */
	bool bStoppedWhileEvaluating = false;
};
