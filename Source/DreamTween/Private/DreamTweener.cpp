// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamTweener.h"
#include "Curves/CurveFloat.h"
#include "DreamTween.h"
#include "Engine/World.h"

UDreamTweener::UDreamTweener()
{
	tweenFunc.BindStatic(&UDreamTweener::OutCubic);//OutCubic default animation curve function
}
FDreamTweenFunction UDreamTweener::GetEaseFunction(EDreamTweenEase easetype)
{
	FDreamTweenFunction Result;
	switch (easetype)
	{
	case EDreamTweenEase::Linear:
		Result.BindStatic(&UDreamTweener::Linear);
		break;
	case EDreamTweenEase::InQuad:
		Result.BindStatic(&UDreamTweener::InQuad);
		break;
	case EDreamTweenEase::OutQuad:
		Result.BindStatic(&UDreamTweener::OutQuad);
		break;
	case EDreamTweenEase::InOutQuad:
		Result.BindStatic(&UDreamTweener::InOutQuad);
		break;
	case EDreamTweenEase::InCubic:
		Result.BindStatic(&UDreamTweener::InCubic);
		break;
	case EDreamTweenEase::OutCubic:
		Result.BindStatic(&UDreamTweener::OutCubic);
		break;
	case EDreamTweenEase::InOutCubic:
		Result.BindStatic(&UDreamTweener::InOutCubic);
		break;
	case EDreamTweenEase::InQuart:
		Result.BindStatic(&UDreamTweener::InQuart);
		break;
	case EDreamTweenEase::OutQuart:
		Result.BindStatic(&UDreamTweener::OutQuart);
		break;
	case EDreamTweenEase::InOutQuart:
		Result.BindStatic(&UDreamTweener::InOutQuart);
		break;
	case EDreamTweenEase::InSine:
		Result.BindStatic(&UDreamTweener::InSine);
		break;
	case EDreamTweenEase::OutSine:
		Result.BindStatic(&UDreamTweener::OutSine);
		break;
	case EDreamTweenEase::InOutSine:
		Result.BindStatic(&UDreamTweener::InOutSine);
		break;
	case EDreamTweenEase::InExpo:
		Result.BindStatic(&UDreamTweener::InExpo);
		break;
	case EDreamTweenEase::OutExpo:
		Result.BindStatic(&UDreamTweener::OutExpo);
		break;
	case EDreamTweenEase::InOutExpo:
		Result.BindStatic(&UDreamTweener::InOutExpo);
		break;
	case EDreamTweenEase::InCirc:
		Result.BindStatic(&UDreamTweener::InCirc);
		break;
	case EDreamTweenEase::OutCirc:
		Result.BindStatic(&UDreamTweener::OutCirc);
		break;
	case EDreamTweenEase::InOutCirc:
		Result.BindStatic(&UDreamTweener::InOutCirc);
		break;
	case EDreamTweenEase::InElastic:
		Result.BindStatic(&UDreamTweener::InElastic);
		break;
	case EDreamTweenEase::OutElastic:
		Result.BindStatic(&UDreamTweener::OutElastic);
		break;
	case EDreamTweenEase::InOutElastic:
		Result.BindStatic(&UDreamTweener::InOutElastic);
		break;
	case EDreamTweenEase::InBack:
		Result.BindStatic(&UDreamTweener::InBack);
		break;
	case EDreamTweenEase::OutBack:
		Result.BindStatic(&UDreamTweener::OutBack);
		break;
	case EDreamTweenEase::InOutBack:
		Result.BindStatic(&UDreamTweener::InOutBack);
		break;
	case EDreamTweenEase::InBounce:
		Result.BindStatic(&UDreamTweener::InBounce);
		break;
	case EDreamTweenEase::OutBounce:
		Result.BindStatic(&UDreamTweener::OutBounce);
		break;
	case EDreamTweenEase::InOutBounce:
		Result.BindStatic(&UDreamTweener::InOutBounce);
		break;
	}
	return Result;
}
UDreamTweener* UDreamTweener::SetEase(EDreamTweenEase easetype)
{
	if (elapseTime > 0 || startToTween)return this;
	this->easeType = easetype;
	// CurveFloat has no static curve of its own -- SetCurveFloat binds that -- so an unbound result
	// must leave whatever is already bound alone, exactly as the original switch did by omitting it.
	FDreamTweenFunction Func = GetEaseFunction(easetype);
	if (Func.IsBound())
	{
		tweenFunc = Func;
	}
	return this;
}
UDreamTweener* UDreamTweener::SetDelay(float newDelay)
{
	if (elapseTime > 0 || startToTween)return this;
	// A delay that is not a number is one the clock never gets past: every comparison against it is false,
	// so the tween would wait for ever without a word.
	if (!FMath::IsFinite(newDelay))
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetDelay] %s was given a delay that is not a number; it starts without one."), *GetName());
		newDelay = 0.0f;
	}
	this->delay = FMath::Max(newDelay, 0.0f);
	return this;
}
UDreamTweener* UDreamTweener::SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount)
{
	if (elapseTime > 0 || startToTween)return this;
	this->loopType = newLoopType;
	this->maxLoopCount = newLoopCount;
	return this;
}

