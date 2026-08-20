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
void UDreamUIPlayTweenSequenceComponent::OnTweenComplete()
{
	if (bPlayNextWhenCycleComplete)
	{
		PlayTweenArray[CurrentTweenPlayIndex]->OnCycleCompleteCPP.Remove(OnCompleteDelegateHandle);
	}
	else
	{
		PlayTweenArray[CurrentTweenPlayIndex]->OnCycleCompleteCPP.Remove(OnCompleteDelegateHandle);
	}

	CurrentTweenPlayIndex++;
	if (CurrentTweenPlayIndex >= PlayTweenArray.Num())
	{
		bIsPlaying = false;
		OnComplete.FireEvent();
		OnCompleteCPP.Broadcast();
		OnCompleteBP.Broadcast();
	}
	else
	{
		auto& tweenItem = PlayTweenArray[CurrentTweenPlayIndex];
		if (bPlayNextWhenCycleComplete)
		{
			OnCompleteDelegateHandle = tweenItem->OnCycleCompleteCPP.AddWeakLambda(this, [this](int count) {
				OnTweenComplete();
				});
		}
		else
		{
			OnCompleteDelegateHandle = tweenItem->OnCompleteCPP.AddUObject(this, &UDreamUIPlayTweenSequenceComponent::OnTweenComplete);
		}
		tweenItem->Start();
	}
}

void UDreamUIPlayTweenSequenceComponent::Play()
{
	if (PlayTweenArray.Num() > 0)
	{
		if (!bIsPlaying)
		{
			bIsPlaying = true;
			CurrentTweenPlayIndex = 0;
			auto& tweenItem = PlayTweenArray[CurrentTweenPlayIndex];
			if (bPlayNextWhenCycleComplete)
			{
				OnCompleteDelegateHandle = tweenItem->OnCycleCompleteCPP.AddWeakLambda(this, [this](int count) {
					OnTweenComplete();
					});
			}
			else
			{
				OnCompleteDelegateHandle = tweenItem->OnCompleteCPP.AddUObject(this, &UDreamUIPlayTweenSequenceComponent::OnTweenComplete);
			}
			tweenItem->Start();
		}
	}
}
void UDreamUIPlayTweenSequenceComponent::Stop()
{
	if (bIsPlaying)
	{
		bIsPlaying = false;
		auto& tweenItem = PlayTweenArray[CurrentTweenPlayIndex];
		tweenItem->OnCompleteCPP.Remove(OnCompleteDelegateHandle);
		UDreamTweenManager::KillIfIsTweening(this, tweenItem->GetTweener(), false);
	}
}
