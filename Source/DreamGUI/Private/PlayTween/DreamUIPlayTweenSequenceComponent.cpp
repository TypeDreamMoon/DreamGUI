// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTweenSequenceComponent.h"
#include "PlayTween/DreamUIPlayTween.h"
#include "DreamTweenManager.h"

void UDreamUIPlayTweenSequenceComponent::Awake()
{
	if (bPlayOnStart)
	{
		Play();
	}
}

void UDreamUIPlayTweenSequenceComponent::SubscribeToTween(UDreamUIPlayTween* InPlayTween)
{
	if (bPlayNextWhenCycleComplete)
	{
		OnCompleteDelegateHandle = InPlayTween->OnCycleCompleteCPP.AddWeakLambda(this, [this](int count) {
			OnTweenComplete();
			});
	}
	else
	{
		OnCompleteDelegateHandle = InPlayTween->OnCompleteCPP.AddUObject(this, &UDreamUIPlayTweenSequenceComponent::OnTweenComplete);
	}
}

void UDreamUIPlayTweenSequenceComponent::UnsubscribeFromTween(UDreamUIPlayTween* InPlayTween)
{
	// The mirror of SubscribeToTween, and it has to read the same flag: a handle taken out of one
	// multicast cannot be removed from the other, and the removal that misses is silent.
	if (bPlayNextWhenCycleComplete)
	{
		InPlayTween->OnCycleCompleteCPP.Remove(OnCompleteDelegateHandle);
	}
	else
	{
		InPlayTween->OnCompleteCPP.Remove(OnCompleteDelegateHandle);
	}
	OnCompleteDelegateHandle.Reset();
}

int32 UDreamUIPlayTweenSequenceComponent::FindNextPlayableTweenIndex(int32 InStartIndex)const
{
	for (int32 Index = FMath::Max(InStartIndex, 0); Index < PlayTweenArray.Num(); Index++)
	{
		if (IsValid(PlayTweenArray[Index]))
		{
			return Index;
		}
	}
	return PlayTweenArray.Num();
}

void UDreamUIPlayTweenSequenceComponent::OnTweenComplete()
{
	if (PlayTweenArray.IsValidIndex(CurrentTweenPlayIndex) && IsValid(PlayTweenArray[CurrentTweenPlayIndex]))
	{
		UnsubscribeFromTween(PlayTweenArray[CurrentTweenPlayIndex]);
	}

	CurrentTweenPlayIndex = FindNextPlayableTweenIndex(CurrentTweenPlayIndex + 1);
	if (!PlayTweenArray.IsValidIndex(CurrentTweenPlayIndex))
	{
		bIsPlaying = false;
		OnComplete.FireEvent();
		OnCompleteCPP.Broadcast();
		OnCompleteBP.Broadcast();
	}
	else
	{
		UDreamUIPlayTween* TweenItem = PlayTweenArray[CurrentTweenPlayIndex];
		SubscribeToTween(TweenItem);
		TweenItem->Start();
	}
}

void UDreamUIPlayTweenSequenceComponent::Play()
{
	if (bIsPlaying)
	{
		return;
	}

	// Play-on-start is the default, so a half-authored array is reached at run time and not only in
	// the designer: an Instanced array gains a row the moment somebody presses "+", and that row holds
	// nothing until a class is picked for it. Starting from the first row that actually holds a tween
	// is what lets the sequence play the ones it HAS rather than dereference the first hole.
	const int32 FirstIndex = FindNextPlayableTweenIndex(0);
	if (!PlayTweenArray.IsValidIndex(FirstIndex))
	{
		return;
	}

	bIsPlaying = true;
	CurrentTweenPlayIndex = FirstIndex;
	UDreamUIPlayTween* TweenItem = PlayTweenArray[CurrentTweenPlayIndex];
	SubscribeToTween(TweenItem);
	TweenItem->Start();
}
void UDreamUIPlayTweenSequenceComponent::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		if (PlayTweenArray.IsValidIndex(CurrentTweenPlayIndex) && IsValid(PlayTweenArray[CurrentTweenPlayIndex]))
		{
			UDreamUIPlayTween* TweenItem = PlayTweenArray[CurrentTweenPlayIndex];
			UnsubscribeFromTween(TweenItem);
			UDreamTweenManager::KillIfIsTweening(this, TweenItem->GetTweener(), false);
		}
	}
}
