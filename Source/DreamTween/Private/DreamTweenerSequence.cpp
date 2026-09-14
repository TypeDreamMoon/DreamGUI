// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamTweenerSequence.h"
#include "DreamTween.h"
#include "DreamTweenManager.h"
#include "Tweener/DreamTweenerCallback.h"
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
	// Written through, not via SetDelay. SetDelay refuses -- silently, returning this -- once a tween
	// has started, and a tween made by UDreamTweenManager::To is in the manager's list ticking from the
	// moment it exists. Building the tweens one frame and assembling them the next is ordinary in a
	// graph, and every child added that way used to keep delay 0 and play on top of the others at t=0.
	// The sequence is a friend of UDreamTweener for exactly this.
	tweener->delay = tweener->delay + timePosition;
	AdoptTweenerClock(tweener);
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
		// Direct, for the reason spelled out in Insert: SetDelay is a no-op on a started tween, and
		// here that would shift only SOME of the children -- leaving the ones already running where
		// they were while everything else moved, which is worse than not prepending at all.
		item->delay = item->delay + inputDuration;
	}
	AdoptTweenerClock(tweener);
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
		// Direct, for the reason spelled out in Insert.
		item->delay = item->delay + interval;
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

UDreamTweenerSequence* UDreamTweenerSequence::AppendCallback(const FDreamTweenSimpleDynamicDelegate& callback)
{
	return InsertCallbackInternal(duration, [callback] { callback.ExecuteIfBound(); });
}
UDreamTweenerSequence* UDreamTweenerSequence::InsertCallback(float timePosition, const FDreamTweenSimpleDynamicDelegate& callback)
{
	return InsertCallbackInternal(timePosition, [callback] { callback.ExecuteIfBound(); });
}
UDreamTweenerSequence* UDreamTweenerSequence::AppendCallback(const TFunction<void()>& callback)
{
	return InsertCallbackInternal(duration, callback);
}
UDreamTweenerSequence* UDreamTweenerSequence::InsertCallback(float timePosition, const TFunction<void()>& callback)
{
	return InsertCallbackInternal(timePosition, callback);
}

UDreamTweenerSequence* UDreamTweenerSequence::InsertCallbackInternal(float timePosition, const TFunction<void()>& callback)
{
	if (elapseTime > 0 || startToTween)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d can't do this because this tween already started"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	if (callback == nullptr)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d callback is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	// A zero-length tween positioned by its delay, so the sequence's own machinery carries it: the
	// yoyo flip, the restart rewind and the finished-list bookkeeping all treat it as a child like
	// any other, which a separate list of "callbacks at times" would have had to reimplement.
	const float Position = FMath::Max(0.0f, timePosition);
	UDreamTweenerCallback* CallbackTweener = NewObject<UDreamTweenerCallback>(this);
	CallbackTweener->SetInitialValue();
	// A hair before the position asked for, and only because the clock is read with a STRICT
	// comparison (elapseTime > delay). The commonest callback of all sits at the very end of the
	// sequence, and the step that ends a sequence lands exactly on its duration -- so without this
	// the "and then hide the panel" callback would be the one that never runs.
	CallbackTweener->delay = FMath::Max(0.0f, Position - UE_KINDA_SMALL_NUMBER);
	CallbackTweener->OnComplete(callback);
	tweenerList.Add(CallbackTweener);
	// A callback at the very end is part of the sequence's length; one past the end extends it, the
	// same way an interval does.
	duration = FMath::Max(duration, Position);
	lastTweenStartTime = Position;
	return this;
}

void UDreamTweenerSequence::AdoptTweenerClock(UDreamTweener* tweener)
{
	// Every child is stepped with the SEQUENCE's elapsed time (see TweenAndApplyValue), so whatever
	// run this tween had already begun on the manager's clock ends here. Left alone, a child that had
	// started keeps startToTween raised: neither its OnStart nor its OnStartGetValue would ever run
	// again, and it would interpolate from whatever value it happened to be holding when it was made.
	tweener->elapseTime = 0;
	tweener->startToTween = false;
	tweener->loopCycleCount = 0;
	tweener->foldedCycleCount = 0;
	tweener->reverseTween = false;
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
		// Starting again is starting: with this left raised the child's OnStart and OnCycleStart never
		// fired a second time, and OnStartGetValue never re-read the value the line above just restored.
		item->startToTween = false;
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
	// Same two omissions the inherited Restart had, and this override has to repeat their repair
	// because it replaces that implementation rather than extending it: a killed sequence stayed
	// killed however often it was restarted, and its own OnStart never fired a second time.
	this->isMarkedToKill = false;
	this->startToTween = false;

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
			// See SetOriginValueForRestart: rewound to the beginning means starting again, callbacks
			// and start value included.
			item->startToTween = false;
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
			// See SetOriginValueForRestart: rewound to the beginning means starting again, callbacks
			// and start value included.
			item->startToTween = false;
		}
	}

	// delay + timePoint, as in UDreamTweener::Goto: timePoint is a position in the sequence, while
	// elapseTime counts this sequence's own delay before it.
	this->ToNextWithElapsedTime(delay + timePoint);
}

