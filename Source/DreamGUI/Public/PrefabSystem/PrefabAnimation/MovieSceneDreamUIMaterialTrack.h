// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "Sections/MovieSceneParameterSection.h"
#include "Tracks/MovieSceneMaterialTrack.h"
#include "MovieSceneDreamUIMaterialTrack.generated.h"

/**
 * A material track which is specialized for DreamGUI's DreamVisualBatchMesh's CustomUIMaterial.
 */
UCLASS(MinimalAPI)
class UMovieSceneDreamUIMaterialTrack
	: public UMovieSceneMaterialTrack
	, public IMovieSceneEntityProvider
	, public IMovieSceneParameterSectionExtender
{
	GENERATED_BODY()

public:
	UMovieSceneDreamUIMaterialTrack(const FObjectInitializer& ObjectInitializer);

	// UMovieSceneTrack interface
	virtual FName GetTrackName() const override;

	virtual bool SupportsType(TSubclassOf<UMovieSceneSection> SectionClass) const override;
	virtual UMovieSceneSection* CreateNewSection() override;
	
	/*~ IMovieSceneEntityProvider */
	virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;
	virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override;

	/*~ IMovieSceneParameterSectionExtender */
	virtual void ExtendEntityImpl(UMovieSceneParameterSection* Section, UMovieSceneEntitySystemLinker* EntityLinker, const UE::MovieScene::FEntityImportParams& Params, UE::MovieScene::FImportedEntity* OutImportedEntity) override;

#if WITH_EDITORONLY_DATA
	virtual FText GetDefaultDisplayName() const override;
#endif

public:

	/** Gets name of the material property. */
	const FName& GetPropertyName() const { return PropertyName; }

	/** Sets the name of the material property. */
	DREAMGUI_API void SetPropertyName( FName InPropertyName);

private:

	/** The name of the material property. */
	UPROPERTY()
	FName PropertyName;
	/** The name of this track, generated from the property name path. */
	UPROPERTY()
	FName TrackName;
};
