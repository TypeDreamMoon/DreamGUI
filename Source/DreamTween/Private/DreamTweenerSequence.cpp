// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamTweenerSequence.h"
#include "DreamTween.h"
#include "DreamTweenManager.h"
#include "Tweener/DreamTweenerFrame.h"
#include "Tweener/DreamTweenerVirtual.h"

UDreamTweenerSequence* UDreamTweenerSequence::Append(UObject* WorldContextObject, UDreamTweener* tweener)
{
	return this->Insert(WorldContextObject, duration, tweener);
}
UDreamTweenerSequence* UDreamTweenerSequence::AppendInterval(UObject* WorldContextObject, float interval)
{
	if (elapseTime > 0 || startToTween)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d can't do this because this tween already started"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	duration += interval;
	return this;
}
UDreamTweenerSequence* UDreamTweenerSequence::Insert(UObject* WorldContextObject, float timePosition, UDreamTweener* tweener)
{
	if (!IsValid(tweener))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d tweener is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweener->IsA<UDreamTweenerFrame>() || tweener->IsA<UDreamTweenerVirtual>())
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d sequence not support this tweener type: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(tweener->GetClass()->GetName()));
		return this;
	}
	if (elapseTime > 0 || startToTween)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d can't do this because this tween already started"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweenerList.Contains(tweener))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d tweener already contains in the list"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweener->loopType != EDreamTweenLoop::Once && tweener->maxLoopCount == -1)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d infinite tweener is not supported in sequence, will convert to 1"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		tweener->maxLoopCount = 1;
	}
	UDreamTweenManager::RemoveTweener(WorldContextObject, tweener);
	int loopCount = tweener->loopType == EDreamTweenLoop::Once ? 1 : tweener->maxLoopCount;
	float tweenerTime = tweener->delay + tweener->duration * loopCount;
	tweener->SetDelay(tweener->delay + timePosition);
	tweenerList.Add(tweener);
	lastTweenStartTime = timePosition;
	float inputDuration = tweenerTime + timePosition;
	if (duration < inputDuration)
	{
		duration = inputDuration;
	}
	return this;
}
UDreamTweenerSequence* UDreamTweenerSequence::Prepend(UObject* WorldContextObject, UDreamTweener* tweener)
{
	if (!IsValid(tweener))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d tweener is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweener->IsA<UDreamTweenerFrame>() || tweener->IsA<UDreamTweenerVirtual>())
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d sequence not support this tweener type: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(tweener->GetClass()->GetName()));
		return this;
	}
	if (elapseTime > 0 || startToTween)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d can't do this because this tween already started"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweenerList.Contains(tweener))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d tweener already contains in the list"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweener->loopType != EDreamTweenLoop::Once && tweener->maxLoopCount == -1)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d infinite tweener is not supported in sequence, will convert to 1"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		tweener->maxLoopCount = 1;
	}
	UDreamTweenManager::RemoveTweener(WorldContextObject, tweener);
	int loopCount = tweener->loopType == EDreamTweenLoop::Once ? 1 : tweener->maxLoopCount;
	float inputDuration = tweener->delay + tweener->duration * loopCount;
	//offset others 
	for (auto& item : tweenerList)
	{
		item->SetDelay(item->delay + inputDuration);
	}
	tweenerList.Insert(tweener, 0);
	duration += inputDuration;
	lastTweenStartTime = 0;
	return this;
}
UDreamTweenerSequence* UDreamTweenerSequence::PrependInterval(UObject* WorldContextObject, float interval)
{
	if (elapseTime > 0 || startToTween)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d can't do this because this tween already started"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	//offset others 
	for (auto& item : tweenerList)
	{
		item->SetDelay(item->delay + interval);
	}
	duration += interval;
	lastTweenStartTime += interval;
	return this;
}
UDreamTweenerSequence* UDreamTweenerSequence::Join(UObject* WorldContextObject, UDreamTweener* tweener)
{
	if (!IsValid(tweener))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d tweener is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (tweener->IsA<UDreamTweenerFrame>() || tweener->IsA<UDreamTweenerVirtual>())
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d sequence not support this tweener type: %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(tweener->GetClass()->GetName()));
		return this;
	}
	if (tweenerList.Num() == 0)return this;
	return this->Insert(WorldContextObject, lastTweenStartTime, tweener);
}

void UDreamTweenerSequence::TweenAndApplyValue(float currentTime)
{
	for(int i = 0; i < tweenerList.Num(); i++)
	{
		auto& item = tweenerList[i];
		if (!item->ToNextWithElapsedTime(currentTime))
		{
			finishedTweenerList.Add(item);
			tweenerList.RemoveAt(i);
			i--;
		}
	}
}

void UDreamTweenerSequence::SetOriginValueForRestart()
{
	for (auto& item : finishedTweenerList)
	{
		//add tweener to tweenerList
		tweenerList.Add(item);
	}
	finishedTweenerList.Reset();

	for (auto& item : tweenerList)
	{
		if (item->elapseTime > 0 || item->startToTween)
		{
			item->SetOriginValueForRestart();//if tween already start, then we can call "SetOriginValueForRestart"
			item->TweenAndApplyValue(0);
		}
		//set parameter to initial
		item->elapseTime = 0;
		item->loopCycleCount = 0;
		item->foldedCycleCount = 0;
		item->reverseTween = false;
	}
}

