// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "PrefabSystem/ILexUIPrefabInterface.h"
#include "MovieSceneSequencePlayer.h"
#include "LexUIComponentReference.h"
#include "Core/LexUIBehaviour.h"
#include "LexUIPrefabSequenceComponent.generated.h"


class ULexUIPrefabSequence;
class ULexUIPrefabSequencePlayer;

/**
 * Movie scene animation embedded within LexUIPrefab.
 */
UCLASS(Blueprintable, ClassGroup=LGUI, hidecategories=(Collision, Cooking, Activation), meta=(BlueprintSpawnableComponent))
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

	ULexUIPrefabSequence* AddNewAnimation();
	bool DeleteAnimationByIndex(int32 InIndex);
	ULexUIPrefabSequence* DuplicateAnimationByIndex(int32 InIndex);
	
	virtual void BeginPlay()override;
	virtual void EndPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreDuplicate(FObjectDuplicationParameters& DupParams)override;
	virtual void PreSave(class FObjectPreSaveContext SaveContext)override;
	virtual void PostDuplicate(bool bDuplicateForPIE)override;
	virtual void PostInitProperties()override;
	virtual void PostLoad()override;

	void FixEditorHelpers();
	UBlueprint* GetSequenceBlueprint()const;
#endif
	UObject* GetSequenceBlueprintInstance()const { return SequenceEventHandler.GetComponent(); }
protected:

	UPROPERTY(EditAnywhere, Category="Playback", meta=(ShowOnlyInnerProperties))
	FMovieSceneSequencePlaybackSettings PlaybackSettings;

	UPROPERTY(VisibleAnywhere, Instanced, Category= Playback)
		TArray<TObjectPtr<ULexUIPrefabSequence>> SequenceArray;
	UPROPERTY(EditAnywhere, Category = Playback)
		int32 CurrentSequenceIndex = 0;
	/**
	 * Use a Blueprint component to handle callback for event track.
	 * Not working: Add event in prefab the event can work no problem, but if close editor and open again, the event not fire at all.
	 */
	UPROPERTY(/*EditAnywhere, Category = Playback*/)
		FLexUIComponentReference SequenceEventHandler;

	UPROPERTY(transient)
		TObjectPtr<ULexUIPrefabSequencePlayer> SequencePlayer;
};
