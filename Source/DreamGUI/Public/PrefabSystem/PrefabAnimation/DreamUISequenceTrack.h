// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Evaluation/MovieSceneEvaluationOperand.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "EntitySystem/MovieSceneEntitySystem.h"
#include "EntitySystem/MovieSceneComponentRegistry.h"
#include "EntitySystem/MovieSceneCachedEntityFilterResult.h"
#include "DreamUISequenceTrack.generated.h"

class UDreamUISequence;

namespace UE::MovieScene
{
	/** Payload for the binding override: which inner (asset) binding gets re-rooted. */
	struct FDreamUISequenceComponentData
	{
		FMovieSceneEvaluationOperand InnerOperand;
	};

	struct DREAMGUI_API FDreamUISequenceComponentTypes
	{
		static FDreamUISequenceComponentTypes* Get();
		TComponentTypeID<FDreamUISequenceComponentData> DreamUISequence;
	private:
		FDreamUISequenceComponentTypes();
	};
}

/**
 * Plays a UDreamUISequence asset under a widget (or presenter) binding of an outer sequence.
 * The section overrides the asset's root binding to resolve as this track's own bound object, so
 * one animation asset drives whichever presenter the outer sequence points it at -- the
 * TemplateSequence trick, re-rooted for widget trees.
 */
UCLASS()
class DREAMGUI_API UDreamUISequenceSection
	: public UMovieSceneSubSection
{
	GENERATED_BODY()

public:
	//~ IMovieSceneEntityProvider interface (UMovieSceneSubSection already implements it)
	virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override;
	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity) override;
};

UCLASS()
class DREAMGUI_API UDreamUISequenceTrack
	: public UMovieSceneSubTrack
{
	GENERATED_BODY()

public:
	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif
};

/** Installs/removes the root-binding override when a DreamUI sequence section links or unlinks. */
UCLASS()
class UDreamUISequenceSystem
	: public UMovieSceneEntitySystem
{
	GENERATED_BODY()

public:
	UDreamUISequenceSystem(const FObjectInitializer& ObjInit);

private:
	virtual void OnRun(FSystemTaskPrerequisites& InPrerequisites, FSystemSubsequentTasks& Subsequents) override;

	UE::MovieScene::FCachedEntityFilterResult_Match ApplicableFilter;
};
