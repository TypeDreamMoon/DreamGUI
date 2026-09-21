// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerRotationQuat.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerRotationQuat :public UDreamTweener
{
	GENERATED_BODY()
public:
	float startFloat = 0.0f;//b
	float changeFloat = 1.0f;//c
	FQuat startValue;
	FQuat endValue;

	bool sweep = false;
	/**
	 * The sweep result this tween writes into, owned. The pointer that used to live here came from the
	 * blueprint node's out-parameter -- a slot in the VM stack frame that is gone the moment the node
	 * returns, while the tween goes on writing a hit result through it every tick for the rest of its
	 * life. Nothing ever read that out-parameter either: the node had already returned before the first
	 * write. So the result is kept here, where it stays valid for as long as anything can write it.
	 */
	FHitResult sweepHitResult;
	ETeleportType teleportType = ETeleportType::None;

	FDreamTweenRotationQuatGetterFunction getter;
	FDreamTweenRotationQuatSetterFunction setter;

	FQuat originStartValue;

	void SetInitialValue(const FDreamTweenRotationQuatGetterFunction& newGetter, const FDreamTweenRotationQuatSetterFunction& newSetter, const FQuat& newEndValue, float newDuration, bool newSweep = false, FHitResult* newSweepHitResult = nullptr, ETeleportType newTeleportType = ETeleportType::None)
	{
		this->duration = newDuration;
		this->getter = newGetter;
		this->setter = newSetter;
		this->endValue = newEndValue;

		this->startFloat = 0.0f;
		this->changeFloat = 1.0f;

		this->sweep = newSweep;
		if (newSweepHitResult != nullptr)
		{
			this->sweepHitResult = *newSweepHitResult;
		}
		this->teleportType = newTeleportType;
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
		auto value = FQuat::Slerp(startValue, endValue, lerpValue);
		setter.ExecuteIfBound(value, sweep, sweep ? &sweepHitResult : nullptr, teleportType);
	}
	/**
	 * diffValue is the rotation that carried start to end, and it is applied on the LEFT: in UE,
	 * A * B applies B first and then A, so diff * end is "end, then the same turn again" -- the
	 * world-frame increment the enum promises. end * diff would apply the turn in end's own frame,
	 * which agrees only for rotations sharing an axis, and would disagree with the restart below.
	 */
	virtual void SetValueForIncremental() override
	{
		const FQuat diffValue = endValue * startValue.Inverse();
		startValue = endValue;
		endValue = (diffValue * endValue).GetNormalized();
	}
	virtual void SetOriginValueForRestart() override
	{
		// Quaternion algebra, not component arithmetic: subtracting two unit quaternions leaves one
		// that is not unit, Slerp then interpolates along the wrong arc, and two nearly opposite
		// rotations leave a near-zero quaternion that normalises to NaN. Restoring the first start
		// value and re-applying the same relative rotation puts the end back exactly where it was.
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