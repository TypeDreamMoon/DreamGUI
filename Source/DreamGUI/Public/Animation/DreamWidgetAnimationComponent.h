// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/ActorComponent.h"
#include "MovieSceneSequencePlayer.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamWidgetAnimationComponent.generated.h"


class UDreamWidgetAnimation;
class UDreamUISequence;
class UDreamUserWidget;
class UDreamWidgetAnimationPlayer;

UENUM(BlueprintType)
enum class EDreamUIAnimationPlayMode : uint8
{
	Forward,
	Reverse,
};

/** The two moments of an animation instance a bound delegate can wait for. */
UENUM(BlueprintType)
enum class EDreamUIAnimationEvent : uint8
{
	Started,
	Finished,
};

/**
 * One playing instance of an animation. PlayAnimation hands one back; every per-instance call
 * (pause, stop, reverse, seek, speed, loops) takes it. Cheap to copy, safe to keep: once the
 * instance ends the handle simply stops being valid.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIAnimationHandle
{
	GENERATED_BODY()

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DreamUI|Animation")
	TObjectPtr<UDreamWidgetAnimationPlayer> Player = nullptr;

	bool IsValid() const;
	/** The animation this instance plays, or null once the instance is gone. */
	UMovieSceneSequence* GetAnimation() const;
};

/** Fired by a DreamUI Event track key while an animation plays. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIAnimEventDelegate, FName, EventName);
/** What BindToAnimationStarted / BindToAnimationFinished take: UMG's FWidgetAnimationDynamicEvent. */
DECLARE_DYNAMIC_DELEGATE(FDreamUIAnimationDynamicEvent);
/** The animation whose instance just started, or just ended (naturally or by Stop). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIAnimationObjectEvent, UMovieSceneSequence*, Animation);
/** Native, per instance: for code holding a handle, such as the async Finished node. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIAnimationInstanceEvent, const FDreamUIAnimationHandle&);

/** A delegate waiting on one animation's Started or Finished; see BindToAnimationEvent. */
USTRUCT()
struct FDreamUIAnimationEventBinding
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UMovieSceneSequence> Animation = nullptr;

	UPROPERTY()
	FDreamUIAnimationDynamicEvent Delegate;

	UPROPERTY()
	EDreamUIAnimationEvent Event = EDreamUIAnimationEvent::Started;
};

/**
 * Movie scene animation embedded within DreamUIPrefab.
 *
 * The playback API mirrors UUserWidget's, with one deliberate difference: an animation can play
 * several instances at once here, so the per-instance calls take the handle PlayAnimation returned
 * rather than the animation object. The "...Of" and Find calls are the bridge for a caller that
 * only has the object.
 */
