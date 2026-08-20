// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequencePlayer.h"

#include "Core/Components/DreamWidget.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"


UObject* UDreamUIPrefabSequencePlayer::GetPlaybackContext() const
{
	if (auto PrefabSequence = CastChecked<UDreamUIPrefabSequence>(Sequence))
	{
		auto Component = PrefabSequence->GetTypedOuter<UDreamUIPrefabSequenceComponent>();
		return Component->GetWidget();
	}

	return nullptr;
}

TArray<UObject*> UDreamUIPrefabSequencePlayer::GetEventContexts() const
{
	TArray<UObject*> Contexts;
	if (UObject* PlaybackContext = GetPlaybackContext())
	{
		Contexts.Add(PlaybackContext);
	}
	return Contexts;
}