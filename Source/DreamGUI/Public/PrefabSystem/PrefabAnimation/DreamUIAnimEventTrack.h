// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneNameableTrack.h"
#include "Sections/MovieSceneHookSection.h"
#include "Channels/MovieSceneStringChannel.h"
#include "DreamUIAnimEventTrack.generated.h"

/**
 * One named trigger key on the animation timeline. The name is broadcast through the hosting
 * UDreamUIPrefabSequenceComponent's OnAnimationEvent delegate when playback crosses the key.
 *
 * This is DreamUI's stand-in for the engine's event track. The engine track needs a director
 * blueprint instance and refuses to fire in an editor world unless the endpoint is CallInEditor;
 * a plain FName broadcast has neither restriction, survives renames and cooking, and any listener
 * (the companion behaviour blueprint above all) can bind to it.
 */
UCLASS()
class DREAMGUI_API UDreamUIAnimEventSection
	: public UMovieSceneHookSection
{
	GENERATED_BODY()

public:
	UDreamUIAnimEventSection(const FObjectInitializer& ObjInit);

	virtual TArrayView<const FFrameNumber> GetTriggerTimes() const override { return EventChannel.GetData().GetTimes(); }
	virtual void Trigger(TSharedRef<UE::MovieScene::FSharedPlaybackState> SharedPlaybackState, const UE::MovieScene::FEvaluationHookParams& Params) const override;
	virtual EMovieSceneChannelProxyType CacheChannelProxy() override;

	/** Each key's value is the event name to broadcast. */
	UPROPERTY()
	FMovieSceneStringChannel EventChannel;
};

/** The timeline row holding UDreamUIAnimEventSection. Lives at the root of the sequence, unbound. */
UCLASS()
class DREAMGUI_API UDreamUIAnimEventTrack
	: public UMovieSceneNameableTrack
{
	GENERATED_BODY()

public:
	UDreamUIAnimEventTrack(const FObjectInitializer& ObjInit);

	//~ UMovieSceneTrack interface
	virtual void AddSection(UMovieSceneSection& Section) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	virtual const TArray<UMovieSceneSection*>& GetAllSections() const override;
	virtual bool HasSection(const UMovieSceneSection& Section) const override;
	virtual bool IsEmpty() const override;
	virtual void RemoveSection(UMovieSceneSection& Section) override;
	virtual void RemoveSectionAt(int32 SectionIndex) override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

private:
	UPROPERTY()
	TArray<TObjectPtr<UMovieSceneSection>> Sections;
};
