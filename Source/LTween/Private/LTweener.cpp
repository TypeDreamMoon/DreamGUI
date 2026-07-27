// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LTweener.h"
#include "Curves/CurveFloat.h"
#include "LTween.h"
#include "Engine/World.h"

ULTweener::ULTweener()
{
	tweenFunc.BindStatic(&ULTweener::OutCubic);//OutCubic default animation curve function
}
FLTweenFunction ULTweener::GetEaseFunction(ELTweenEase easetype)
{
	FLTweenFunction Result;
	switch (easetype)
	{
	case ELTweenEase::Linear:
		Result.BindStatic(&ULTweener::Linear);
		break;
	case ELTweenEase::InQuad:
		Result.BindStatic(&ULTweener::InQuad);
		break;
	case ELTweenEase::OutQuad:
		Result.BindStatic(&ULTweener::OutQuad);
		break;
	case ELTweenEase::InOutQuad:
		Result.BindStatic(&ULTweener::InOutQuad);
		break;
	case ELTweenEase::InCubic:
		Result.BindStatic(&ULTweener::InCubic);
		break;
	case ELTweenEase::OutCubic:
		Result.BindStatic(&ULTweener::OutCubic);
		break;
	case ELTweenEase::InOutCubic:
		Result.BindStatic(&ULTweener::InOutCubic);
		break;
	case ELTweenEase::InQuart:
		Result.BindStatic(&ULTweener::InQuart);
		break;
	case ELTweenEase::OutQuart:
		Result.BindStatic(&ULTweener::OutQuart);
		break;
	case ELTweenEase::InOutQuart:
		Result.BindStatic(&ULTweener::InOutQuart);
		break;
	case ELTweenEase::InSine:
		Result.BindStatic(&ULTweener::InSine);
		break;
	case ELTweenEase::OutSine:
		Result.BindStatic(&ULTweener::OutSine);
		break;
	case ELTweenEase::InOutSine:
		Result.BindStatic(&ULTweener::InOutSine);
		break;
	case ELTweenEase::InExpo:
		Result.BindStatic(&ULTweener::InExpo);
		break;
	case ELTweenEase::OutExpo:
		Result.BindStatic(&ULTweener::OutExpo);
		break;
	case ELTweenEase::InOutExpo:
		Result.BindStatic(&ULTweener::InOutExpo);
		break;
	case ELTweenEase::InCirc:
		Result.BindStatic(&ULTweener::InCirc);
		break;
	case ELTweenEase::OutCirc:
		Result.BindStatic(&ULTweener::OutCirc);
		break;
	case ELTweenEase::InOutCirc:
		Result.BindStatic(&ULTweener::InOutCirc);
		break;
	case ELTweenEase::InElastic:
		Result.BindStatic(&ULTweener::InElastic);
		break;
	case ELTweenEase::OutElastic:
		Result.BindStatic(&ULTweener::OutElastic);
		break;
	case ELTweenEase::InOutElastic:
		Result.BindStatic(&ULTweener::InOutElastic);
		break;
	case ELTweenEase::InBack:
		Result.BindStatic(&ULTweener::InBack);
		break;
	case ELTweenEase::OutBack:
		Result.BindStatic(&ULTweener::OutBack);
		break;
	case ELTweenEase::InOutBack:
		Result.BindStatic(&ULTweener::InOutBack);
		break;
	case ELTweenEase::InBounce:
		Result.BindStatic(&ULTweener::InBounce);
		break;
	case ELTweenEase::OutBounce:
		Result.BindStatic(&ULTweener::OutBounce);
		break;
	case ELTweenEase::InOutBounce:
		Result.BindStatic(&ULTweener::InOutBounce);
		break;
	}
	return Result;
}
ULTweener* ULTweener::SetEase(ELTweenEase easetype)
{
	if (elapseTime > 0 || startToTween)return this;
	// CurveFloat has no static curve of its own -- SetCurveFloat binds that -- so an unbound result
	// must leave whatever is already bound alone, exactly as the original switch did by omitting it.
	FLTweenFunction Func = GetEaseFunction(easetype);
	if (Func.IsBound())
	{
		tweenFunc = Func;
	}
	return this;
}
ULTweener* ULTweener::SetDelay(float newDelay)
{
	if (elapseTime > 0 || startToTween)return this;
	this->delay = newDelay;
	if (this->delay < 0)
	{
		this->delay = 0;
	}
	return this;
}
ULTweener* ULTweener::SetLoop(ELTweenLoop newLoopType, int32 newLoopCount)
{
	if (elapseTime > 0 || startToTween)return this;
	this->loopType = newLoopType;
	this->maxLoopCount = newLoopCount;
	return this;
}