UCLASS(Blueprintable, ClassGroup=DreamGUI, meta=(BlueprintSpawnableComponent), DisplayName="DreamUI Widget Animation Component")
class DREAMGUI_API UDreamWidgetAnimationComponent
	: public UDreamUIBehaviour
{
public:
	GENERATED_BODY()

	UDreamWidgetAnimationComponent();

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidgetAnimation* GetSequenceByDisplayName(const FString& InName) const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidgetAnimation* GetSequenceByIndex(int32 InIndex) const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		const TArray<UDreamWidgetAnimation*>& GetSequenceArray() const { return SequenceArray; }
	/** The standalone sequence assets on this component, which SequenceArray does not include. */
		const TArray<TObjectPtr<UDreamUISequence>>& GetSequenceAssets() const { return SequenceAssets; }
	/** Init SequencePlayer with current sequence. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void InitSequencePlayer();
	/** Find animation in SequenceArray by Index, then set it to SequencePlayer. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSequenceByIndex(int32 InIndex);
	/** Find animation in SequenceArray by Name, then set it to SequencePlayer */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSequenceByDisplayName(const FString& InName);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int32 GetCurrentSequenceIndex()const { return CurrentSequenceIndex; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidgetAnimation* GetCurrentSequence() const { return GetSequenceByIndex(CurrentSequenceIndex); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidgetAnimationPlayer* GetSequencePlayer() const { return SequencePlayer; }

	// ------------------------------------------------------------------------------- play

	/**
	 * Plays a new instance of an animation without interrupting the ones already running. The
	 * form the compiler's generated animation variables feed: a graph drags the animation in
	 * instead of naming it with a string that goes stale on rename. Accepts this component's
	 * embedded animations and its standalone sequence assets alike.
	 * @param StartAtTime Seconds into the animation to start from; for a reverse play, seconds before its end.
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely, matching UMG; so does any negative number, for callers who spell "forever" as -1.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimation(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationByDisplayName(
		const FString& Name,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	/**
	 * Plays an animation and stops it at EndAtTime rather than at its end.
	 * @param EndAtTime Absolute seconds into the animation where playback ends. Zero or less means the animation's own end.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationTimeRange(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		float EndAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	/**
	 * Plays an animation forward relative to its current state: an instance already running or
	 * paused turns around from where it is, otherwise a new one starts from the beginning. The
	 * "panel slides out on click, slides back on the next click" idiom, exactly as in UMG.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationForward(UMovieSceneSequence* Animation, float PlaybackSpeed = 1.0f, bool bRestoreState = false);

	/** The reverse half of PlayAnimationForward: turns a live instance around, or starts one from the end. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FDreamUIAnimationHandle PlayAnimationReverse(UMovieSceneSequence* Animation, float PlaybackSpeed = 1.0f, bool bRestoreState = false);

	// ------------------------------------------------------------------------------ queue
	//
	// The same operations, deferred to the end of this frame's sequence evaluation. Safe to call
	// from inside an animation's own Started / Finished / event callbacks, where playing or
	// stopping immediately would re-enter the evaluation that is delivering the callback.

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	void QueuePlayAnimation(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	void QueuePlayAnimationTimeRange(
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		float EndAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void QueueStopAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void QueueStopAllAnimations();

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void QueuePauseAnimation(FDreamUIAnimationHandle Handle);

	// --------------------------------------------------------------------- one instance

	/** @return the time the instance was at when paused, in seconds; feed it back to PlayAnimation's StartAtTime to resume from there. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	float PauseAnimation(FDreamUIAnimationHandle Handle);

	/** Continues a paused instance in the direction it was going. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void ResumeAnimation(FDreamUIAnimationHandle Handle);

	/** Ends the instance where it is. Its Finished delegates fire, the same as a natural end. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void StopAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void ReverseAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAnimationPlaying(FDreamUIAnimationHandle Handle) const;

	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAnimationPaused(FDreamUIAnimationHandle Handle) const;

	/** True while the instance runs towards its end; false when reversed. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAnimationPlayingForward(FDreamUIAnimationHandle Handle) const;

	/** Seconds into the animation; zero for a handle that is no longer live. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	float GetAnimationCurrentTime(FDreamUIAnimationHandle Handle) const;

	/** Jumps the instance to a time in seconds without changing whether it is playing. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetAnimationCurrentTime(FDreamUIAnimationHandle Handle, float InTime);

	/** Changes how many times a live instance plays in total. Zero or less loops indefinitely. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetNumLoopsToPlay(FDreamUIAnimationHandle Handle, int32 NumLoopsToPlay);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetPlaybackSpeed(FDreamUIAnimationHandle Handle, float PlaybackSpeed = 1.0f);

	/**
	 * Scales everything this instance writes, so two instances can blend instead of fighting.
	 *
	 * The engine multiplies every blendable value the instance produces by this weight; two instances
	 * of the same animation at 0.5 each land halfway between their two results rather than the later
	 * one simply overwriting the earlier. Needs Dynamic Weighting on (this component's own flag, or
	 * the animation asset's), which is why the flag exists -- without it the engine has no blend
	 * channel to apply a weight through and the value is whatever the last writer said.
	 * @param Weight Usually 0..1. Not clamped: the engine does not clamp it either.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetAnimationWeight(FDreamUIAnimationHandle Handle, float Weight = 1.0f);

	/** Drops a weight set by SetAnimationWeight, returning the instance to writing its values whole. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void ClearAnimationWeight(FDreamUIAnimationHandle Handle);

	/**
	 * Whether this component's animations stop while the game is paused. The runtime form of the
	 * authored flag; it applies to instances started AFTER it, because which clock a player follows
	 * is settled when the player is initialized.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetAffectedByGamePause(bool bValue) { bAffectedByGamePause = bValue; }
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAffectedByGamePause() const { return bAffectedByGamePause; }

	/** Whether this component's animations follow Global Time Dilation; see SetAffectedByGamePause. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetAffectedByTimeDilation(bool bValue) { bAffectedByTimeDilation = bValue; }
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAffectedByTimeDilation() const { return bAffectedByTimeDilation; }

	/** Whether instances of this component's animations can be weighted; see SetAnimationWeight. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetDynamicWeighting(bool bValue) { bDynamicWeighting = bValue; }
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsDynamicWeighting() const { return bDynamicWeighting; }

	// ---------------------------------------------------------------- by animation object

	/** The newest live (playing or paused) instance of an animation, or an invalid handle. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	FDreamUIAnimationHandle FindAnimationInstance(UMovieSceneSequence* Animation) const;

	/** True if any instance of the animation is currently playing. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool HasPlayingAnimation(UMovieSceneSequence* Animation) const;

	/** Stops every live instance of the animation. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void StopAnimationsOf(UMovieSceneSequence* Animation);

	/** Pauses every live instance of the animation. @return the paused time of the newest one, in seconds. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	float PauseAnimationsOf(UMovieSceneSequence* Animation);

	/** True if any instance of any animation on this component is playing. */
	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAnyAnimationPlaying() const;

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void StopAllAnimations();

	/** Applies any evaluation still queued for this component's instances before returning. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void FlushAnimations();

	// -------------------------------------------------------------------------- events

	/** Called when an instance of the animation starts. Unbind with the same delegate. */
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void BindToAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void UnbindFromAnimationStarted(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void UnbindAllFromAnimationStarted(UMovieSceneSequence* Animation);

	/** Called when an instance of the animation ends, naturally or by Stop. Unbind with the same delegate. */
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void BindToAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void UnbindFromAnimationFinished(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate);
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void UnbindAllFromAnimationFinished(UMovieSceneSequence* Animation);

	/** The general form of the two above. */
	UFUNCTION(BlueprintCallable, Category = "DreamUI|Animation")
	void BindToAnimationEvent(UMovieSceneSequence* Animation, FDreamUIAnimationDynamicEvent Delegate, EDreamUIAnimationEvent AnimationEvent);

	/** Any instance of any animation on this component started. */
	UPROPERTY(BlueprintAssignable, Category = "DreamUI|Animation")
	FDreamUIAnimationObjectEvent OnAnimationStarted;
	/** Any instance of any animation on this component ended, naturally or by Stop. */
	UPROPERTY(BlueprintAssignable, Category = "DreamUI|Animation")
	FDreamUIAnimationObjectEvent OnAnimationFinished;

	/** Native per-instance forms of the two above. */
	FDreamUIAnimationInstanceEvent OnInstanceStarted;
	FDreamUIAnimationInstanceEvent OnInstanceFinished;

	/** Fired by a DreamUI Event track key while an animation of this component plays. */
	UPROPERTY(BlueprintAssignable, Category = "DreamUI|Animation")
	FDreamUIAnimEventDelegate OnAnimationEvent;
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void BroadcastAnimationEvent(FName EventName);

	/** The user widget whose contents this component lives in, or null for an authoring tree. */
	UDreamUserWidget* GetOwningUserWidget() const;

	/** True while Player is one of this component's live instances -- what makes a handle valid. */
	bool OwnsLiveInstance(const UDreamWidgetAnimationPlayer* Player) const;

	UDreamWidgetAnimation* AddNewAnimation();
	bool DeleteAnimationByIndex(int32 InIndex);
	UDreamWidgetAnimation* DuplicateAnimationByIndex(int32 InIndex);
	/**
	 * Take a copy of another component's animation into this one, keeping its display name.
	 *
	 * What the .dui compile needs and DuplicateAnimationByIndex is not: the source lives on the tree
	 * the rebuild is about to drop, and the destination is the tree it just built. Before timelines
	 * existed the whole component could be re-homed wholesale (AddComponentByTemplate), which stops
	 * working the moment the FILE also produces animations -- the new tree already has a component,
	 * and replacing it would throw away exactly the ones the text just wrote.
	 *
	 * Refuses a language-owned source: the file rebuilds those, and carrying one would put the
	 * previous compile's copy back on top of the one the file just produced. Null on refusal.
	 */
	UDreamWidgetAnimation* AdoptAnimation(UDreamWidgetAnimation* InSource);

	virtual void Awake()override;
	virtual void OnDestroy() override;
	/** Reads the old whole-struct PlaybackSettings across into the properties above, once. NOT
	 *  editor-only: a cooked build loads the same asset and must end up with the same settings. */
	virtual void PostLoad()override;
#if WITH_EDITOR
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams)override;
	virtual void PreSave(class FObjectPreSaveContext SaveContext)override;

	void FixEditorHelpers();
#endif
protected:

	// ------------------------------------------------------------------------- authored settings
	//
	// Spelled out one property at a time, rather than by splatting FMovieSceneSequencePlaybackSettings
	// across the panel. That struct offered the author a Loop, a Play Rate, a Start Offset, a Random
	// Start Time and a Finish Completion State that PlayAnimation overwrote from its own arguments on
	// every play, next to five Cinematic entries (Disable Movement Input, Hide Player, Hide HUD...)
	// that only ALevelSequenceActor has ever read. Six controls that did nothing and five that mean
	// nothing to a widget. What is left below is what this component actually honours; the old struct
	// is still serialized under it and migrated once, in PostLoad, so no authored value is lost.

	/** Play one of this component's animations as soon as the widget comes alive. */
	UPROPERTY(EditAnywhere, Category = "Playback")
	bool bAutoPlay = false;

	/** Which of this component's animations Auto Play starts. */
	UPROPERTY(EditAnywhere, Category = "Playback", meta = (EditCondition = "bAutoPlay"))
	int32 CurrentSequenceIndex = 0;

	/** Seconds into the animation that the auto-played instance starts from. */
	UPROPERTY(EditAnywhere, Category = "Playback", meta = (EditCondition = "bAutoPlay", Units = s))
	float AutoPlayStartTime = 0.0f;

	/** Total plays for the auto-played instance. Zero or less loops indefinitely. */
	UPROPERTY(EditAnywhere, Category = "Playback", meta = (EditCondition = "bAutoPlay"))
	int32 AutoPlayNumLoopsToPlay = 1;

	/** Play rate for the auto-played instance. */
	UPROPERTY(EditAnywhere, Category = "Playback", meta = (EditCondition = "bAutoPlay", Units = Multiplier))
	float AutoPlayPlaybackSpeed = 1.0f;

	/** Put every property the auto-played instance touched back as it was when the instance ends. */
	UPROPERTY(EditAnywhere, Category = "Playback", meta = (EditCondition = "bAutoPlay"))
	bool bAutoPlayRestoreState = false;

	/** Every animation this component plays: hold the last frame instead of ending. */
	UPROPERTY(EditAnywhere, Category = "Playback")
	bool bPauseAtEnd = false;

	/**
	 * Every animation this component plays: keep going while the game is paused.
	 *
	 * The tween side has had this per tween all along (UDreamTweener::affectByGamePause) and a pause
	 * menu that animates itself away is the ordinary case for it; the Sequencer side simply had no
	 * switch. Off means the engine's own "tick this client while paused" path, which also hands the
	 * player the clock that keeps running while paused.
	 */
	UPROPERTY(EditAnywhere, Category = "Playback")
	bool bAffectedByGamePause = true;

	/**
	 * Every animation this component plays: follow Global Time Dilation.
	 *
	 * The delta the sequence tick manager hands out is already dilated, so this is on by default and a
	 * bullet-time effect slows the HUD with everything else. Off divides the dilation back out, which
	 * is the tween side's affectByTimeDilation -- and what a menu that must stay responsive wants.
	 */
	UPROPERTY(EditAnywhere, Category = "Playback")
	bool bAffectedByTimeDilation = true;

	/**
	 * Every animation this component plays: allow per-instance weights (SetAnimationWeight).
	 *
	 * Off by default because the blend channel it turns on is not free, and an animation that is
	 * never weighted does not need one.
	 */
	UPROPERTY(EditAnywhere, Category = "Playback")
	bool bDynamicWeighting = false;

	/** Every animation this component plays: seconds between evaluations. Zero evaluates every frame. */
	UPROPERTY(EditAnywhere, Category = "Playback", AdvancedDisplay, meta = (Units = s, ClampMin = "0.0"))
	float TickIntervalSeconds = 0.0f;

	/**
	 * The settings struct this component used to expose whole. Kept so assets authored against it
	 * still load, read once by PostLoad into the properties above, and never written again.
	 */
	UPROPERTY()
	FMovieSceneSequencePlaybackSettings PlaybackSettings;

	/** True once PostLoad has read PlaybackSettings across, so a second load does not re-read it. */
	UPROPERTY()
	bool bPlaybackSettingsMigrated = false;

	UPROPERTY(VisibleAnywhere, Instanced, Category= Playback)
		TArray<TObjectPtr<UDreamWidgetAnimation>> SequenceArray;
	/** Standalone animation assets this component can also play, addressed by asset name. */
	UPROPERTY(EditAnywhere, Category = Playback)
		TArray<TObjectPtr<UDreamUISequence>> SequenceAssets;

	UPROPERTY(transient)
		TObjectPtr<UDreamWidgetAnimationPlayer> SequencePlayer;

	/** Players created by PlayAnimation. Kept alive independently for concurrent playback. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidgetAnimationPlayer>> ActiveSequencePlayers;

	/** Delegates bound through BindToAnimationEvent and its two named forms. */
	UPROPERTY(Transient)
	TArray<FDreamUIAnimationEventBinding> AnimationCallbacks;

	FDreamUIAnimationHandle PlayAnimationInternal(
		UMovieSceneSequence* Animation,
		float StartAtTime,
		TOptional<float> EndAtTime,
		int32 NumLoopsToPlay,
		EDreamUIAnimationPlayMode PlayMode,
		float PlaybackSpeed,
		bool bRestoreState);
	/**
	 * The playback settings every play of this component starts from: the component's own authored
	 * flags translated into the struct the engine takes. One place, so the legacy player and every
	 * PlayAnimation agree about pause, dilation, weighting and tick interval.
	 */
	FMovieSceneSequencePlaybackSettings MakePlaybackSettings() const;
	/** Gives Player the clock this component's flags call for; see bAffectedByTimeDilation. */
	void ApplyTimeControl(UDreamWidgetAnimationPlayer* Player) const;
	/** A live instance turned to run InDirection, or a fresh one started that way; PlayAnimationForward / Reverse. */
	FDreamUIAnimationHandle PlayAnimationRelative(UMovieSceneSequence* Animation, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, bool bRestoreState);
	/** Queues Action on the world's sequence tick manager; runs it now if there is no manager to queue on. */
	void QueueAnimationAction(TFunction<void()> Action);

	void HandleActiveSequencePlayerFinished(UDreamWidgetAnimationPlayer* Player);
	bool IsActiveSequencePlayer(const UDreamWidgetAnimationPlayer* Player) const;
	void ReleaseActiveSequencePlayer(UDreamWidgetAnimationPlayer* Player, bool bStopPlayer = true);
	void NotifyInstanceStarted(UDreamWidgetAnimationPlayer* Player);
	void NotifyInstanceFinished(UDreamWidgetAnimationPlayer* Player);
	void ExecuteBoundAnimationEvents(UMovieSceneSequence* Animation, EDreamUIAnimationEvent Event);
	void UnbindAnimationEvent(UMovieSceneSequence* Animation, EDreamUIAnimationEvent Event, const FDreamUIAnimationDynamicEvent* Delegate);
};