void UDreamTweenerSequence::SetValueForIncremental()
{
	for (auto& item : finishedTweenerList)
	{
		item->SetValueForIncremental();
		//set parameter to initial
		item->elapseTime = 0;
		item->loopCycleCount = 0;
		item->foldedCycleCount = 0;
		item->reverseTween = false;
		item->TweenAndApplyValue(0);

		//add tweener to tweenerList
		tweenerList.Add(item);
	}
	finishedTweenerList.Reset();
}
void UDreamTweenerSequence::SetValueForYoyo()
{
	this->reverseTween = !this->reverseTween;//reverse it again, so it will keep value false, because we only need to reverse tweenerList
	for (auto& item : finishedTweenerList)
	{
		if (item->loopType != EDreamTweenLoop::Yoyo)//if it is already yoyo, then we no need to change reverseTween for it
		{
			item->reverseTween = !item->reverseTween;
		}

		//set parameter to initial
		item->elapseTime = 0;
		item->loopCycleCount = 0;
		item->foldedCycleCount = 0;
		//flip tweener
		int loopCount = item->loopType == EDreamTweenLoop::Once ? 1 : item->maxLoopCount;
		float tweenerDelay = duration - (item->delay + item->duration * loopCount);
		item->delay = tweenerDelay;

		//add tweener to tweenerList
		tweenerList.Add(item);
	}
	finishedTweenerList.Reset();
}
void UDreamTweenerSequence::SetValueForRestart()
{
	for (auto& item : finishedTweenerList)
	{
		//set parameter to initial
		item->elapseTime = 0;
		item->loopCycleCount = 0;
		item->foldedCycleCount = 0;
		item->reverseTween = false;
		item->TweenAndApplyValue(0);

		//add tweener to tweenerList
		tweenerList.Add(item);
	}
	finishedTweenerList.Reset();
}
void UDreamTweenerSequence::Restart()
{
	if (elapseTime == 0)
	{
		return;
	}
	this->isMarkedPause = false;//incase it is paused.

	//reset parameter and value to start
	{
		//reset parameter to initial
		if (this->loopType == EDreamTweenLoop::Yoyo)
		{
			if (loopCycleCount % 2 != 0)//this means current is yoyo back, then we should reverse it
			{
				this->reverseTween = true;
				for (auto& item : tweenerList)
				{
					finishedTweenerList.Add(item);
				}
				tweenerList.Reset();
				this->SetValueForYoyo();
			}
		}
		for (auto& item : finishedTweenerList)
		{
			tweenerList.Add(item);
		}
		finishedTweenerList.Reset();
		this->loopCycleCount = 0;
		this->foldedCycleCount = 0;

		//sort it, so later tweener can do "SetOriginValueForRestart" ealier, so ealier tweener will get correct start state
		tweenerList.Sort([=](const UDreamTweener& A, const UDreamTweener& B) {
			return A.delay > B.delay;
			});
		for (int i = 0; i < tweenerList.Num(); i++)
		{
			auto& item = tweenerList[i];
			if (item->startToTween)
			{
				item->SetOriginValueForRestart();
				item->TweenAndApplyValue(0);
			}
			//set parameter to initial
			item->elapseTime = 0;
			item->loopCycleCount = 0;
			item->foldedCycleCount = 0;
			item->reverseTween = false;
		}
	}

	this->ToNextWithElapsedTime(0);
}
void UDreamTweenerSequence::Goto(float timePoint)
{
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);

	//reset parameter to start, then goto timepoint. these line should be same as lines in "Restart"
	{
		//reset parameter to initial
		if (this->loopType == EDreamTweenLoop::Yoyo)
		{
			if (loopCycleCount % 2 != 0)//mean current is yoyo back, should reverse it
			{
				this->reverseTween = true;
				for (auto& item : tweenerList)
				{
					finishedTweenerList.Add(item);
				}
				tweenerList.Reset();
				this->SetValueForYoyo();
			}
		}
		for (auto& item : finishedTweenerList)
		{
			tweenerList.Add(item);
		}
		finishedTweenerList.Reset();
		this->loopCycleCount = 0;
		this->foldedCycleCount = 0;

		//sort it, so later tweener can do "SetOriginValueForRestart" ealier, so ealier tweener will get correct start state
		tweenerList.Sort([=](const UDreamTweener& A, const UDreamTweener& B) {
			return A.delay > B.delay;
			});
		for (int i = 0; i < tweenerList.Num(); i++)
		{
			auto& item = tweenerList[i];
			if (item->startToTween)
			{
				item->SetOriginValueForRestart();
				item->TweenAndApplyValue(0);
			}
			//set parameter to initial
			item->elapseTime = 0;
			item->loopCycleCount = 0;
			item->foldedCycleCount = 0;
			item->reverseTween = false;
		}
	}

	this->ToNextWithElapsedTime(timePoint);
}

