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
	tweenFunc.BindWeakLambda(newCurveFloat, [=](float c, float b, float t, float d) {
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (newCurveFloat)
		{
			return newCurveFloat->GetFloatValue(t / d) * c + b;
		}
		else
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweener::SetCurveFloat] CurveFloat not valid! Fallback to linear. You should always call SetCurveFloat(and pass a valid curve) if set Easetype to CurveFloat."));
			return Linear(c, b, t, d);
		}
	});
	return this;
}

UDreamTweener* UDreamTweener::SetRuntimeFloatCurve(const FRuntimeFloatCurve& Value)
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

bool UDreamTweener::ToNext(float deltaTime, float unscaledDeltaTime)
{
	if (auto world = GetWorld())
	{
		if (world->IsPaused() && affectByGamePause)return true;
	}
	if (isMarkedToKill)return false;
	if (isMarkedPause)return true;//no need to tick time if pause
	return this->ToNextWithElapsedTime(elapseTime + (affectByTimeDilation ? deltaTime : unscaledDeltaTime));
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
			if (loopType == EDreamTweenLoop::Once)
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
			onUpdateCpp.ExecuteIfBound(currentTime / duration);
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
	if (callComplete)
	{
		onCompleteCpp.ExecuteIfBound();
	}
	isMarkedToKill = true;
}

void UDreamTweener::ForceComplete()
{
	isMarkedToKill = true;
	elapseTime = delay + duration;
	TweenAndApplyValue(duration);
	onUpdateCpp.ExecuteIfBound(1.0f);
	onCompleteCpp.ExecuteIfBound();
}

void UDreamTweener::Restart()
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

void UDreamTweener::Goto(float timePoint)
{
	timePoint = FMath::Clamp(timePoint, 0.0f, duration);
	//reset parameter to initial
	loopCycleCount = 0;
	reverseTween = false;

	this->ToNextWithElapsedTime(timePoint);
}

float UDreamTweener::GetProgress()const
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

UDreamTweener* UDreamTweener::SetTickType(EDreamTweenTickType value)
{
	if (elapseTime > 0 || startToTween)return this;
	this->tickType = value;
	return this;
}
