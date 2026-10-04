// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamTweenerSequence.h"
#include "DreamTween.h"
#include "DreamTweenManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Tweener/DreamTweenerCallback.h"
#include "Tweener/DreamTweenerFrame.h"
#include "Tweener/DreamTweenerVirtual.h"

namespace DreamTweenerSequenceLocal
{
	/**
	 * How many seeks of a sequence may sit one inside another. A legitimate one -- a callback that loops the
	 * sequence back with Goto or Restart -- nests once; only a callback that seeks forward past its own position
	 * keeps finding itself again, and that is a loop, not an animation.
	 */
	constexpr int32 MaxSeekDepth = 16;

	/**
	 * The manager that drives InTweener, found through the tween's own world. Not through UGameplayStatics, which
	 * warns about every object with no world -- a tween built outside one is an ordinary thing to hand a sequence.
	 */
	UDreamTweenManager* FindManagerDriving(const UDreamTweener* InTweener)
	{
		UWorld* World = GEngine != nullptr ? GEngine->GetWorldFromContextObject(InTweener, EGetWorldErrorMode::ReturnNull) : nullptr;
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr ? GameInstance->GetSubsystem<UDreamTweenManager>() : nullptr;
	}

	/**
	 * Out of the manager that drives it, and so driven by the sequence alone. Found from the tween itself
	 * rather than the caller's context, which is often null (C++ building a sequence by hand passes none) or
	 * an object of another world: the child was then left in its manager's list and stepped twice a frame,
	 * once by the manager and once by the sequence.
	 */
	void TakeOutOfManager(UDreamTweener* InTweener)
	{
		if (UDreamTweenManager* Manager = FindManagerDriving(InTweener))
		{
			Manager->RemoveTweener(InTweener);
		}
	}
}

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
	if (!FMath::IsFinite(interval) || interval < 0.0f)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d interval %f is not a length of time"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, interval);
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
	if (!FMath::IsFinite(timePosition))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d time position is not a number"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return this;
	}
	timePosition = FMath::Max(timePosition, 0.0f);
	if (tweener->loopType != EDreamTweenLoop::Once && tweener->maxLoopCount == -1)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d infinite tweener is not supported in sequence, will convert to 1"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		tweener->maxLoopCount = 1;
	}
	DreamTweenerSequenceLocal::TakeOutOfManager(tweener);
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
	DreamTweenerSequenceLocal::TakeOutOfManager(tweener);
	RemoveInvalidChildren();
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
	if (!FMath::IsFinite(interval) || interval < 0.0f)
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d interval %f is not a length of time"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, interval);
		return this;
	}
	RemoveInvalidChildren();
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
	if (!FMath::IsFinite(timePosition))
	{
		UE_LOG(DreamTween, Error, TEXT("[%s].%d time position is not a number"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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
	// Its clock changed hands. A manager tick that saw it finish earlier in the same frame reads this as
	// "taken over" and leaves it to the sequence instead of retiring it (see clockGeneration).
	tweener->clockGeneration++;
}

void UDreamTweenerSequence::RemoveInvalidChildren()
{
	const auto IsGone = [](const TObjectPtr<UDreamTweener>& Item)
	{
		return !IsValid(Item) || Item->IsOwnerGone();
	};
	tweenerList.RemoveAll(IsGone);
	finishedTweenerList.RemoveAll(IsGone);
}

bool UDreamTweenerSequence::CanSeekFromHere()const
{
	if (seekDepth < DreamTweenerSequenceLocal::MaxSeekDepth)
	{
		return true;
	}
	UE_LOG(DreamTween, Error, TEXT("[%s].%d %s was sent back into itself %d times from its own callbacks without returning -- most likely a callback that seeks forward past its own position, which reaches it again. This seek is refused."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetName(), seekDepth);
	return false;
}

void UDreamTweenerSequence::TweenAndApplyValue(float currentTime)
{
	RemoveInvalidChildren();
	// Over a snapshot, and only for as long as the sequence is still the one this pass started on. A child's
	// callback is free to Goto or Restart this very sequence; that rebuilds and re-sorts the lists under the
	// walk and runs a pass of its own at the new time, so whatever is left of this one is stale. Walking on by
	// index stepped whatever had moved into the slot, moved the wrong child to the finished list, and reached
	// the reset callback child again in the same pass -- firing it again, and for ever if it seeks.
	// clockGeneration says when that has happened; the snapshot keeps a reallocated list from moving under it.
	const int32 generation = clockGeneration;
	const TArray<TObjectPtr<UDreamTweener>> childrenThisPass = tweenerList;
	for (UDreamTweener* item : childrenThisPass)
	{
		if (!IsValid(item))
		{
			continue;
		}
		const bool bChildRunning = item->ToNextWithElapsedTime(currentTime);
		if (clockGeneration != generation)
		{
			return;
		}
		if (!bChildRunning)
		{
			tweenerList.RemoveSingle(item);
			finishedTweenerList.Add(item);
		}
	}
}

void UDreamTweenerSequence::SetOriginValueForRestart()
{
	RemoveInvalidChildren();
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
	RemoveInvalidChildren();
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
	RemoveInvalidChildren();
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
	RemoveInvalidChildren();
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
void UDreamTweenerSequence::RewindChildrenToStart()
{
	RemoveInvalidChildren();
	//reset parameter to initial
	if (this->loopType == EDreamTweenLoop::Yoyo)
	{
		if (loopCycleCount % 2 != 0)//this means current is yoyo back, then we should reverse it
		{
			this->reverseTween = true;
			finishedTweenerList.Append(tweenerList);
			tweenerList.Reset();
			this->SetValueForYoyo();
		}
	}
	tweenerList.Append(finishedTweenerList);
	finishedTweenerList.Reset();
	this->loopCycleCount = 0;
	this->foldedCycleCount = 0;

	//sort it, so later tweener can do "SetOriginValueForRestart" ealier, so ealier tweener will get correct start state
	tweenerList.Sort([](const UDreamTweener& A, const UDreamTweener& B) {
		return A.delay > B.delay;
		});
	// Over a copy: putting a child back at its start runs its setter, which is caller code, and the
	// reference into the list the loop used to hold did not survive a reallocation under it.
	const TArray<TObjectPtr<UDreamTweener>> childrenToRewind = tweenerList;
	for (UDreamTweener* item : childrenToRewind)
	{
		if (!IsValid(item))
		{
			continue;
		}
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
		// And a step the child is in the middle of -- this rewind came from one of its own callbacks --
		// stops there instead of finishing off the state just reset (a yoyo turning round, say).
		item->clockGeneration++;
	}
}
void UDreamTweenerSequence::Restart()
{
	if (IsRetired())
	{
		// As UDreamTweener::Restart: a handle to a sequence the manager has let go of has nothing to rewind.
		if (bRetired)
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenerSequence::Restart] %s was retired when it ended, so there is nothing to restart. Keep a sequence with SetAutoKill(false) to restart it after it finishes."), *GetName());
		}
		return;
	}
	if (elapseTime == 0 && !startToTween)
	{
		return;
	}
	if (!CanSeekFromHere())
	{
		return;
	}
	TGuardValue<int32> SeekDepthGuard(seekDepth, seekDepth + 1);
	// A pass of this sequence in progress -- Restart called from a child's callback -- stops where it is.
	clockGeneration++;
	this->isMarkedPause = false;//incase it is paused.
	// Same two omissions the inherited Restart had, and this override has to repeat their repair
	// because it replaces that implementation rather than extending it: a killed sequence stayed
	// killed however often it was restarted, and its own OnStart never fired a second time.
	this->isMarkedToKill = false;
	this->startToTween = false;

	//reset parameter and value to start
	RewindChildrenToStart();

	this->ToNextWithElapsedTime(0);
}
void UDreamTweenerSequence::Goto(float timePoint)
{
	// As UDreamTweener::Goto: nothing to seek on a sequence that is killed or retired, or at no time at all.
	if (isMarkedToKill || IsRetired() || !FMath::IsFinite(timePoint))
	{
		return;
	}
	if (!CanSeekFromHere())
	{
		return;
	}
	TGuardValue<int32> SeekDepthGuard(seekDepth, seekDepth + 1);
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);
	// A pass of this sequence in progress -- Goto called from a child's callback -- stops where it is.
	clockGeneration++;

	//reset parameter to start, then goto timepoint: the same rewind Restart makes
	RewindChildrenToStart();

	// delay + timePoint, as in UDreamTweener::Goto: timePoint is a position in the sequence, while
	// elapseTime counts this sequence's own delay before it.
	const bool bStillRunning = this->ToNextWithElapsedTime(delay + timePoint);
	// Completed here, once, as UDreamTweener::Goto completes: ToNext then finds every cycle played.
	if (!bStillRunning && !isMarkedToKill)
	{
		FinishOrHold(false);
	}
}