UDreamTweener* UDreamTweener::SetCurveFloat(UCurveFloat* newCurveFloat)
{
	if (elapseTime > 0 || startToTween)return this;
	if (newCurveFloat == nullptr)
	{
		// A null curve is not a curve to animate along, it is the absence of one, and rebinding on it
		// could only destroy the ease that had already been chosen. That is exactly what used to
		// happen to every caller holding an authored ease type and an authored curve side by side:
		// handing the curve over unconditionally replaced the chosen ease with a lambda that re-tested
		// the same null on every evaluation, logged from inside the tween loop, and ran linear. So a
		// null is answered here instead of per frame, and only where the author actually asked for
		// CurveFloat does it mean anything -- everywhere else the ease already set survives untouched.
		if (easeType == EDreamTweenEase::CurveFloat)
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetCurveFloat] CurveFloat not valid! Fallback to linear. You should always call SetCurveFloat(and pass a valid curve) if set Easetype to CurveFloat."));
			tweenFunc.BindStatic(&UDreamTweener::Linear);
		}
		return this;
	}
	// Held, so the curve stays loaded while this tween can evaluate it, and read through a weak pointer
	// checked on every evaluation, so a curve the collector takes anyway -- one marked garbage, deleted or
	// replaced in the editor -- reads as linear instead of through freed memory. The binding is a plain
	// lambda: the subclasses Execute tweenFunc without asking whether it is bound, and a weak binding whose
	// object died only checkSlow's before running its lambda regardless.
	curveFloat = newCurveFloat;
	const TWeakObjectPtr<UCurveFloat> WeakCurve(newCurveFloat);
	tweenFunc.BindLambda([WeakCurve](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		const UCurveFloat* Curve = WeakCurve.Get();
		if (Curve == nullptr)return Linear(c, b, t, d);
		return Curve->GetFloatValue(t / d) * c + b;
	});
	return this;
}

UDreamTweener* UDreamTweener::SetRuntimeFloatCurve(const FRuntimeFloatCurve& Value)
{
	if (elapseTime > 0 || startToTween)return this;
	// A curve with no keys evaluates to 0 everywhere, so a tween eased by it never leaves its start: a layout
	// animation set to CurveFloat with its default, empty curve wrote the old rect and stayed there. It is
	// answered the way SetCurveFloat answers no curve -- linear, said out loud, and only where the author
	// actually chose CurveFloat; any other ease already set is left alone.
	const FRichCurve* RichCurve = Value.GetRichCurveConst();
	if (RichCurve == nullptr || RichCurve->GetNumKeys() == 0)
	{
		if (easeType == EDreamTweenEase::CurveFloat)
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetRuntimeFloatCurve] %s was given a curve with no keys. Fallback to linear."), *GetName());
			tweenFunc.BindStatic(&UDreamTweener::Linear);
		}
		return this;
	}
	// The asset a runtime curve may point at, held for as long as this tween lives and read through a weak
	// pointer, for the reasons given in SetCurveFloat. Inline keys are copied into the function, which then
	// owns them outright.
	runtimeExternalCurve = Value.ExternalCurve;
	if (Value.ExternalCurve != nullptr)
	{
		const TWeakObjectPtr<UCurveFloat> WeakCurve(Value.ExternalCurve);
		tweenFunc.BindLambda([WeakCurve](float c, float b, float t, float d) {
			if (d < KINDA_SMALL_NUMBER)return c + b;
			const UCurveFloat* Curve = WeakCurve.Get();
			if (Curve == nullptr)return Linear(c, b, t, d);
			return Curve->FloatCurve.Eval(t / d) * c + b;
		});
	}
	else
	{
		tweenFunc.BindLambda([InlineCurve = Value.EditorCurveData](float c, float b, float t, float d) {
			if (d < KINDA_SMALL_NUMBER)return c + b;
			return InlineCurve.Eval(t / d) * c + b;
		});
	}
	return this;
}

