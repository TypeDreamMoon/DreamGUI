// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneSequenceEditor.h"

struct FMovieSceneSequenceEditor_LexUIPrefabSequence : FMovieSceneSequenceEditor
{
	virtual bool CanCreateEvents(UMovieSceneSequence* InSequence) const override
	{
		return true;
	}
	virtual UBlueprint* GetBlueprintForSequence(UMovieSceneSequence* InSequence) const override;
	virtual UBlueprint* CreateBlueprintForSequence(UMovieSceneSequence* InSequence) const override;
};
