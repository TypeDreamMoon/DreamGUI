// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "DreamTweener.h"
#include "DreamSpring.h"
#include "Engine/World.h"
#include "DreamTweenerSpring.generated.h"

/**
 * A tween driven by a spring instead of a clock: no duration, no ease. It pulls the value toward
 * the target every tick and completes when the spring is at rest. SetTarget may be called at any
 * time, including mid-flight -- the velocity carries over, which is what makes a list of lyric
 * lines glide instead of restarting.
 */
UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerSpring : public UDreamTweener
{
	GENERATED_BODY()
public:
	void SetInitialValue(const FDreamTweenFloatGetterFunction& InGetter, const FDreamTweenFloatSetterFunction& InSetter, float InTarget, const FDreamSpringParams& InParams)
	{
		Getter = InGetter;
		Setter = InSetter;
		State.Target = InTarget;
		Params = InParams;
	}

	/** Move the goal; the current velocity is kept. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	UDreamTweenerSpring* SetTarget(float InTarget)
	{
		State.Target = InTarget;
		return this;
	}
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	UDreamTweenerSpring* SetVelocity(float InVelocity)
	{
		State.Velocity = InVelocity;
		return this;
	}
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	UDreamTweenerSpring* SetSpringParams(const FDreamSpringParams& InParams)
	{
		Params = InParams;
		return this;
	}
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	float GetTarget() const { return State.Target; }
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	float GetVelocity() const { return State.Velocity; }
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	float GetValue() const { return State.Value; }
	const FDreamSpringState& GetState() const { return State; }

protected:
	FDreamTweenFloatGetterFunction Getter;
	FDreamTweenFloatSetterFunction Setter;
	FDreamSpringParams Params;
	FDreamSpringState State;

	virtual void OnStartGetValue() override
	{
		if (Getter.IsBound())
		{
			State.Value = Getter.Execute();
		}
	}
	virtual bool ToNext(float deltaTime, float unscaledDeltaTime) override
	{
		if (auto world = GetWorld())
		{
			if (world->IsPaused() && affectByGamePause)return true;
		}
		if (isMarkedToKill)return false;
		if (isMarkedPause)return true;
		const float Dt = affectByTimeDilation ? deltaTime : unscaledDeltaTime;
		elapseTime += Dt;
		if (elapseTime <= delay)
		{
			return true;
		}
		if (!startToTween)
		{
			startToTween = true;
			OnStartGetValue();
			onCycleStartCpp.ExecuteIfBound();
			onStartCpp.ExecuteIfBound();
		}
		const bool bMoving = FDreamSpring::Step(Params, State, Dt);
		Setter.ExecuteIfBound(State.Value);
		onUpdateCpp.ExecuteIfBound(bMoving ? 0.0f : 1.0f);
		if (!bMoving)
		{
			onCycleCompleteCpp.ExecuteIfBound();
			onCompleteCpp.ExecuteIfBound();
			return false;
		}
		return true;
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		// Used by ForceComplete: jump to the goal.
		State.Settle();
		Setter.ExecuteIfBound(State.Value);
	}
	virtual void SetValueForIncremental() override {}
	virtual void SetOriginValueForRestart() override {}
	virtual UDreamTweener* SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount = 1) override
	{
		UE_LOG(LogTemp, Warning, TEXT("[UDreamTweenerSpring::SetLoop] A spring has no cycle to loop; ignored."));
		return this;
	}
	virtual float GetProgress() const override
	{
		return State.IsAtRest(Params) ? 1.0f : 0.0f;
	}
};
