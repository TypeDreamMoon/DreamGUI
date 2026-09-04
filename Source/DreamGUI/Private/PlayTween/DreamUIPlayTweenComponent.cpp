// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "PlayTween/DreamUIPlayTween.h"

void UDreamUIPlayTweenComponent::Awake()
{
	if (bPlayOnStart)
	{
		if (IsValid(PlayTween))
		{
			PlayTween->Start();
		}
	}
}
void UDreamUIPlayTweenComponent::OnDestroy()
{
	// The tween lives in the game instance's manager, not in this component, and with the default
	// LoopType it never completes on its own -- so a widget torn down mid-animation left a tween
	// running forever, holding the play tween (and everything its callbacks touch) alive with it.
	Stop();
	Super::OnDestroy();
}
void UDreamUIPlayTweenComponent::Play()
{
	if (IsValid(PlayTween))
	{
		PlayTween->Start();
	}
}
void UDreamUIPlayTweenComponent::Stop()
{
	if (IsValid(PlayTween))
	{
		PlayTween->Stop();
	}
}
