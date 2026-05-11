// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "LexUIPrefabSequenceObjectReference.h"
#include "LexUIPrefabSequence.generated.h"

/**
 * Movie scene animation embedded within LGUI prefab.
 */
UCLASS(BlueprintType, DefaultToInstanced)
class LGUI_API ULexUIPrefabSequence
	: public UMovieSceneSequence
{
public:
	GENERATED_BODY()

	ULexUIPrefabSequence(const FObjectInitializer& ObjectInitializer);

	//~ UMovieSceneSequence interface
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	virtual void LocateBoundObjects(const FGuid& ObjectId, UObject* Context, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual UMovieScene* GetMovieScene() const override;
	virtual UObject* GetParentObject(UObject* Object) const override;
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override {}
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override {}
	virtual UObject* CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID) override;

#if WITH_EDITOR
	virtual FText GetDisplayName() const override { return FText::FromString(DisplayNameString); }
	virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
#endif

	bool IsEditable() const;
	
	void SetDisplayNameString(const FString& Value) { DisplayNameString = Value; }
	const FString& GetDisplayNameString()const { return DisplayNameString; }
private:

	//~ UObject interface
	virtual void PostInitProperties() override;

private:
	
	/** Pointer to the movie scene that controls this animation. */
	UPROPERTY(Instanced)
	TObjectPtr<UMovieScene> MovieScene;

	/** Collection of object references. */
	UPROPERTY()
	FLexUIPrefabSequenceObjectReferenceMap ObjectReferences;

	UPROPERTY()
	FString DisplayNameString;

#if WITH_EDITOR
public:

	/** Event that is fired to initialize default state for a sequence */
	DECLARE_EVENT_OneParam(ULexUIPrefabSequence, FOnInitialize, ULexUIPrefabSequence*)
	static FOnInitialize& OnInitializeSequence() { return OnInitializeSequenceEvent; }

	bool IsObjectReferencesGood(ULexWidget* InContextActor)const;
	bool IsEditorHelpersGood(ULexWidget* InContextActor)const;
	void FixObjectReferences(ULexWidget* InContextActor);
	void FixEditorHelpers(ULexWidget* InContextActor);
private:
	static FOnInitialize OnInitializeSequenceEvent;
#endif

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	bool bHasBeenInitialized;
#endif
};