UDreamTweener* UDreamTweener::SetAffectByGamePause(bool value)
{
	affectByGamePause = value;
	return this;
}
UDreamTweener* UDreamTweener::SetAffectByTimeDilation(bool value)
{
	affectByTimeDilation = value;
	return this;
}
UDreamTweener* UDreamTweener::SetTimeScale(float value)
{
	// A scale that is not a number would stop the clock for good, and a negative one runs it backwards:
	// the tween either never gets past its delay again or hands the eases a time below zero, where
	// OutCirc answers NaN. Neither is a speed, so the first is refused and the second held at 0.
	if (!FMath::IsFinite(value))
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetTimeScale] %s was given a time scale that is not a number; the scale is left at %f."), *GetName(), timeScale);
		return this;
	}
	if (value < 0.0f)
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetTimeScale] %s was given a negative time scale (%f); a tween does not run backwards, so it is held still instead."), *GetName(), value);
		value = 0.0f;
	}
	timeScale = value;
	return this;
}
UDreamTweener* UDreamTweener::SetAutoKill(bool value)
{
	if (elapseTime > 0 || startToTween)return this;
	bAutoKill = value;
	return this;
}
UDreamTweener* UDreamTweener::SetFrom(bool value)
{
	if (elapseTime > 0 || startToTween)return this;
	bFromMode = value;
	return this;
}
UDreamTweener* UDreamTweener::SetSpeedBased(bool value)
{
	if (elapseTime > 0 || startToTween)return this;
	bSpeedBased = value;
	return this;
}
UDreamTweener* UDreamTweener::SetAutoPlay(bool value)
{
	// Only ever the pause flag: "not playing yet" and "paused" are the same state to everything that
	// reads it, and giving a tween a second way of standing still is how the two drift apart.
	isMarkedPause = !value;
	return this;
}

void UDreamTweener::ApplySpeedBasedDuration()
{
	if (!bSpeedBased)
	{
		return;
	}
	// Once only: duration is rewritten in place, and a second pass would divide the distance by a
	// duration instead of by the speed it was.
	bSpeedBased = false;
	const float Speed = duration;
	const float Distance = GetValueDistance();
	if (!FMath::IsFinite(Speed) || !FMath::IsFinite(Distance) || Speed <= UE_SMALL_NUMBER || Distance <= UE_SMALL_NUMBER)
	{
		// No distance to cover, or no speed to cover it at. The authored duration stands, and the
		// tween types that cannot measure a distance at all (Virtual, Update, DelayFrame, Sequence)
		// land here by returning zero from GetValueDistance.
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::ApplySpeedBasedDuration] %s has no measurable distance (%.3f) or no speed (%.3f), so SetSpeedBased left its duration alone."), *GetClass()->GetName(), Distance, Speed);
		return;
	}
	duration = Distance / Speed;
}

