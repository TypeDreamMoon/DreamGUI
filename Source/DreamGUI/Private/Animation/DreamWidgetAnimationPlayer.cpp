// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Animation/DreamWidgetAnimationPlayer.h"

#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimation.h"


UObject* UDreamWidgetAnimationPlayer::GetPlaybackContext() const
{
	if (auto WidgetAnimation = CastChecked<UDreamWidgetAnimation>(Sequence))
	{
		auto Component = WidgetAnimation->GetTypedOuter<UDreamWidgetAnimationComponent>();
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