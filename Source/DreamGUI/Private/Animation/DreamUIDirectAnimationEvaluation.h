// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/FrameRate.h"
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
	 * written, so the sequencer can take over from there as if this had never run. InPlayer plays the sequence this was
	 * made for and holds it: its channels are read without asking whether it is still alive.
	 */
	bool Evaluate(IMovieScenePlayer& InPlayer, FFrameTime InTime);
	/** Back to the values the properties had before they were first written, as the sequencer restores state. */
	void RestoreInitialValues();
	/** The movie scene's tick resolution when this was made (TryCreate): the resolution Evaluate takes its time in. */
	FFrameRate GetTickResolution() const { return TickResolution; }
	/**
	 * Whether this is still what TryCreate would make for InSequence: made for it, and its movie scene not edited since
	 * -- every edit of a binding, a track, a section or a key changes the movie scene's signature.
	 */
	bool IsStillPlanFor(const UMovieSceneSequence& InSequence) const;
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
		/** The track names a property of the bound object itself, not one inside a struct of it. */
		bool bDirectProperty = false;
		/**
		 * The property's native setter, for objects of SetterClass: the write the bindings would make, without looking the
		 * object up in their map every time (WriteValue). Found with the property's type, once a play; none for a property
		 * without a native setter, or down a path, which the bindings write as they always did.
		 */
		const UClass* SetterClass = nullptr;
		const FProperty* SetterProperty = nullptr;
		int32 NumChannels = 0;
		/**
		 * The section the channels below were found in, while its sequence is alive (Evaluate), and where in it each channel
		 * is: with the section's signature, what says which channel of which content it is (EvaluateChannels).
		 */
		const UMovieSceneSection* BoundSection = nullptr;
		/**
		 * The section's signature when the channels were found in it: what EvaluateChannels keys the channels' shared
		 * values by, without a read of the section -- every widget's copy of an animation has sections of its own -- for
		 * every player every frame. An edit of the section changes its movie scene's signature as well, and the next play
		 * makes a plan again (IsStillPlanFor); one made while a play goes on keys the channels as before it until then.
		 */
		FGuid BoundSignature;
		uint32 ChannelOffsets[4] = { 0, 0, 0, 0 };
		const FMovieSceneFloatChannel* FloatChannels[4] = { nullptr, nullptr, nullptr, nullptr };
		const FMovieSceneDoubleChannel* DoubleChannels[4] = { nullptr, nullptr, nullptr, nullptr };
		const FMovieSceneBoolChannel* BoolChannel = nullptr;
		/** Whether every channel has something to say at every time: then the current value never has to be read. */
		bool bEveryChannelAnimated = false;
		/** The objects written so far, with the value each had before the first write. */
		TArray<TPair<TWeakObjectPtr<UObject>, FChannelValues>> InitialValues;
		/**
		 * The first entry of InitialValues, while there is one: found by its key without a read of the array, a block of
		 * its own, for every player every frame -- an animation mostly writes one object.
		 */
		TWeakObjectPtr<UObject> FirstInitialKey;
		FChannelValues FirstInitialValues;
		/**
		 * The objects the binding resolved to at this play's first evaluation, as the player's object cache gave them: looked
		 * up again only once one of them is gone. A play of a widget animation does not see its binding resolve to other
		 * objects -- nothing in DreamGUI invalidates a player's bindings, and the sequencer resolves one again when its object
		 * went away -- and the lookup, a map in the player's state and another in its object cache, came to a twentieth of
		 * what a wall of playing animations cost a frame.
		 */
		TArray<TWeakObjectPtr<UObject>, TInlineAllocator<1>> BoundObjects;
		bool bBoundObjectsFound = false;
		/**
		 * The one object the binding resolved to is the player's playback context -- the widget whose animation component
		 * made the player -- which lives while the player plays: the component is the widget's own, and stops its plays when
		 * it goes. Written without a weak look-up of it every frame.
		 */
		UObject* BoundHost = nullptr;
		/**
		 * The one object the binding resolved to, when it is a DreamGUI widget or behaviour, as a weak look-up found it alive
		 * while the count of objects gone read BoundSingleGone (DreamUIGone): while the count reads the same, it is that
		 * object, alive and as registered as it was, and it is written without a weak look-up of it frame after frame.
		 */
		UObject* BoundSingle = nullptr;
		uint64 BoundSingleGone = 0;
		/**
		 * The object the native setter was last found to fit or not (SetterClass), by address: what WriteValue asks of its
		 * class -- in its header, a read from memory every write -- is asked again only of another object.
		 */
		const UObject* SetterCheckedObject = nullptr;
		bool bSetterFits = false;
	};

	/** Points the property at its section's channels. */
	static bool BindChannels(FAnimatedProperty& InOutProperty);
	/** The property's type on InObject, if the track's channels can be written to it the way the sequencer would. */
	static bool FindValueKind(FAnimatedProperty& InOutProperty, UObject& InObject);
	/** Reads the property's value as channel values. */
	static bool Read(FAnimatedProperty& InProperty, UObject& InObject, FChannelValues& OutValues);
	static void Write(FAnimatedProperty& InProperty, UObject& InObject, const FChannelValues& InValues);
	/** One value into the property: through its native setter when it has one for InObject's class, else through the bindings. */
	template<typename ValueType>
	static void WriteValue(FAnimatedProperty& InProperty, UObject& InObject, const ValueType& InValue);
	/** Overwrites the channels that have a value at InTime. */
	static void EvaluateChannels(const FAnimatedProperty& InProperty, FFrameTime InTime, FChannelValues& InOutValues);

	/** The sequence the channels belong to: they are only read while it is alive. */
	TWeakObjectPtr<const UMovieSceneSequence> Sequence;
	/** See GetTickResolution. Kept here so that a frame's evaluation need not look at the movie scene for it. */
	FFrameRate TickResolution;
	/** The movie scene's signature when this was made: see IsStillPlanFor. */
	FGuid MovieSceneSignature;
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
