// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerQuaternion.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerQuaternion :public UDreamTweener
{
	GENERATED_BODY()
public:
	float startFloat = 0.0f;//b
	float changeFloat = 1.0f;//c
	FQuat startValue;
	FQuat endValue;

	FDreamTweenQuaternionGetterFunction getter;
	FDreamTweenQuaternionSetterFunction setter;

	FQuat originStartValue;

	void SetInitialValue(const FDreamTweenQuaternionGetterFunction& newGetter, const FDreamTweenQuaternionSetterFunction& newSetter, const FQuat& newEndValue, float newDuration)
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
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		float lerpValue = tweenFunc.Execute(changeFloat, startFloat, currentTime, duration);
		FQuat value = FQuat::Slerp(startValue, endValue, lerpValue);
		setter.ExecuteIfBound(value);
	}
	/**
	 * Rotations compose by multiplication, never by adding components. The sum of two unit
	 * quaternions is not a unit quaternion, and Slerp handed one interpolates along the wrong arc --
	 * at worst, subtracting two nearly opposite rotations leaves a near-zero quaternion whose
	 * normalisation is NaN. diffValue below is the rotation that carried start to end (end applied
	 * after start's inverse), and applying it again is what "one more increment" means.
	 */
	virtual void SetValueForIncremental() override
	{
		const FQuat diffValue = endValue * startValue.Inverse();
		startValue = endValue;
		endValue = (diffValue * endValue).GetNormalized();
	}
	virtual void SetOriginValueForRestart() override
	{
		// Same algebra: restoring the first start value and re-applying the same relative rotation
		// puts the end back exactly where it was, which component arithmetic only manages for
		// rotations that happen to share an axis.
		const FQuat diffValue = endValue * startValue.Inverse();
		startValue = originStartValue;
		endValue = (diffValue * originStartValue).GetNormalized();
	}
	virtual void SwapStartAndEndValues() override
	{
		Swap(startValue, endValue);
		originStartValue = startValue;
	}
	/** Degrees of rotation between the two ends, the unit a rotation speed is quoted in. */
	virtual float GetValueDistance()const override
	{
		return FMath::RadiansToDegrees(static_cast<float>(startValue.AngularDistance(endValue)));
	}
};