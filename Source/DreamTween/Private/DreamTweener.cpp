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
	this->delay = newDelay;
	if (this->delay < 0)
	{
		this->delay = 0;
	}
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
	// Hold the curve: the binding below is weak, but the tweener subclasses evaluate tweenFunc without
	// asking whether it is still bound, so a collected curve would be read through a dangling capture.
	curveFloat = newCurveFloat;
	tweenFunc.BindWeakLambda(newCurveFloat, [newCurveFloat](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (!IsValid(newCurveFloat))return Linear(c, b, t, d);
		return newCurveFloat->GetFloatValue(t / d) * c + b;
	});
	return this;
}

UDreamTweener* UDreamTweener::SetRuntimeFloatCurve(const FRuntimeFloatCurve& Value)
{
	if (elapseTime > 0 || startToTween)return this;
	// The asset a runtime curve may point at, held for as long as this tween lives -- the same reason
	// SetCurveFloat holds curveFloat. The lambda below carries a COPY of the struct, and a TObjectPtr
	// inside a lambda capture is invisible to the collector. Inline curve data (EditorCurveData) is
	// owned by that copy and safe either way; an ExternalCurve became a read through freed memory as
	// soon as the asset was collected, since GetRichCurveConst hands back a pointer into it.
	runtimeExternalCurve = Value.ExternalCurve;
	// Weak to this tween, as SetCurveFloat is weak to its curve: an evaluation can only ever reach a
	// tween that is still alive, and BindLambda kept no such guarantee.
	tweenFunc.BindWeakLambda(this, [Value](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (const FRichCurve* RichCurve = Value.GetRichCurveConst())
		{
			return RichCurve->Eval(t / d) * c + b;
		}
		return Linear(c, b, t, d);
	});
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
	if (Speed <= UE_SMALL_NUMBER || Distance <= UE_SMALL_NUMBER)
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
	if (isMarkedToKill)return false;
	if (auto world = GetWorld())
	{
		if (world->IsPaused() && affectByGamePause)return true;
	}
	if (isMarkedPause)return true;//no need to tick time if pause
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
	return FinishOrHold(this->ToNextWithElapsedTime(newElapseTime));
}
bool UDreamTweener::ToNextWithElapsedTime(float InElapseTime)
{
	this->elapseTime = InElapseTime;
	if (elapseTime > delay)//if elapseTime bigger than delay, do animation
	{
		if (!startToTween)
		{
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
			//execute callback
			onCycleStartCpp.Broadcast();
			onStartCpp.Broadcast();
		}

		float elapseTimeWithoutDelay = elapseTime - delay;
		float currentTime = elapseTimeWithoutDelay - duration * (loopCycleCount - foldedCycleCount);
		if (currentTime >= duration)
		{
			bool returnValue = true;
			loopCycleCount++;

			TweenAndApplyValue(reverseTween ? 0 : duration);
			onUpdateCpp.Broadcast(1.0f);
			onCycleCompleteCpp.Broadcast();
			if (loopType == EDreamTweenLoop::Once)
			{
				onCompleteCpp.Broadcast();
				returnValue = false;
			}
			else if (maxLoopCount <= -1)//infinite loop
			{
				onCycleStartCpp.Broadcast();//start new cycle callback
				returnValue = true;
			}
			else
			{
				if (loopCycleCount >= maxLoopCount)//reach end cycle
				{
					onCompleteCpp.Broadcast();
					returnValue = false;
				}
				else//not reach end cycle
				{
					onCycleStartCpp.Broadcast();//start new cycle callback
					returnValue = true;
				}
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
			return returnValue;
		}
		else
		{
			if (reverseTween)
			{
				currentTime = duration - currentTime;
			}
			TweenAndApplyValue(currentTime);
			onUpdateCpp.Broadcast(currentTime / duration);
			return true;
		}
	}
	else
	{
		return true;//waiting delay
	}
}

void UDreamTweener::Kill(bool callComplete)
{
	// The flag goes up BEFORE the callback runs. A completion handler is free to Restart this very
	// tween -- a perfectly ordinary "loop it until something says stop" -- and with the assignment
	// afterwards it landed on top of the restart and killed a tween that had just been brought back
	// to life, with nothing anywhere saying so. Restart clears the flag again, so a handler that
	// restarts wins and a handler that does not leaves the tween killed, which is what each asked for.
	isMarkedToKill = true;
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
	isMarkedToKill = true;
	elapseTime = delay + duration;
	// The end of a cycle that is running backwards is time 0, not duration -- the same choice the
	// natural completion in ToNextWithElapsedTime makes. Completing a yoyo on its way back used to
	// throw the value to the end it had already left, the opposite of where it was heading.
	TweenAndApplyValue(reverseTween ? 0 : duration);
	onUpdateCpp.Broadcast(1.0f);
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
	if (elapseTime == 0)
	{
		return;
	}
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
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);
	//reset parameter to initial
	loopCycleCount = 0;
	foldedCycleCount = 0;
	reverseTween = false;

	// timePoint is a position in the ANIMATION; elapseTime counts the delay before it. Handing the
	// raw time point over landed the tween at timePoint - delay, and for a tween whose delay is at
	// least its duration, Goto(duration) left it still waiting out the delay with nothing applied.
	this->ToNextWithElapsedTime(delay + timePoint);
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
