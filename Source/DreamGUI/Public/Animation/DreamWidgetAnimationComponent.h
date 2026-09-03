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
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely, matching UMG.
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

	/** Changes how many times a live instance plays in total. Zero loops indefinitely. */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetNumLoopsToPlay(FDreamUIAnimationHandle Handle, int32 NumLoopsToPlay);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void SetPlaybackSpeed(FDreamUIAnimationHandle Handle, float PlaybackSpeed = 1.0f);

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

	virtual void Awake()override;
	virtual void OnDestroy() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams)override;
	virtual void PreSave(class FObjectPreSaveContext SaveContext)override;
	virtual void PostDuplicate(bool bDuplicateForPIE)override;
	virtual void PostInitProperties()override;
	virtual void PostLoad()override;

	void FixEditorHelpers();
#endif
protected:

	UPROPERTY(EditAnywhere, Category="Playback", meta=(ShowOnlyInnerProperties))
	FMovieSceneSequencePlaybackSettings PlaybackSettings;

	UPROPERTY(VisibleAnywhere, Instanced, Category= Playback)
		TArray<TObjectPtr<UDreamWidgetAnimation>> SequenceArray;
	/** Standalone animation assets this component can also play, addressed by asset name. */
	UPROPERTY(EditAnywhere, Category = Playback)
		TArray<TObjectPtr<UDreamUISequence>> SequenceAssets;
	UPROPERTY(EditAnywhere, Category = Playback)
		int32 CurrentSequenceIndex = 0;

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
