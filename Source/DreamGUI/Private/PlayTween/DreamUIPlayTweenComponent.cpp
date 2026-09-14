// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTweenComponent.h"
#include "PlayTween/DreamUIPlayTween.h"

void UDreamUIPlayTweenComponent::Awake()
{
	// UDreamUIBehaviour::Awake is what raises the blueprint's Event Awake, so a blueprint subclass of
	// this component never got one. OnDestroy below already calls its Super for the same reason; this
	// was simply missed. It comes first, mirroring OnDestroy's order, so the blueprint's own setup has
	// run before an auto-started tween begins writing over the values it just set.
	Super::Awake();
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
