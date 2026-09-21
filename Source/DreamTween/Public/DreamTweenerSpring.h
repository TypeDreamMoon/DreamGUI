// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "DreamTweener.h"
#include "DreamSpring.h"
#include "DreamTween.h"
#include "Engine/World.h"
#include "DreamTweenerSpring.generated.h"

/**
 * A tween driven by a spring instead of a clock: no duration, no ease. It pulls the value toward
 * the target every tick and completes when the spring is at rest. SetTarget may be called at any
 * time, including mid-flight -- the velocity carries over, which is what makes a list of lyric
 * lines glide instead of restarting.
 */
// BlueprintType, unlike its siblings: every one of its setters below is already BlueprintCallable,
// and a spring is steered AFTER it starts (SetTarget mid-flight is the whole point of it), so a
// graph has to be able to hold one. The other tweener types are configured before they run and are
// handed back as the base UDreamTweener, which is where their blueprint surface lives.
UCLASS(BlueprintType)
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
		// A killed spring is finished whether or not the game is paused; answering the pause first left
		// anything killed during a pause in the manager's list until the game resumed. Same order as
		// UDreamTweener::ToNext, which this overrides.
		if (isMarkedToKill)return false;
		if (auto world = GetWorld())
		{
			if (world->IsPaused() && affectByGamePause)return true;
		}
		if (isMarkedPause)return true;
		const float Dt = (affectByTimeDilation ? deltaTime : unscaledDeltaTime) * timeScale;
		elapseTime += Dt;
		if (elapseTime <= delay)
		{
			return true;
		}
		if (!startToTween)
		{
			startToTween = true;
			OnStartGetValue();
			onCycleStartCpp.Broadcast();
			onStartCpp.Broadcast();
		}
		const bool bMoving = FDreamSpring::Step(Params, State, Dt);
		Setter.ExecuteIfBound(State.Value);
		onUpdateCpp.Broadcast(bMoving ? 0.0f : 1.0f);
		if (!bMoving)
		{
			onCycleCompleteCpp.Broadcast();
			onCompleteCpp.Broadcast();
			// Through FinishOrHold, as every ToNext does; see UDreamTweener::SetAutoKill.
			return FinishOrHold(false);
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
	virtual void Restart() override
	{
		// A spring has no curve to rewind to, so restarting one means letting it chase its goal again
		// from where it stands. The inherited Restart would call TweenAndApplyValue to put the value
		// back at the start of the animation, and for a spring that call means "settle at the goal" --
		// the exact opposite. The clock, the pause and the kill flag still reset, and dropping the
		// velocity is what makes the chase start over rather than continue.
		if (elapseTime == 0)
		{
			return;
		}
		isMarkedPause = false;
		isMarkedToKill = false;
		elapseTime = 0;
		loopCycleCount = 0;
		foldedCycleCount = 0;
		startToTween = false;
		State.Velocity = 0.0f;
	}
	virtual UDreamTweener* SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount = 1) override
	{
		UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenerSpring::SetLoop] A spring has no cycle to loop; ignored."));
		return this;
	}
	virtual float GetProgress() const override
	{
		return State.IsAtRest(Params) ? 1.0f : 0.0f;
	}
};
