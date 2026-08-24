// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Components/ActorComponent.h"
#include "MovieSceneSequencePlayer.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIPrefabSequenceComponent.generated.h"


class UDreamUIPrefabSequence;
class UDreamUISequence;
class UDreamUIPrefabSequencePlayer;

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
	TObjectPtr<UDreamUIPrefabSequencePlayer> Player = nullptr;

	bool IsValid() const;
};

/**
 * Movie scene animation embedded within DreamUIPrefab.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIAnimEventDelegate, FName, EventName);

UCLASS(Blueprintable, ClassGroup=DreamGUI, meta=(BlueprintSpawnableComponent), DisplayName="DreamUI Prefab Sequence Component")
class DREAMGUI_API UDreamUIPrefabSequenceComponent
	: public UDreamUIBehaviour
{
public:
	GENERATED_BODY()

	UDreamUIPrefabSequenceComponent();

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamUIPrefabSequence* GetSequenceByDisplayName(const FString& InName) const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamUIPrefabSequence* GetSequenceByIndex(int32 InIndex) const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		const TArray<UDreamUIPrefabSequence*>& GetSequenceArray() const { return SequenceArray; }
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
		UDreamUIPrefabSequence* GetCurrentSequence() const { return GetSequenceByIndex(CurrentSequenceIndex); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamUIPrefabSequencePlayer* GetSequencePlayer() const { return SequencePlayer; }

	/**
	 * Plays a new animation instance without interrupting animations already running on this prefab.
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely, matching UMG.
	 */
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

	UDreamUIPrefabSequence* AddNewAnimation();
	bool DeleteAnimationByIndex(int32 InIndex);
	UDreamUIPrefabSequence* DuplicateAnimationByIndex(int32 InIndex);
	
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
		TArray<TObjectPtr<UDreamUIPrefabSequence>> SequenceArray;
	/** Standalone animation assets this component can also play, addressed by asset name. */
	UPROPERTY(EditAnywhere, Category = Playback)
		TArray<TObjectPtr<UDreamUISequence>> SequenceAssets;
	UPROPERTY(EditAnywhere, Category = Playback)
		int32 CurrentSequenceIndex = 0;

	UPROPERTY(transient)
		TObjectPtr<UDreamUIPrefabSequencePlayer> SequencePlayer;

	/** Players created by PlayAnimationByDisplayName. Kept alive independently for concurrent playback. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamUIPrefabSequencePlayer>> ActiveSequencePlayers;

	void HandleActiveSequencePlayerFinished(UDreamUIPrefabSequencePlayer* Player);
	bool IsActiveSequencePlayer(const UDreamUIPrefabSequencePlayer* Player) const;
	void ReleaseActiveSequencePlayer(UDreamUIPrefabSequencePlayer* Player, bool bStopPlayer = true);
};
