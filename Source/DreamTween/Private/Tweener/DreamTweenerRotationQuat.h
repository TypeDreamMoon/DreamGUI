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
	virtual void SetValueForIncremental() override
	{
		auto diffValue = endValue * startValue.Inverse();
		startValue = endValue;
		endValue = endValue * diffValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		auto diffValue = endValue - startValue;
		startValue = originStartValue;
		endValue = originStartValue + diffValue;
	}
};