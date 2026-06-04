// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequencePlayer.h"

#include "Core/Components/LexWidget.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"


UObject* ULexUIPrefabSequencePlayer::GetPlaybackContext() const
{
	if (auto PrefabSequence = CastChecked<ULexUIPrefabSequence>(Sequence))
	{
		auto Component = PrefabSequence->GetTypedOuter<ULexUIPrefabSequenceComponent>();
		return Component->GetWidget();
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