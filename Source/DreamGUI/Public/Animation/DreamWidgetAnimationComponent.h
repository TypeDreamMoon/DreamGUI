// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/ActorComponent.h"
#include "MovieSceneSequencePlayer.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamWidgetAnimationComponent.generated.h"


class UDreamWidgetAnimation;
class UDreamUISequence;
class UDreamWidgetAnimationPlayer;

UENUM(BlueprintType)
enum class EDreamUIAnimationPlayMode : uint8
{
	Forward,
	Reverse,
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIAnimationHandle
{
	GENERATED_BODY()

	UPROPERTY(Transient, BlueprintReadOnly, Category = "DreamUI|Animation")
	TObjectPtr<UDreamWidgetAnimationPlayer> Player = nullptr;

	bool IsValid() const;
};

/**
 * Movie scene animation embedded within DreamUIPrefab.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIAnimEventDelegate, FName, EventName);

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

	/**
	 * Plays a new animation instance without interrupting animations already running on this prefab.
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely, matching UMG.
	 */
	/**
	 * Plays a new animation instance by OBJECT -- the form the compiler's generated animation
	 * variables feed, so a graph drags the animation in instead of naming it with a string that
	 * goes stale on rename. Accepts this component's embedded animations and its standalone
	 * sequence assets alike.
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

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void PauseAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void StopAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void ReverseAnimation(FDreamUIAnimationHandle Handle);

	UFUNCTION(BlueprintPure, Category = "DreamUI|Animation")
	bool IsAnimationPlaying(FDreamUIAnimationHandle Handle) const;

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void StopAllAnimations();

	/** Fired by a DreamUI Event track key while an animation of this component plays. */
	UPROPERTY(BlueprintAssignable, Category = "DreamUI|Animation")
	FDreamUIAnimEventDelegate OnAnimationEvent;
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "DreamUI|Animation")
	void BroadcastAnimationEvent(FName EventName) { OnAnimationEvent.Broadcast(EventName); }

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

	/** Players created by PlayAnimationByDisplayName. Kept alive independently for concurrent playback. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidgetAnimationPlayer>> ActiveSequencePlayers;

	void HandleActiveSequencePlayerFinished(UDreamWidgetAnimationPlayer* Player);
	bool IsActiveSequencePlayer(const UDreamWidgetAnimationPlayer* Player) const;
	void ReleaseActiveSequencePlayer(UDreamWidgetAnimationPlayer* Player, bool bStopPlayer = true);
};
