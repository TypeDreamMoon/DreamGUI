// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerInteger.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerInteger :public UDreamTweener
{
	GENERATED_BODY()
public:
	float startFloat = 0.0f;//b
	float changeFloat = 1.0f;//c
	int startValue;
	int endValue;

	FDreamTweenIntGetterFunction getter;
	FDreamTweenIntSetterFunction setter;

	int originStartValue = 0;

	void SetInitialValue(const FDreamTweenIntGetterFunction& newGetter, const FDreamTweenIntSetterFunction& newSetter, int newEndValue, float newDuration)
	{
		this->duration = newDuration;
		this->getter = newGetter;
		this->setter = newSetter;
		this->endValue = newEndValue;
	}
protected:
	virtual void OnStartGetValue() override
	{
		if (getter.IsBound())
			this->startValue = getter.Execute();
		this->originStartValue = this->startValue;
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		auto lerpValue = tweenFunc.Execute(changeFloat, startFloat, currentTime, duration);
		setter.ExecuteIfBound(FMath::Lerp(startValue, endValue, lerpValue));
	}
	virtual void SetValueForIncremental() override
	{
		auto diffValue = endValue - startValue;
		startValue = endValue;
		endValue += diffValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		// The value this tween started from, not the frame it happens to be restarted on. The
		// GFrameNumber that used to stand here came from UDreamTweenerFrame, whose "value" IS a frame
		// number; here it made a restarted int tween jump to a six-digit number that differed every
		// time. Every other tweener in the family restores originStartValue, and so does this one.
		auto diffValue = endValue - startValue;
		startValue = originStartValue;
		endValue = originStartValue + diffValue;
	}
	virtual void SwapStartAndEndValues() override
	{
		Swap(startValue, endValue);
		originStartValue = startValue;
	}
	virtual float GetValueDistance()const override { return static_cast<float>(FMath::Abs(endValue - startValue)); }
};