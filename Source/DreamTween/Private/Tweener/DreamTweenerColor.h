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
	/**
	 * One channel, interpolated in float and rounded back into a byte. The eases that overshoot -- OutBack
	 * peaks at 1.1, the elastic ones further -- carry the value past either end of 0..255, and FMath::Lerp
	 * on two bytes cast that straight back into one: 0 to 255 under OutBack reached 280.5 and came out as
	 * 24, a white that flashed to near black for a moment. Held at the end of the range instead.
	 */
	static uint8 LerpChannel(uint8 From, uint8 To, float Alpha)
	{
		const float Value = FMath::Lerp(static_cast<float>(From), static_cast<float>(To), Alpha);
		return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Value), 0, 255));
	}
	/** A step from one byte to another, carried on past To and held inside 0..255; see SetValueForIncremental. */
	static uint8 StepChannel(uint8 From, uint8 To)
	{
		return static_cast<uint8>(FMath::Clamp(2 * static_cast<int32>(To) - static_cast<int32>(From), 0, 255));
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		float lerpValue = tweenFunc.Execute(changeFloat, startFloat, currentTime, duration);
		FColor value;
		value.R = LerpChannel(startValue.R, endValue.R, lerpValue);
		value.G = LerpChannel(startValue.G, endValue.G, lerpValue);
		value.B = LerpChannel(startValue.B, endValue.B, lerpValue);
		value.A = LerpChannel(startValue.A, endValue.A, lerpValue);
		setter.ExecuteIfBound(value);
	}
	virtual void SetValueForIncremental() override
	{
		// The step is signed. Taken as bytes it wrapped whenever a channel went down (200 to 100 is a step
		// of 156, not -100), and FColor's += then saturated the wrapped step upwards: a fade-out loop that
		// brightened on its second cycle.
		FColor nextEndValue;
		nextEndValue.R = StepChannel(startValue.R, endValue.R);
		nextEndValue.G = StepChannel(startValue.G, endValue.G);
		nextEndValue.B = StepChannel(startValue.B, endValue.B);
		nextEndValue.A = StepChannel(startValue.A, endValue.A);
		startValue = endValue;
		endValue = nextEndValue;
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