// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequence.h"
#include "MovieSceneBindingReferences.h"
#include "DreamUISequence.generated.h"

class UDreamWidget;

/**
 * A DreamGUI animation as a standalone asset.
 *
 * The embedded UDreamUIPrefabSequence lives inside one prefab's sequence component and can be
 * played only there. This asset form exists so a Level Sequence can play the same animation as an
 * ordinary subsequence -- scrubbing, time-warping and blending included -- and so one animation can
 * be reused across prefabs.
 *
 * Bindings are FMovieSceneBindingReferences carrying UDreamUIWidgetBinding entries: a root binding
 * (empty path) plus per-widget display-name paths. Child possessables are parented to the root and
 * parent contexts are significant, so resolving the root against a different presenter re-roots the
 * whole tree -- which is exactly what UDreamUISequenceSection's binding override does.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUISequence
	: public UMovieSceneSequence
{
	GENERATED_BODY()

public:
	UDreamUISequence(const FObjectInitializer& ObjInit);

	//~ UMovieSceneSequence interface
	virtual UMovieScene* GetMovieScene() const override { return MovieScene; }
	virtual const FMovieSceneBindingReferences* GetBindingReferences() const override { return &BindingReferences; }
	virtual bool CanPossessObject(UObject& Object, UObject* InPlaybackContext) const override;
	virtual void BindPossessableObject(const FGuid& ObjectId, UObject& PossessedObject, UObject* Context) override;
	virtual void UnbindPossessableObjects(const FGuid& ObjectId) override;
	virtual void UnbindObjects(const FGuid& ObjectId, const TArray<UObject*>& InObjects, UObject* Context) override;
	virtual void UnbindInvalidObjects(const FGuid& ObjectId, UObject* Context) override;
	virtual UObject* GetParentObject(UObject* Object) const override;
#if WITH_EDITOR
	virtual FText GetDisplayName() const override;
	virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
#endif

	/**
	 * The prefab this animation is authored against. The standalone editor instantiates it as a
	 * preview tree so the bindings resolve and scrubbing shows the real thing; playback through a
	 * component or a subsequence override ignores it entirely.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	TSoftObjectPtr<class UDreamUIPrefab> PreviewPrefab;

	/** The standalone editor's live preview tree, if one is up. Not serialized, not owned here. */
	UDreamWidget* GetPreviewRoot() const { return PreviewRootWidget.Get(); }
	void SetPreviewRoot(UDreamWidget* InRoot) { PreviewRootWidget = InRoot; }

	/** The root binding (empty widget path), created on demand; the subsequence override retargets it. */
	FGuid EnsureRootBinding();
	FGuid GetRootBindingGuid() const { return RootBindingGuid; }
#if WITH_EDITOR
	/** The export path builds bindings by hand and then records which one is the root. */
	void SetRootBindingGuidForExport(const FGuid& InGuid) { RootBindingGuid = InGuid; }
#endif

	/** Adds a possessable + widget binding for the given path ('' = root). Editor-time authoring helper. */
	FGuid AddWidgetBinding(const FString& InWidgetPath, const FString& InDisplayName);

	UPROPERTY()
	TObjectPtr<UMovieScene> MovieScene;

	UPROPERTY()
	FMovieSceneBindingReferences BindingReferences;

private:
	UPROPERTY()
	FGuid RootBindingGuid;

	TWeakObjectPtr<UDreamWidget> PreviewRootWidget;
};
