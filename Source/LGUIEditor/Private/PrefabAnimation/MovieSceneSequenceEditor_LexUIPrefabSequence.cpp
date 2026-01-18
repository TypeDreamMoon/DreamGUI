// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "MovieSceneSequenceEditor_LexUIPrefabSequence.h"
#include "ISequencerModule.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"

#define LOCTEXT_NAMESPACE "MovieSceneSequenceEditor_LGUIPrefabSequence"

UBlueprint* FMovieSceneSequenceEditor_LexUIPrefabSequence::GetBlueprintForSequence(UMovieSceneSequence* InSequence) const
{
	auto PrefabSequence = CastChecked<ULexUIPrefabSequence>(InSequence);
	auto Component = PrefabSequence->GetTypedOuter<ULexUIPrefabSequenceComponent>();
	return Component->GetSequenceBlueprint();
}

UBlueprint* FMovieSceneSequenceEditor_LexUIPrefabSequence::CreateBlueprintForSequence(UMovieSceneSequence* InSequence) const
{
	auto PrefabSequence = CastChecked<ULexUIPrefabSequence>(InSequence);
	auto Component = PrefabSequence->GetTypedOuter<ULexUIPrefabSequenceComponent>();
	check(!Component->GetSequenceBlueprint());
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
