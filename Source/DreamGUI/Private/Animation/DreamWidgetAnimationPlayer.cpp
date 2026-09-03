// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationPlayer.h"

#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "EntitySystem/MovieSceneEntitySystemRunner.h"


UObject* UDreamWidgetAnimationPlayer::GetPlaybackContext() const
{
	// The OUTER, not the sequence: a standalone UDreamUISequence asset is outered to its package
	// and a cast of the sequence to the embedded type would assert on it, while the component
	// that created this player is the same for either kind and is what the bindings resolve from.
	if (const UDreamWidgetAnimationComponent* Component = GetTypedOuter<UDreamWidgetAnimationComponent>())
	{
		return Component->GetWidget();
	}
	return nullptr;
}

TArray<UObject*> UDreamWidgetAnimationPlayer::GetEventContexts() const
{
	TArray<UObject*> Contexts;
	if (UObject* PlaybackContext = GetPlaybackContext())
	{
		Contexts.Add(PlaybackContext);
	}
	return Contexts;
}

void UDreamWidgetAnimationPlayer::SetLoopCount(int32 InLoopCount)
{
	PlaybackSettings.LoopCount.Value = InLoopCount;
}

void UDreamWidgetAnimationPlayer::FlushQueuedEvaluation()
{
	TSharedPtr<FMovieSceneEntitySystemRunner> Runner = RootTemplateInstance.GetRunner();
	if (Runner.IsValid() && Runner->HasQueuedUpdates())
	{
		Runner->Flush();
	}
}
