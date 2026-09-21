// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerColor.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerColor :public UDreamTweener
{
	GENERATED_BODY()
public:
	float startFloat = 0.0f;//b
	float changeFloat = 1.0f;//c
	FColor startValue;
	FColor endValue;

	FDreamTweenColorGetterFunction getter;
	FDreamTweenColorSetterFunction setter;

	FColor originStartValue;
	FColor originEndValue;

	void SetInitialValue(const FDreamTweenColorGetterFunction& newGetter, const FDreamTweenColorSetterFunction& newSetter, const FColor& newEndValue, float newDuration)
	{
		this->duration = newDuration;
		this->getter = newGetter;
		this->setter = newSetter;
		this->endValue = newEndValue;

		this->startFloat = 0.0f;
		this->changeFloat = 1.0f;
	}
protected:
	virtual void OnStartGetValue() override
	{
		if (getter.IsBound())
			this->startValue = getter.Execute();
		this->originStartValue = this->startValue;
		this->originEndValue = this->endValue;
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		float lerpValue = tweenFunc.Execute(changeFloat, startFloat, currentTime, duration);
		FColor value;
		value.R = FMath::Lerp(startValue.R, endValue.R, lerpValue);
		value.G = FMath::Lerp(startValue.G, endValue.G, lerpValue);
		value.B = FMath::Lerp(startValue.B, endValue.B, lerpValue);
		value.A = FMath::Lerp(startValue.A, endValue.A, lerpValue);
		setter.ExecuteIfBound(value);
	}
	virtual void SetValueForIncremental() override
	{
		FColor diffValue;
		diffValue.R = endValue.R - startValue.R;
		diffValue.G = endValue.G - startValue.G;
		diffValue.B = endValue.B - startValue.B;
		diffValue.A = endValue.A - startValue.A;
		startValue = endValue;
		endValue += diffValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		startValue = originStartValue;
		endValue = originEndValue;
	}
	virtual void SwapStartAndEndValues() override
	{
		Swap(startValue, endValue);
		// This tweener restores BOTH ends on a restart, so both origins have to follow the swap.
		originStartValue = startValue;
		originEndValue = endValue;
	}
	virtual float GetValueDistance()const override
	{
		// The channel that has furthest to go, in 0-255 units: a speed for a colour is a speed per
		// channel, and the tween takes as long as its longest channel needs.
		return static_cast<float>(FMath::Max(FMath::Max(
			FMath::Abs(static_cast<int32>(endValue.R) - static_cast<int32>(startValue.R)),
			FMath::Abs(static_cast<int32>(endValue.G) - static_cast<int32>(startValue.G))), FMath::Max(
			FMath::Abs(static_cast<int32>(endValue.B) - static_cast<int32>(startValue.B)),
			FMath::Abs(static_cast<int32>(endValue.A) - static_cast<int32>(startValue.A)))));
	}
};