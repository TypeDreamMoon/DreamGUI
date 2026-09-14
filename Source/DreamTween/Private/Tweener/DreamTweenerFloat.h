// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerFloat.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerFloat:public UDreamTweener
{
	GENERATED_BODY()
public:
	float startValue = 0.0f;//b
	float changeValue = 0.0f;//c
	float endValue = 0.0f;

	FDreamTweenFloatGetterFunction getter;
	FDreamTweenFloatSetterFunction setter;

	float originStartValue = 0.0f;

	void SetInitialValue(const FDreamTweenFloatGetterFunction& newGetter, const FDreamTweenFloatSetterFunction& newSetter, float newEndValue, float newDuration)
	{
		this->duration = newDuration;
		this->endValue = newEndValue;
		this->getter = newGetter;
		this->setter = newSetter;
	}
protected:
	virtual void OnStartGetValue() override
	{
		if (getter.IsBound())
			this->startValue = getter.Execute();
		this->originStartValue = this->startValue;
		this->changeValue = endValue - startValue;
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		auto value = tweenFunc.Execute(changeValue, startValue, currentTime, duration);
		setter.ExecuteIfBound(value);
	}
	virtual void SetValueForIncremental() override
	{
		startValue = endValue;
		endValue += changeValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		startValue = originStartValue;
		endValue = startValue + changeValue;
	}
	virtual void SwapStartAndEndValues() override
	{
		Swap(startValue, endValue);
		// The origin is what a restart restores, so it has to follow the swap; changeValue is derived
		// and would otherwise still describe the direction the tween was authored in.
		originStartValue = startValue;
		changeValue = endValue - startValue;
	}
	virtual float GetValueDistance()const override { return FMath::Abs(endValue - startValue); }
};