bool UDreamTweener::ToNext(float deltaTime, float unscaledDeltaTime)
{
	// A killed tween is finished whether or not the game is paused. Answering "still running" for the
	// pause first meant anything killed during a pause stayed in the manager's list -- ticked, and kept
	// alive with everything it references -- until the game resumed, which for a paused menu is never.
	if (isMarkedToKill || IsRetired())return false;
	if (auto world = GetWorld())
	{
		if (world->IsPaused() && affectByGamePause)return true;
	}
	if (isMarkedPause)return true;//no need to tick time if pause
	// Nothing is left to play once the last cycle has been. Goto(duration) completes a tween in place and
	// the clock it leaves behind reads as the start of a next cycle: stepping on from it replayed the whole
	// animation and completed it a second time. A held tween somebody resumed is the same case.
	if (HasCompletedAllCycles())
	{
		return FinishOrHold(false);
	}
	// The tween's own time scale, on top of whichever world clock it was told to follow.
	float newElapseTime = elapseTime + (affectByTimeDilation ? deltaTime : unscaledDeltaTime) * timeScale;
	// A loop with no end has no end to its clock either: elapseTime would climb until a float can no
	// longer resolve a frame's delta, and the cycle phase drifts and then stops advancing altogether.
	// The cycles that are over are folded out of the clock here, where the clock is the tween's OWN --
	// a sequence hands its children an elapsed time it computes itself, so those are left alone -- and
	// counted in foldedCycleCount, which the cycle arithmetic subtracts back out. currentTime is
	// therefore exactly what it was before the fold; only the magnitude of the clock changes.
	if (loopType != EDreamTweenLoop::Once && maxLoopCount <= -1 && duration > 0)
	{
		const int32 pendingFoldCycleCount = loopCycleCount - foldedCycleCount;
		if (pendingFoldCycleCount > 0)
		{
			newElapseTime -= duration * pendingFoldCycleCount;
			foldedCycleCount = loopCycleCount;
		}
	}
	const bool bStillRunning = this->ToNextWithElapsedTime(newElapseTime);
	// Killed from inside its own step, by a callback of its own, is finished whatever the step was about to
	// say -- and must not be held paused at its end by an auto-kill that is off.
	if (isMarkedToKill)
	{
		return false;
	}
	return FinishOrHold(bStillRunning);
}
bool UDreamTweener::ToNextWithElapsedTime(float InElapseTime)
{
	SanitizeTiming();
	// A clock that is not a number would carry NaN into every value the tween writes; it stays where it was.
	if (!FMath::IsFinite(InElapseTime))
	{
		return true;
	}
	this->elapseTime = InElapseTime;
	if (elapseTime <= delay)
	{
		return true;//waiting delay
	}
	// Every callback below -- and a value setter, which is caller code too -- may take this tween over:
	// restart it, send it elsewhere in time, complete or kill it. When one does, what it did replaces the
	// state the rest of this step was about to finish off, so the step stops there (see clockGeneration).
	// Running on undid it: a Restart from the tween's own OnComplete was answered with "finished", the
	// manager retired the tween on the spot, and the target sat at the start value the restart had applied.
	const int32 generation = clockGeneration;
	if (!startToTween && !BeginTween())
	{
		return IsRunningAfterTakeover();
	}

	const float elapseTimeWithoutDelay = elapseTime - delay;
	float currentTime = elapseTimeWithoutDelay - duration * (loopCycleCount - foldedCycleCount);
	if (currentTime >= duration)
	{
		loopCycleCount++;
		const bool bLastCycle = loopType == EDreamTweenLoop::Once || (maxLoopCount > -1 && loopCycleCount >= maxLoopCount);

		TweenAndApplyValue(reverseTween ? 0 : duration);
		if (clockGeneration != generation)
		{
			return IsRunningAfterTakeover();
		}
		// A cycle running backwards ends where the tween began, at progress 0 -- the value just applied. It
		// used to report 1 there, so the progress of a yoyo's backward cycle ran down towards 0 and then
		// jumped to 1 on its last frame.
		onUpdateCpp.Broadcast(reverseTween ? 0.0f : 1.0f);
		if (clockGeneration != generation)
		{
			return IsRunningAfterTakeover();
		}
		onCycleCompleteCpp.Broadcast();
		if (clockGeneration != generation)
		{
			return IsRunningAfterTakeover();
		}
		if (bLastCycle)
		{
			onCompleteCpp.Broadcast();
		}
		else
		{
			onCycleStartCpp.Broadcast();//start new cycle callback
		}
		if (clockGeneration != generation)
		{
			return IsRunningAfterTakeover();
		}
		switch (loopType)
		{
		case EDreamTweenLoop::Restart:
		{
			SetValueForRestart();
		}
		break;
		case EDreamTweenLoop::Yoyo:
		{
			reverseTween = !reverseTween;
			SetValueForYoyo();
		}
		break;
		case EDreamTweenLoop::Incremental:
		{
			SetValueForIncremental();
		}
		break;
		}
		return !bLastCycle;
	}

	if (reverseTween)
	{
		currentTime = duration - currentTime;
	}
	TweenAndApplyValue(currentTime);
	if (clockGeneration != generation)
	{
		return IsRunningAfterTakeover();
	}
	onUpdateCpp.Broadcast(currentTime / duration);
	return true;
}

bool UDreamTweener::BeginTween()
{
	const int32 generation = clockGeneration;
	startToTween = true;
	//set initialize value
	OnStartGetValue();
	// From() and SetSpeedBased both need the start value first: one turns the tween around
	// between where the value is and where it was told to go, the other measures the distance
	// between exactly those two. Neither is knowable before the getter has been asked.
	if (bFromMode)
	{
		// One-shot, like the speed-based rewrite below: after the swap the stored start and
		// end ARE the from-configuration, and SetOriginValueForRestart restores exactly that.
		// Swapping again on a restart would turn the tween back the way it came.
		bFromMode = false;
		SwapStartAndEndValues();
	}
	ApplySpeedBasedDuration();
	// A speed that worked out to a duration that is not a number is caught here, before anything divides by it.
	SanitizeTiming();
	//execute callback
	onCycleStartCpp.Broadcast();
	if (clockGeneration != generation)
	{
		return false;
	}
	onStartCpp.Broadcast();
	return clockGeneration == generation;
}

