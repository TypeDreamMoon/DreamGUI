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
 * lines glide instead of restarting -- and at rest, which wakes the spring: a spring is held at
 * rest by default (SetAutoKill(false)) rather than retired, so the handle stays good to retarget.
 * Kill it when it is no longer wanted, or SetAutoKill(true) before it starts to retire it at rest.
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
	UDreamTweenerSpring()
	{
		// Held at rest. A spring retired the moment it settled could not be retargeted afterwards -- the
		// "SetTarget at any time" above only held while it was moving, and a retired tween's handle is gone.
		bAutoKill = false;
	}

	void SetInitialValue(const FDreamTweenFloatGetterFunction& InGetter, const FDreamTweenFloatSetterFunction& InSetter, float InTarget, const FDreamSpringParams& InParams)
	{
		Getter = InGetter;
		Setter = InSetter;
		State.Target = InTarget;
		Params = InParams;
	}

	/** Move the goal; the current velocity is kept. A spring held at rest wakes and goes to the new goal. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	UDreamTweenerSpring* SetTarget(float InTarget)
	{
		State.Target = InTarget;
		// Woken only if the new goal is not where it already rests, and only from the pause coming to rest put
		// it in -- one the caller asked for with Pause stays.
		if (bRestingHeld && !State.IsAtRest(Params))
		{
			bRestingHeld = false;
			isMarkedPause = false;
		}
		else if (bRetired)
		{
			UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenerSpring::SetTarget] %s was retired when it came to rest (SetAutoKill(true)), so nothing will move it to the new goal."), *GetName());
		}
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
	/** Came to rest and was paused there by FinishOrHold; SetTarget wakes such a spring, and only such a spring. */
	bool bRestingHeld = false;

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
		if (isMarkedToKill || IsRetired())return false;
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
		const int32 Generation = clockGeneration;
		if (!startToTween)
		{
			startToTween = true;
			OnStartGetValue();
			if (clockGeneration != Generation) return IsRunningAfterTakeover();
			onCycleStartCpp.Broadcast();
			if (clockGeneration != Generation) return IsRunningAfterTakeover();
			onStartCpp.Broadcast();
			if (clockGeneration != Generation) return IsRunningAfterTakeover();
		}
		const bool bMoving = FDreamSpring::Step(Params, State, Dt);
		Setter.ExecuteIfBound(State.Value);
		if (clockGeneration != Generation) return IsRunningAfterTakeover();
		onUpdateCpp.Broadcast(bMoving ? 0.0f : 1.0f);
		if (clockGeneration != Generation) return IsRunningAfterTakeover();
		if (!bMoving)
		{
			onCycleCompleteCpp.Broadcast();
			if (clockGeneration != Generation) return IsRunningAfterTakeover();
			onCompleteCpp.Broadcast();
			if (clockGeneration != Generation) return IsRunningAfterTakeover();
			if (isMarkedToKill)
			{
				return false;
			}
			// A completion handler that gave it a new goal woke it before it could be put down: it runs on.
			if (!State.IsAtRest(Params))
			{
				return true;
			}
			// Through FinishOrHold, as every ToNext does; see UDreamTweener::SetAutoKill. Held (the default),
			// it waits at rest, paused, for SetTarget to wake it.
			bRestingHeld = !bAutoKill;
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
		if (elapseTime == 0 || IsRetired())
		{
			return;
		}
		clockGeneration++;
		bRestingHeld = false;
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
