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