bool UDreamTweener::HasCompletedAllCycles()const
{
	if (loopCycleCount <= 0)
	{
		return false;
	}
	if (loopType == EDreamTweenLoop::Once)
	{
		return true;
	}
	// The same test the end of a cycle makes: an endless loop (any count below 0) never ends, and a count of
	// 0 still plays the one cycle every loop plays.
	return maxLoopCount > -1 && loopCycleCount >= maxLoopCount;
}

bool UDreamTweener::IsRetired()const
{
	return bRetired || !IsValid(this);
}

bool UDreamTweener::IsOwnerGone()const
{
	return !IsValid(GetOuter());
}

bool UDreamTweener::IsRunningAfterTakeover()const
{
	return !isMarkedToKill && !IsRetired() && !HasCompletedAllCycles();
}

void UDreamTweener::SanitizeTiming()
{
	if (!FMath::IsFinite(delay) || delay < 0.0f)
	{
		if (!FMath::IsFinite(delay))
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SanitizeTiming] %s has a delay that is not a number; it starts without one."), *GetName());
		}
		delay = 0.0f;
	}
	if (!FMath::IsFinite(duration) || duration < 0.0f)
	{
		if (!FMath::IsFinite(duration))
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SanitizeTiming] %s has a duration that is not a number; it completes at once instead of never."), *GetName());
		}
		duration = 0.0f;
	}
}

void UDreamTweener::Kill(bool callComplete)
{
	// Once is all a kill means. A tween stays in the manager's list, killed, until the next tick drops it,
	// so it was perfectly possible to kill it again: "A's OnKill kills B, B's OnKill kills A" re-announced
	// both kills back and forth until the stack ran out, and KillIfIsTweening from an OnKill handler did the
	// same on its own. A retired tween has nothing left to kill either.
	if (isMarkedToKill || IsRetired())
	{
		return;
	}
	// The flag goes up BEFORE the callback runs. A completion handler is free to Restart this very
	// tween -- a perfectly ordinary "loop it until something says stop" -- and with the assignment
	// afterwards it landed on top of the restart and killed a tween that had just been brought back
	// to life, with nothing anywhere saying so. Restart clears the flag again, so a handler that
	// restarts wins and a handler that does not leaves the tween killed, which is what each asked for.
	isMarkedToKill = true;
	// And a step of this tween that is in progress -- this is a callback of its own -- stops where it is.
	clockGeneration++;
	if (callComplete)
	{
		onCompleteCpp.Broadcast();
	}
	// After the completion, and only if the tween is still on its way out: a completion handler that
	// restarted this very tween has un-killed it (see the flag above), and announcing a kill for a
	// tween that is running again would be a lie to everything listening.
	if (isMarkedToKill)
	{
		onKillCpp.Broadcast();
	}
}

void UDreamTweener::ForceComplete()
{
	// Already killed, or retired, means already over: completing it again fired its OnComplete a second
	// time and wrote its end value over whatever had happened to the target since.
	if (isMarkedToKill || IsRetired())
	{
		return;
	}
	// A tween that has not taken its first step has not read its start value either, and the end it would
	// be thrown to is computed from that; a From tween has not even turned around yet. It starts first.
	if (!startToTween && !BeginTween())
	{
		return;
	}
	isMarkedToKill = true;
	clockGeneration++;
	elapseTime = delay + duration;
	// The end of a cycle that is running backwards is time 0, not duration -- the same choice the
	// natural completion in ToNextWithElapsedTime makes. Completing a yoyo on its way back used to
	// throw the value to the end it had already left, the opposite of where it was heading.
	TweenAndApplyValue(reverseTween ? 0 : duration);
	onUpdateCpp.Broadcast(reverseTween ? 0.0f : 1.0f);
	onCompleteCpp.Broadcast();
	// This ends the tween as well as completing it, so the kill listeners hear it too -- unless a
	// completion handler brought it back, exactly as in Kill.
	if (isMarkedToKill)
	{
		onKillCpp.Broadcast();
	}
}

