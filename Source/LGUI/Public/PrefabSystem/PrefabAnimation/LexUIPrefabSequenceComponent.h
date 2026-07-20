// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "MovieSceneSequencePlayer.h"
#include "Core/LexUIBehaviour.h"
#include "LexUIPrefabSequenceComponent.generated.h"


class ULexUIPrefabSequence;
class ULexUIPrefabSequencePlayer;

UENUM(BlueprintType)
enum class ELexUIAnimationPlayMode : uint8
{
	Forward,
	Reverse,
};

USTRUCT(BlueprintType)
struct LGUI_API FLexUIAnimationHandle
{
	GENERATED_BODY()

	UPROPERTY(Transient, BlueprintReadOnly, Category = "LexUI|Animation")
	TObjectPtr<ULexUIPrefabSequencePlayer> Player = nullptr;

	bool IsValid() const;
};

/**
 * Movie scene animation embedded within LexUIPrefab.
 */
UCLASS(Blueprintable, ClassGroup=LGUI, meta=(BlueprintSpawnableComponent), DisplayName="LexUI Prefab Sequence Component")
class LGUI_API ULexUIPrefabSequenceComponent
	: public ULexUIBehaviour
{
public:
	GENERATED_BODY()

	ULexUIPrefabSequenceComponent();

	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexUIPrefabSequence* GetSequenceByDisplayName(const FString& InName) const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexUIPrefabSequence* GetSequenceByIndex(int32 InIndex) const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		const TArray<ULexUIPrefabSequence*>& GetSequenceArray() const { return SequenceArray; }
	/** Init SequencePlayer with current sequence. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void InitSequencePlayer();
	/** Find animation in SequenceArray by Index, then set it to SequencePlayer. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSequenceByIndex(int32 InIndex);
	/** Find animation in SequenceArray by Name, then set it to SequencePlayer */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSequenceByDisplayName(const FString& InName);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetCurrentSequenceIndex()const { return CurrentSequenceIndex; }

	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexUIPrefabSequence* GetCurrentSequence() const { return GetSequenceByIndex(CurrentSequenceIndex); }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexUIPrefabSequencePlayer* GetSequencePlayer() const { return SequencePlayer; }

	/**
	 * Plays a new animation instance without interrupting animations already running on this prefab.
	 * @param NumLoopsToPlay Total number of times to play the animation. Zero loops indefinitely, matching UMG.
	 */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "LexUI|Animation", meta = (AdvancedDisplay = "bRestoreState"))
	FLexUIAnimationHandle PlayAnimationByDisplayName(
		const FString& Name,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		ELexUIAnimationPlayMode PlayMode = ELexUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f,
		bool bRestoreState = false);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "LexUI|Animation")
	void PauseAnimation(FLexUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "LexUI|Animation")
	void StopAnimation(FLexUIAnimationHandle Handle);

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "LexUI|Animation")
	void ReverseAnimation(FLexUIAnimationHandle Handle);

	UFUNCTION(BlueprintPure, Category = "LexUI|Animation")
	bool IsAnimationPlaying(FLexUIAnimationHandle Handle) const;

	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category = "LexUI|Animation")
	void StopAllAnimations();

	ULexUIPrefabSequence* AddNewAnimation();
	bool DeleteAnimationByIndex(int32 InIndex);
	ULexUIPrefabSequence* DuplicateAnimationByIndex(int32 InIndex);
	
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
		TArray<TObjectPtr<ULexUIPrefabSequence>> SequenceArray;
	UPROPERTY(EditAnywhere, Category = Playback)
		int32 CurrentSequenceIndex = 0;

	UPROPERTY(transient)
		TObjectPtr<ULexUIPrefabSequencePlayer> SequencePlayer;

	/** Players created by PlayAnimationByDisplayName. Kept alive independently for concurrent playback. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULexUIPrefabSequencePlayer>> ActiveSequencePlayers;

	void HandleActiveSequencePlayerFinished(ULexUIPrefabSequencePlayer* Player);
	bool IsActiveSequencePlayer(const ULexUIPrefabSequencePlayer* Player) const;
	void ReleaseActiveSequencePlayer(ULexUIPrefabSequencePlayer* Player, bool bStopPlayer = true);
};
