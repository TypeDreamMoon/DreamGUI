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
};
