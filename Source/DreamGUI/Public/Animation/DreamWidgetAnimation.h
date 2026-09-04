// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "MovieSceneSequence.h"
#include "MovieScene.h"
#include "DreamWidgetAnimationObjectReference.h"
#include "DreamWidgetAnimation.generated.h"

/**
 * Movie scene animation embedded within DreamGUI prefab.
 */
UCLASS(BlueprintType, DefaultToInstanced, DisplayName="DreamUI Widget Animation")
class DREAMGUI_API UDreamWidgetAnimation
	: public UMovieSceneSequence
{
public:
	GENERATED_BODY()

	UDreamWidgetAnimation(const FObjectInitializer& ObjectInitializer);

	//~ UMovieSceneSequence interface
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	virtual void LocateBoundObjects(const FGuid& ObjectId, const UE::UniversalObjectLocator::FResolveParams& ResolveParams, TSharedPtr<const FSharedPlaybackState> SharedPlaybackState, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const override;
	virtual UMovieScene* GetMovieScene() const override;
	virtual UObject* GetParentObject(UObject* Object) const override;
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override {}
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override {}
	virtual UObject* CreateDirectorInstance(TSharedRef<const FSharedPlaybackState> SharedPlaybackState, FMovieSceneSequenceID SequenceID) override;

#if WITH_EDITOR
	virtual FText GetDisplayName() const override { return FText::FromString(DisplayNameString); }
	virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
	virtual bool IsFilterSupportedImpl(const FString& InFilterName) const override;
#endif

	bool IsEditable() const;
	
	void SetDisplayNameString(const FString& Value) { DisplayNameString = Value; }
	const FString& GetDisplayNameString()const { return DisplayNameString; }
private:

	//~ UObject interface
	virtual void PostInitProperties() override;

	/**
	 * Point every binding's cached widget pointer at THIS animation's own tree, once.
	 *
	 * The other half of the fix PostInitProperties starts: there the stale pointers are dropped,
	 * because the tree is still being instanced and there is nothing yet to walk; here they are
	 * refilled, at the first resolve that arrives with a live hierarchy. Once, because after that the
	 * pointers name this tree and re-walking every path per resolve would be a cost for nothing.
	 */
	void RebindObjectReferencesOnce(UDreamWidget* InContextWidget) const;
	/**
	 * Latch for the above. Deliberately NOT a UPROPERTY: an instanced copy has to start life
	 * un-rebound, and a reflected field would be copied from a template that had already done it.
	 */
	mutable bool bObjectReferencesRebound = false;

private:
	
	/** Pointer to the movie scene that controls this animation. */
	UPROPERTY(Instanced)
	TObjectPtr<UMovieScene> MovieScene;

	/** Collection of object references. */
	UPROPERTY()
	FDreamWidgetAnimationObjectReferenceMap ObjectReferences;

	UPROPERTY()
	FString DisplayNameString;

#if WITH_EDITOR
public:

	/** Event that is fired to initialize default state for a sequence */
	DECLARE_EVENT_OneParam(UDreamWidgetAnimation, FOnInitialize, UDreamWidgetAnimation*)
	static FOnInitialize& OnInitializeSequence() { return OnInitializeSequenceEvent; }

	bool IsObjectReferencesGood(UDreamWidget* InContextWidget)const;
	void GetInvalidObjectBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const;
	/** Bindings whose recorded path this context cannot walk; see the map's own note on the distinction. */
	void GetUnresolvableBindingPaths(UDreamWidget* InContextWidget, TArray<TPair<FGuid, FString>>& OutBindings) const;
	/**
	 * Carry every binding path through a widget rename, and the track labels with them.
	 *
	 * The paths are the binding; the possessable's name is only what the sequencer prints on the
	 * track. Both are the widget's display name and both go stale together, but only one of them
	 * breaks playback -- so the label is renamed on a strict equality check and never guessed at,
	 * because a possessable whose name was already something else was named that on purpose.
	 *
	 * @return how many binding paths changed. Zero means this animation had nothing to migrate,
	 *         which is the ordinary answer for a `(was: ...)` clause the author has simply not deleted.
	 */
	int32 RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment);
	bool HasObjectBindingCountMismatch() const;
	bool IsEditorHelpersGood(UDreamWidget* InContextWidget)const;
	void FixObjectReferences(UDreamWidget* InContextWidget);
	void FixEditorHelpers(UDreamWidget* InContextWidget);
private:
	static FOnInitialize OnInitializeSequenceEvent;
#endif

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY()
	bool bHasBeenInitialized;
#endif
};