ULTweener* ULTweener::SetCurveFloat(UCurveFloat* newCurveFloat)
{
	if (elapseTime > 0 || startToTween)return this;
	tweenFunc.BindWeakLambda(newCurveFloat, [=](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (newCurveFloat)
		{
			return newCurveFloat->GetFloatValue(t / d) * c + b;
		}
		else
		{
			UE_LOG(LTween, Warning, TEXT("[ULTweener::SetCurveFloat] CurveFloat not valid! Fallback to linear. You should always call SetCurveFloat(and pass a valid curve) if set Easetype to CurveFloat."));
			return Linear(c, b, t, d);
		}
	});
	return this;
}

ULTweener* ULTweener::SetRuntimeFloatCurve(const FRuntimeFloatCurve& Value)
{
	if (elapseTime > 0 || startToTween)return this;
	tweenFunc.BindLambda([=](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (const FRichCurve* RichCurve = Value.GetRichCurveConst())
		{
			return RichCurve->Eval(t / d) * c + b;
		}
		return Linear(c, b, t, d);
	});
	return this;
}

ULTweener* ULTweener::SetAffectByGamePause(bool value)
{
	affectByGamePause = value;
	return this;
}
ULTweener* ULTweener::SetAffectByTimeDilation(bool value)
{
	affectByTimeDilation = value;
	return this;
}

bool ULTweener::ToNext(float deltaTime, float unscaledDeltaTime)
{
	if (auto world = GetWorld())
	{
		if (world->IsPaused() && affectByGamePause)return true;
	}
	if (isMarkedToKill)return false;
	if (isMarkedPause)return true;//no need to tick time if pause
	return this->ToNextWithElapsedTime(elapseTime + (affectByTimeDilation ? deltaTime : unscaledDeltaTime));
}
bool ULTweener::ToNextWithElapsedTime(float InElapseTime)
{
	this->elapseTime = InElapseTime;
	if (elapseTime > delay)//if elapseTime bigger than delay, do animation
	{
		if (!startToTween)
		{
			startToTween = true;
			//set initialize value
			OnStartGetValue();
			//execute callback
			onCycleStartCpp.ExecuteIfBound();
			onStartCpp.ExecuteIfBound();
		}

		float elapseTimeWithoutDelay = elapseTime - delay;
		float currentTime = elapseTimeWithoutDelay - duration * loopCycleCount;
		if (currentTime >= duration)
		{
			bool returnValue = true;
			loopCycleCount++;

			TweenAndApplyValue(reverseTween ? 0 : duration);
			onUpdateCpp.ExecuteIfBound(1.0f);
			onCycleCompleteCpp.ExecuteIfBound();
			if (loopType == ELTweenLoop::Once)
			{
				onCompleteCpp.ExecuteIfBound();
				returnValue = false;
			}
			else if (maxLoopCount <= -1)//infinite loop
			{
				onCycleStartCpp.ExecuteIfBound();//start new cycle callback
				returnValue = true;
			}
			else
			{
				if (loopCycleCount >= maxLoopCount)//reach end cycle
				{
					onCompleteCpp.ExecuteIfBound();
					returnValue = false;
				}
				else//not reach end cycle
				{
					onCycleStartCpp.ExecuteIfBound();//start new cycle callback
					returnValue = true;
				}
			}
			switch (loopType)
			{
			case ELTweenLoop::Restart:
			{
				SetValueForRestart();
			}
			break;
			case ELTweenLoop::Yoyo:
			{
				reverseTween = !reverseTween;
				SetValueForYoyo();
			}
			break;
			case ELTweenLoop::Incremental:
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
			onUpdateCpp.ExecuteIfBound(currentTime / duration);
			return true;
		}
	}
	else
	{
		return true;//waiting delay
	}
}

void ULTweener::Kill(bool callComplete)
{
	if (callComplete)
	{
		onCompleteCpp.ExecuteIfBound();
	}
	isMarkedToKill = true;
}

void ULTweener::ForceComplete()
{
	isMarkedToKill = true;
	elapseTime = delay + duration;
	TweenAndApplyValue(duration);
	onUpdateCpp.ExecuteIfBound(1.0f);
	onCompleteCpp.ExecuteIfBound();
}

void ULTweener::Restart()
{
	if (elapseTime == 0)
	{
		return;
	}
	isMarkedPause = false;//incase it is paused.
	//reset parameter to initial
	loopCycleCount = 0;
	reverseTween = false;
	SetOriginValueForRestart();

	this->ToNextWithElapsedTime(0);
}

void ULTweener::Goto(float timePoint)
{
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);
	//reset parameter to initial
	loopCycleCount = 0;
	reverseTween = false;

	this->ToNextWithElapsedTime(timePoint);
}

float ULTweener::GetProgress()const
{
	if (elapseTime > delay)
	{
		float elapseTimeWithoutDelay = elapseTime - delay;
		float currentTime = elapseTimeWithoutDelay - duration * loopCycleCount;
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

ULTweener* ULTweener::SetTickType(ELTweenTickType value)
{
	if (elapseTime > 0 || startToTween)return this;
	this->tickType = value;
	return this;
}
