// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTween.h"
#include "DreamGUI.h"
#include "DreamTweenManager.h"

void UDreamUIPlayTween::Stop()
{
	UDreamTweenManager::KillIfIsTweening(this, Tweener, false);
}
void UDreamUIPlayTween::Start()
{
	// Start means restart, not "one more". The member below only ever holds the newest tweener, so a
	// second Start used to strand the previous one: still in the manager's list, still ticking, still
	// broadcasting THIS play tween's events over the top of the new run, and permanently out of Stop's
	// reach, because Stop can only kill what the member points at. Retiring the old one first is what
	// keeps one play tween to one running animation.
	Stop();

	// The new tweener is held in a local as well as in the member because the callbacks below belong to
	// this particular run and must keep reading ITS cycle count. Reading the member from inside them --
	// which is what capturing by reference amounted to -- means a callback reports whatever Start ran
	// most recently rather than the tween that actually completed a cycle.
	UDreamTweener* NewTweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateLambda([] { return 0.0f; })
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamUIPlayTween::OnUpdate)
		, 1.0f, Duration);
	Tweener = NewTweener;
	if (NewTweener == nullptr)
	{
		return;
	}

	NewTweener
		->SetDelay(StartDelay)
		->SetLoop(LoopType, LoopCount)
		->SetEase(EaseType);

	// Only a CurveFloat ease carries a curve, and EaseCurve is null for every other one. Passing it
	// over regardless used to overwrite the ease function SetEase had just chosen, which meant every
	// play tween in the project animated linearly no matter what its author picked in the details
	// panel. The tweener now refuses a null curve on its own, but asking first is what the rest of
	// this codebase does at the point where the ease type is still in hand.
	if (EaseType == EDreamTweenEase::CurveFloat)
	{
		NewTweener->SetCurveFloat(EaseCurve);
	}

	NewTweener
		->OnStart([this] {
			OnStart.FireEvent();
			OnStartCPP.Broadcast();
			OnStartBP.Broadcast();
		})
		->OnUpdate([this](float progress) {
			OnUpdateProgress.FireEvent(progress);
			OnUpdateProgressCPP.Broadcast(progress);
			OnUpdateProgressBP.Broadcast(progress);
		})
		->OnCycleComplete([this, NewTweener] {
			// OnCycleComplete is declared to carry the cycle number, and a DreamUI event checks the
			// value it is handed against that declaration: firing it empty did not call the bound
			// functions at all, it logged a type error instead, so anything a designer wired to this
			// event silently never ran.
			const int32 CycleCompleteCount = NewTweener->GetLoopCycleCount();
			OnCycleComplete.FireEvent(CycleCompleteCount);
			OnCycleCompleteCPP.Broadcast(CycleCompleteCount);
			OnCycleCompleteBP.Broadcast(CycleCompleteCount);
		})
		->OnComplete([this] {
			OnComplete.FireEvent();
			OnCompleteCPP.Broadcast();
			OnCompleteBP.Broadcast();
		})
		->SetAffectByGamePause(bAffectByGamePause)
		->SetAffectByTimeDilation(bAffectByTimeDilation);
}
