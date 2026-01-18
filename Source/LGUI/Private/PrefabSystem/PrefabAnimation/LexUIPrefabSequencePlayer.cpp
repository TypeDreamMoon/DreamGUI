// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequencePlayer.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"


UObject* ULexUIPrefabSequencePlayer::GetPlaybackContext() const
{
	auto PrefabSequence = CastChecked<ULexUIPrefabSequence>(Sequence);
	if (PrefabSequence)
	{
		auto Component = PrefabSequence->GetTypedOuter<ULexUIPrefabSequenceComponent>();
		return Component->GetOwner();
	}

	return nullptr;
}

TArray<UObject*> ULexUIPrefabSequencePlayer::GetEventContexts() const
{
	TArray<UObject*> Contexts;
	if (UObject* PlaybackContext = GetPlaybackContext())
	{
		Contexts.Add(PlaybackContext);
	}
	return Contexts;
}