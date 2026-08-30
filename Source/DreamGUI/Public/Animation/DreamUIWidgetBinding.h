// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Bindings/MovieSceneCustomBinding.h"
#include "DreamUIWidgetBinding.generated.h"

class AActor;
class UDreamWidget;
class UDreamWidgetPresenterComponentBase;

/**
 * Lets a Level Sequence possess a DreamGUI widget.
 *
 * A widget is a plain UObject the presenter component deserializes at load, transient in the editor
 * world and auto-named -- three reasons the engine's object locators can neither list nor persist
 * one. This binding stores what IS stable instead: the actor carrying the presenter, and the
 * widget's display-name path from the loaded root. Resolution walks presenter -> loaded root ->
 * path, identically in the editor, PIE, and a cooked game, so what the sequencer preview shows is
 * what runs.
 *
 * Create one by possessing the presenter's actor or component and converting the binding to
 * "DreamUI Widget", or by adding a binding from a selected widget inside a designer world.
 * Property tracks then key any Interp property on the widget (transform, color, opacity, width...).
 *
 * A widget loaded after the sequence started stays unresolved until someone pokes the player --
 * the ECS only re-runs resolution on explicit invalidation -- which is what
 * UDreamWidgetPresenterComponentBase::NotifyWidgetLoaded does.
 */
UCLASS(BlueprintType, EditInlineNew, DisplayName = "DreamUI Widget")
class DREAMGUI_API UDreamUIWidgetBinding
	: public UMovieSceneCustomBinding
{
	GENERATED_BODY()

public:
	//~ UMovieSceneCustomBinding interface
	virtual FMovieSceneBindingResolveResult ResolveBinding(const FMovieSceneBindingResolveParams& ResolveParams, int32 BindingIndex, TSharedRef<const UE::MovieScene::FSharedPlaybackState> SharedPlaybackState) const override;
	virtual bool SupportsBindingCreationFromObject(const UObject* SourceObject) const override;
	/** Wider than creation: conversion may start from a presenter component or its actor. */
	bool SupportsSourceObjectForConversion(const UObject* SourceObject) const;
	virtual UMovieSceneCustomBinding* CreateNewCustomBinding(UObject* SourceObject, UMovieScene& OwnerMovieScene) override;
	virtual UClass* GetBoundObjectClass() const override;
	virtual int32 GetCustomBindingPriority() const override { return BaseCustomPriority; }
	virtual FString GetDesiredBindingName() const override;
#if WITH_EDITOR
	virtual FText GetBindingTypePrettyName() const override;
	virtual bool SupportsConversionFromBinding(const FMovieSceneBindingReference& BindingReference, const UObject* SourceObject) const override;
	virtual UMovieSceneCustomBinding* CreateCustomBindingFromBinding(const FMovieSceneBindingReference& BindingReference, UObject* SourceObject, UMovieScene& OwnerMovieScene) override;
#endif

	/** '/'-joined display names from the loaded root down; empty binds the root widget itself. */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	FString WidgetPath;

	/** The actor whose presenter component loads the widget tree this binding resolves inside. */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	TSoftObjectPtr<AActor> PresenterActor;

	/**
	 * Optional: a sub-object of the widget (a behaviour, visual, or layout), as its path name
	 * relative to the widget. Empty binds the widget itself. This is how an exported animation
	 * keeps its component and visual bindings.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamUI")
	FString SubObjectPathRelativeToWidget;

	static FString BuildWidgetPathFromRoot(const UDreamWidget* Root, const UDreamWidget* Widget);
	static UDreamWidget* ResolveWidgetPath(UDreamWidget* Root, const FString& InPath);

#if WITH_EDITOR
	/**
	 * One `(was:)` hop for this binding's path: every segment spelling InOldSegment becomes
	 * InNewSegment. The same one-word contract as the animation reference's rewriter -- no
	 * normalisation, empties kept -- because a repair that reshapes paths it has no business with
	 * is an edit nobody reviews. An empty path binds the root and carries no name to rename.
	 */
	bool RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment);
#endif

private:
	/** The presenter this binding resolves through, honoring PIE world remapping via the context. */
	UDreamWidgetPresenterComponentBase* ResolvePresenter(UObject* Context) const;
	void InitializeFromObject(UObject* SourceObject);
};
