// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequence.h"
#include "MovieSceneBindingReferences.h"
#include "DreamUISequence.generated.h"

class UDreamWidget;
class UDreamUserWidget;

/**
 * A DreamGUI animation as a standalone asset.
 *
 * The embedded UDreamUIPrefabSequence lives inside one widget's sequence component and can be
 * played only there. This asset form exists so a Level Sequence can play the same animation as an
 * ordinary subsequence -- scrubbing, time-warping and blending included -- and so one animation can
 * be reused across widget classes.
 *
 * Bindings are FMovieSceneBindingReferences carrying UDreamUIWidgetBinding entries: a root binding
 * (empty path) plus per-widget display-name paths. Child possessables are parented to the root and
 * parent contexts are significant, so resolving the root against a different presenter re-roots the
 * whole tree -- which is exactly what UDreamUISequenceSection's binding override does.
 *
 * Bindings resolve by NAME against whatever tree they are given, so one animation can drive the
 * preview here, a live instance at runtime, and any other instance of the class -- the same thing
 * that made the embedded animations work on a class model at all.
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
	//without this the sequencer's create-binding path never consults custom binding types at all
	virtual bool AllowsCustomBindings() const override { return true; }
#if WITH_EDITOR
	virtual FText GetDisplayName() const override;
	virtual ETrackSupport IsTrackSupportedImpl(TSubclassOf<class UMovieSceneTrack> InTrackClass) const override;
#endif

	/**
	 * The widget class this animation is authored against. The standalone editor instantiates it as a
	 * preview tree so the bindings resolve and scrubbing shows the real thing; playback through a
	 * component or a subsequence override ignores it entirely.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	TSoftClassPtr<UDreamUserWidget> PreviewWidgetClass;

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