void UDreamTweener::Restart()
{
	if (IsRetired())
	{
		// A handle to a tween the manager has let go of -- it finished with auto-kill on, or was killed. There is
		// no clock left to rewind; restarting used to put the value back at the start and leave it there for good.
		if (bRetired)
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::Restart] %s was retired when it ended, so there is nothing to restart. Keep a tween with SetAutoKill(false) to restart it after it finishes."), *GetName());
		}
		return;
	}
	// Not started, nothing to rewind. A tween Goto(0) placed at its very start has started, with its clock at 0.
	if (elapseTime == 0 && !startToTween)
	{
		return;
	}
	// A step of this tween in progress -- Restart called from a callback of its own -- stops where it is, and
	// reports the tween running: see clockGeneration.
	clockGeneration++;
	isMarkedPause = false;//incase it is paused.
	// A tween that was killed is restartable again. The flag is the only thing that tells the manager
	// to drop this tween, and nothing ever cleared it, so Restart on a killed tween did all its work
	// and then had it thrown away on the next tick -- silently, since Restart reports nothing.
	isMarkedToKill = false;
	//reset parameter to initial
	loopCycleCount = 0;
	foldedCycleCount = 0;
	reverseTween = false;
	if (startToTween)
	{
		SetOriginValueForRestart();
		// And put the value back at the beginning NOW, the way a sequence does for its children
		// (UDreamTweenerSequence::SetOriginValueForRestart). Restoring only the bookkeeping leaves the
		// animated object sitting at the end value, and the fresh OnStartGetValue below would read
		// THAT back as the new origin -- a restart that interpolates from the end to the end.
		TweenAndApplyValue(0);
	}
	// Starting again is starting: OnStart and OnCycleStart belong to a restarted tween as much as to
	// a new one, and OnStartGetValue is how a tween that was told to run "from wherever the value is"
	// picks its origin up. Leaving this flag raised is what kept both from ever happening twice.
	startToTween = false;

	this->ToNextWithElapsedTime(0);
}

void UDreamTweener::Goto(float timePoint)
{
	// A killed or retired tween is one nothing will step again; seeking it would only write its values over
	// the target. And a time that is not a number is no place in it.
	if (isMarkedToKill || IsRetired() || !FMath::IsFinite(timePoint))
	{
		return;
	}
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);
	// A step of this tween in progress -- Goto called from a callback of its own -- stops where it is.
	clockGeneration++;
	//reset parameter to initial
	loopCycleCount = 0;
	foldedCycleCount = 0;
	reverseTween = false;

	if (timePoint <= 0.0f)
	{
		// The very start. The step below treats a clock exactly at the end of the delay as still waiting it
		// out, so this applied nothing, and scrubbing a paused tween back to 0 left it showing wherever it
		// had been. The start is applied here instead -- read first if the tween has not started yet, which
		// is also where a From tween turns around.
		elapseTime = delay;
		if (!startToTween && !BeginTween())
		{
			return;
		}
		TweenAndApplyValue(0.0f);
		onUpdateCpp.Broadcast(0.0f);
		return;
	}

	// timePoint is a position in the ANIMATION; elapseTime counts the delay before it. Handing the
	// raw time point over landed the tween at timePoint - delay, and for a tween whose delay is at
	// least its duration, Goto(duration) left it still waiting out the delay with nothing applied.
	const bool bStillRunning = this->ToNextWithElapsedTime(delay + timePoint);
	// Reaching the end completes the tween here and once. ToNext then sees every cycle played and reports it
	// finished rather than stepping on from a clock that reads as a new cycle; a held tween stays at its end.
	if (!bStillRunning && !isMarkedToKill)
	{
		FinishOrHold(false);
	}
}

float UDreamTweener::GetProgress()const
{
	if (elapseTime > delay)
	{
		float elapseTimeWithoutDelay = elapseTime - delay;
		float currentTime = elapseTimeWithoutDelay - duration * (loopCycleCount - foldedCycleCount);
		if (currentTime >= duration)
		{
			return 1;
		}
		else
		{
			if (reverseTween)
			{
				currentTime = duration - currentTime;
			}
			return currentTime / duration;
		}
	}
	else
	{
		return 0;
	}
}

UDreamTweener* UDreamTweener::SetTickType(EDreamTweenTickType value)
{
	if (elapseTime > 0 || startToTween)return this;
	this->tickType = value;
	return this;
}
