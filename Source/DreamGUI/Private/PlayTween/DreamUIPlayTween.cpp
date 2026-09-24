// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PlayTween/DreamUIPlayTween.h"
#include "DreamGUI.h"
#include "DreamTweenManager.h"
#include "Curves/CurveFloat.h"

namespace DreamUIPlayTweenNoManagerLocal
{
	/**
	 * How deep runs with no tween manager may nest before one refuses to start. Such a run ends inside
	 * Start, so a completion handler that starts another play tween -- which is how
	 * UDreamUIPlayTweenSequenceComponent chains them -- runs it one call deeper, and a handler that
	 * starts THIS one again (a loop built by hand out of OnComplete) would go deeper forever. Far past
	 * the length of any authored sequence.
	 */
	constexpr int32 MaxNestedRuns = 32;
	/** How many such runs are on the stack right now. Game thread only, like every play tween. */
	int32 NestedRuns = 0;

	/**
	 * What OnUpdate is handed at the end of one cycle: the ease evaluated where UDreamTweener evaluates
	 * it when a cycle completes -- its last moment, or its first for a yoyo on the way back -- from
	 * InCycleStart over a change of one (the play tween's getter is 0 and its end 1; an incremental
	 * loop starts each cycle where the last one ended). Bound the way the tweener binds it, so a curve
	 * that does not end at one -- a punch that returns home -- lands where the curve says.
	 */
	float CycleEndValue(EDreamTweenEase InEase, const UCurveFloat* InCurve, float InCycleStart, bool bInBackwards, float InDuration)
	{
		const float Time = bInBackwards ? 0.0f : InDuration;
		if (InEase == EDreamTweenEase::CurveFloat)
		{
			// UDreamTweener::SetCurveFloat's reading, down to its answer for no curve at all: linear.
			if (InDuration < UE_KINDA_SMALL_NUMBER)
			{
				return 1.0f + InCycleStart;
			}
			if (InCurve == nullptr)
			{
				return UDreamTweener::Linear(1.0f, InCycleStart, Time, InDuration);
			}
			return InCurve->GetFloatValue(Time / InDuration) + InCycleStart;
		}
		const FDreamTweenFunction EaseFunction = UDreamTweener::GetEaseFunction(InEase);
		// Unbound only for CurveFloat, answered above; OutCubic is the ease a tweener is born with.
		return EaseFunction.IsBound()
			? EaseFunction.Execute(1.0f, InCycleStart, Time, InDuration)
			: UDreamTweener::OutCubic(1.0f, InCycleStart, Time, InDuration);
	}
}

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
		// No tween manager to run on: it is a game instance subsystem, and a world no game instance owns
		// -- the designer's preview is one -- has none, so To answers null. The run is played out here
		// and now instead, in the tween's own order: the start; then per cycle the value it ends on, the
		// progress at one and the cycle's completion; then the completion. So the target lands where the
		// tween would have left it, and whatever waits for the end -- a sequence component moving on to
		// its next tween -- still hears it. Returning here instead left the target at its start and a
		// sequence stuck on its first tween for good. An endless loop has no end to land on: it plays one
		// cycle and, like a real endless loop, never completes. The start delay has no clock to wait on.
		using namespace DreamUIPlayTweenNoManagerLocal;
		if (NestedRuns >= MaxNestedRuns)
		{
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' was started from inside %d nested runs with no tween manager to pace them -- a play tween restarting itself from its own completion -- and is not run again."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), NestedRuns);
			return;
		}
		TGuardValue<int32> NestingGuard(NestedRuns, NestedRuns + 1);

		const bool bEndless = LoopType != EDreamTweenLoop::Once && LoopCount <= -1;
		// A finite loop plays at least one cycle whatever its count says, as UDreamTweener does.
		const int32 CycleCount = (LoopType == EDreamTweenLoop::Once || bEndless) ? 1 : FMath::Max(LoopCount, 1);
		OnStart.FireEvent();
		OnStartCPP.Broadcast();
		OnStartBP.Broadcast();
		float CycleStart = 0.0f;
		bool bBackwards = false;
		for (int32 Cycle = 1; Cycle <= CycleCount; ++Cycle)
		{
			OnUpdate(CycleEndValue(EaseType, EaseCurve.Get(), CycleStart, bBackwards, Duration));
			OnUpdateProgress.FireEvent(1.0f);
			OnUpdateProgressCPP.Broadcast(1.0f);
			OnUpdateProgressBP.Broadcast(1.0f);
			OnCycleComplete.FireEvent(Cycle);
			OnCycleCompleteCPP.Broadcast(Cycle);
			OnCycleCompleteBP.Broadcast(Cycle);
			// What the tween does between cycles (UDreamTweener::ToNextWithElapsedTime): a yoyo turns
			// round, an incremental loop carries on from where this cycle ended, a restart replays.
			if (LoopType == EDreamTweenLoop::Yoyo)
			{
				bBackwards = !bBackwards;
			}
			else if (LoopType == EDreamTweenLoop::Incremental)
			{
				CycleStart += 1.0f;
			}
		}
		if (!bEndless)
		{
			OnComplete.FireEvent();
			OnCompleteCPP.Broadcast();
			OnCompleteBP.Broadcast();
		}
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

	// The tween outlives this object: it is owned by the game instance's tween manager, and with the
	// default LoopType it never ends on its own. These callbacks are plain lambdas -- BindLambda, not
	// BindWeakLambda -- so a bare this would still be called, and would still fire this object's events
	// over a destroyed widget tree, long after the play tween itself was gone.
	const TWeakObjectPtr<UDreamUIPlayTween> WeakThis(this);
	NewTweener
		->OnStart([WeakThis] {
			if (!WeakThis.IsValid())return;
			WeakThis->OnStart.FireEvent();
			WeakThis->OnStartCPP.Broadcast();
			WeakThis->OnStartBP.Broadcast();
		})
		->OnUpdate([WeakThis](float progress) {
			if (!WeakThis.IsValid())return;
			WeakThis->OnUpdateProgress.FireEvent(progress);
			WeakThis->OnUpdateProgressCPP.Broadcast(progress);
			WeakThis->OnUpdateProgressBP.Broadcast(progress);
		})
		->OnCycleComplete([WeakThis, NewTweener] {
			// OnCycleComplete is declared to carry the cycle number, and a DreamUI event checks the
			// value it is handed against that declaration: firing it empty did not call the bound
			// functions at all, it logged a type error instead, so anything a designer wired to this
			// event silently never ran.
			if (!WeakThis.IsValid())return;
			const int32 CycleCompleteCount = NewTweener->GetLoopCycleCount();
			WeakThis->OnCycleComplete.FireEvent(CycleCompleteCount);
			WeakThis->OnCycleCompleteCPP.Broadcast(CycleCompleteCount);
			WeakThis->OnCycleCompleteBP.Broadcast(CycleCompleteCount);
		})
		->OnComplete([WeakThis] {
			if (!WeakThis.IsValid())return;
			WeakThis->OnComplete.FireEvent();
			WeakThis->OnCompleteCPP.Broadcast();
			WeakThis->OnCompleteBP.Broadcast();
		})
		->SetAffectByGamePause(bAffectByGamePause)
		->SetAffectByTimeDilation(bAffectByTimeDilation);
}
