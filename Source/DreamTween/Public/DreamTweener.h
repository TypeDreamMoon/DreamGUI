// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/HitResult.h"
#include "DreamTweener.generated.h"

DECLARE_DELEGATE_RetVal_FourParams(float, FDreamTweenFunction, float, float, float, float);

DECLARE_DELEGATE_OneParam(FDreamTweenUpdateDelegate, float);
/** What a tween holds its OnUpdate listeners in; see the callback block in UDreamTweener. */
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamTweenUpdateMulticastDelegate, float);

DECLARE_DELEGATE_RetVal(float, FDreamTweenFloatGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenFloatSetterFunction, float);

DECLARE_DELEGATE_RetVal(double, FDreamTweenDoubleGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenDoubleSetterFunction, double);

DECLARE_DELEGATE_RetVal(int, FDreamTweenIntGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenIntSetterFunction, int);

DECLARE_DELEGATE_RetVal(FVector, FDreamTweenVectorGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenVectorSetterFunction, FVector);

DECLARE_DELEGATE_RetVal(FColor, FDreamTweenColorGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenColorSetterFunction, FColor);

DECLARE_DELEGATE_RetVal(FLinearColor, FDreamTweenLinearColorGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenLinearColorSetterFunction, FLinearColor);

DECLARE_DELEGATE_RetVal(FVector2D, FDreamTweenVector2DGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenVector2DSetterFunction, FVector2D);

DECLARE_DELEGATE_RetVal(FVector4, FDreamTweenVector4GetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenVector4SetterFunction, FVector4);

DECLARE_DELEGATE_RetVal(FQuat, FDreamTweenQuaternionGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenQuaternionSetterFunction, const FQuat&);

DECLARE_DELEGATE_RetVal(FRotator, FDreamTweenRotatorGetterFunction);
DECLARE_DELEGATE_OneParam(FDreamTweenRotatorSetterFunction, FRotator);

DECLARE_DELEGATE_RetVal(FVector, FDreamTweenPositionGetterFunction);
DECLARE_DELEGATE_FourParams(FDreamTweenPositionSetterFunction, FVector, bool, FHitResult*, ETeleportType);

DECLARE_DELEGATE_RetVal(FQuat, FDreamTweenRotationQuatGetterFunction);
DECLARE_DELEGATE_FourParams(FDreamTweenRotationQuatSetterFunction, const FQuat&, bool, FHitResult*, ETeleportType);

DECLARE_DELEGATE_RetVal_OneParam(bool, FDreamTweenMaterialScalarGetterFunction, float&);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FDreamTweenMaterialScalarSetterFunction, int32, float);

DECLARE_DELEGATE_RetVal_OneParam(bool, FDreamTweenMaterialVectorGetterFunction, FLinearColor&);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FDreamTweenMaterialVectorSetterFunction, int32, const FLinearColor&);

/** simple delegate */
DECLARE_DYNAMIC_DELEGATE(FDreamTweenSimpleDynamicDelegate);
/** @param InProgress Progress of this tween, from 0 to 1 */
DECLARE_DYNAMIC_DELEGATE_OneParam(FDreamTweenFloatDynamicDelegate, float, InProgress);

/**  */
UENUM(BlueprintType, Category = DreamTween)
enum class EDreamTweenTickType:uint8
{
	PrePhysics = ETickingGroup::TG_PrePhysics UMETA(DisplayName = "Pre Physics"),
	DuringPhysics = ETickingGroup::TG_DuringPhysics UMETA(DisplayName = "During Physics"),
	PostPhysics = ETickingGroup::TG_PostPhysics UMETA(DisplayName = "Post Physics"),
	PostUpdateWork = ETickingGroup::TG_PostUpdateWork UMETA(DisplayName = "Post Update Work"),
	/** This tween will use manual tick. You need to call DreamTweenManager::ManualTick to make it work. */
	Manual,
};

/**
 * Animation curve type
 */
UENUM(BlueprintType, Category = DreamTween)
enum class EDreamTweenEase :uint8
{
	Linear,
	InQuad,
	OutQuad,
	InOutQuad,
	InCubic,
	OutCubic,
	InOutCubic,
	InQuart,
	OutQuart,
	InOutQuart,
	InSine,
	OutSine,
	InOutSine,
	InExpo,
	OutExpo,
	InOutExpo,
	InCirc,
	OutCirc,
	InOutCirc,
	InElastic,
	OutElastic,
	InOutElastic,
	InBack,
	OutBack,
	InOutBack,
	InBounce,
	OutBounce,
	InOutBounce,
	/**
	 * Use CurveFloat or RuntimeFloatCurve to animate, only range 0-1 is valid.
	 * Call SetCurveFloat or SetRuntimeCurveFloat to set the curve.
	 * Fallback to Linear if curve is not set or invalid.
	 */
	CurveFloat,
};

/**
 * Loop type
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamTweenLoop :uint8
{
	/** Play once, not loop */
	Once, 
	/** Each loop cycle restarts from beginning */
	Restart, 
	/** The tween move forward and backward at alternate cycles */
	Yoyo, 
	/** Continuously increments the tween at the end of each loop cycle (A to B, B to B+(A-B), and so on). */
	Incremental,
};

class UCurveFloat;

/** Class for manage single tween */
UCLASS(BlueprintType, Abstract)
class DREAMTWEEN_API UDreamTweener : public UObject
{
	GENERATED_BODY()

public:
	UDreamTweener();

protected:
	friend class UDreamTweenerSequence;
	/** animation duration */
	float duration = 0.0f;
	/** delay time before animation start */
	float delay = 0.0f;
	/** total elapse time, include delay */
	float elapseTime = 0.0f;
	/** loop type */
	EDreamTweenLoop loopType = EDreamTweenLoop::Once;
	/** max loop count when loop type is Restart/Yoyo/Incremental */
	int32 maxLoopCount = 0;
	/** current completed cycle count */
	int32 loopCycleCount = 0;
	/**
	 * How many of those completed cycles have had their time folded out of elapseTime. An infinite loop
	 * would otherwise run its clock up without bound, and a float that large can no longer hold a frame's
	 * delta: the cycle phase drifts and the tween eventually stops advancing at all. Whole cycles are
	 * subtracted from the clock instead, and counted here, so that the phase this counts back out of
	 * elapseTime is exact and loopCycleCount -- which callers read -- still means what it always did.
	 */
	int32 foldedCycleCount = 0;
	/** how this tween update */
	EDreamTweenTickType tickType = EDreamTweenTickType::DuringPhysics;

	/** reverse animation */
	bool reverseTween = false;
	/** if animation start play */
	bool startToTween = false;
	/** mark this tween for kill, so when the next update came, the tween will be killed */
	bool isMarkedToKill = false;
	/** mark this tween as pause, it will keep pause until call Resume() */
	bool isMarkedPause = false;
	/** will this tween be affected when GamePause? usually set to false for UI */
	bool affectByGamePause = true;
	/** will this tween use dilation-time or real-time? */
	bool affectByTimeDilation = true;
	/**
	 * Is this tween thrown away when it finishes? Off keeps it alive and paused at its end, ready for
	 * Restart or Goto -- DOTween's SetAutoKill(false), and what a tween that plays on every hover
	 * wants instead of being rebuilt each time.
	 */
	bool bAutoKill = true;
	/**
	 * Does this tween run from the target value back to the current one? DOTween's From(). Read once,
	 * at the moment the start value is taken, because "the current value" only means anything then.
	 */
	bool bFromMode = false;
	/** Is `duration` a duration, or a speed in units per second? DOTween's SetSpeedBased. */
	bool bSpeedBased = false;
	/** This tween's own multiplier on time, on top of any world dilation. DOTween's timeScale. */
	float timeScale = 1.0f;

	/**
	 * Which ease the caller asked for. Kept ALONGSIDE the bound function rather than instead of it,
	 * because CurveFloat is the one ease with no static function to bind: its curve arrives in a
	 * separate call, and only by remembering the choice can SetCurveFloat tell "the author picked
	 * CurveFloat and left the curve empty" -- which falls back to linear, loudly -- from "a caller
	 * handed over a curve this tween was never meant to use", which must leave the chosen ease alone.
	 * Defaults to the ease the constructor binds, so the two never start out disagreeing.
	 */
	EDreamTweenEase easeType = EDreamTweenEase::OutCubic;

	/** tween function */
	FDreamTweenFunction tweenFunc;
	/**
	 * The curve SetCurveFloat bound the tween function to, held for as long as this tween lives. The
	 * binding is a WEAK lambda over the curve, and every subclass calls tweenFunc.Execute unconditionally:
	 * once the curve is collected the delegate reports itself unbound, but Execute on a dead weak binding
	 * only checkSlow's before running the lambda over the freed curve. Owning a reference is what keeps
	 * the curve alive for exactly as long as something can still evaluate it.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> curveFloat = nullptr;
	/**
	 * The ExternalCurve of the FRuntimeFloatCurve SetRuntimeFloatCurve was given, held for the same
	 * reason and against the same failure: the tween function there closes over a COPY of that struct,
	 * whose TObjectPtr the collector cannot see, and GetRichCurveConst reaches straight into the asset.
	 * Null for a runtime curve that carries its keys inline, which the copy owns outright.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UCurveFloat> runtimeExternalCurve = nullptr;

	/**
	 * The five callbacks a tween offers, each holding EVERY listener that asked for it rather than
	 * only the last one. They used to be single delegates, so a second OnComplete quietly threw the
	 * first away -- and the first was often not the author's at all but a system's own bookkeeping
	 * (UDreamUIPlayTween binds four of these to drive its events), which then simply stopped
	 * happening the moment a caller bound one of its own to the tween it had been handed.
	 */
	/** call once after animation complete */
	FSimpleMulticastDelegate onCompleteCpp;
	/** if use loop, this will call every time when begin tween in every cycle */
	FSimpleMulticastDelegate onCycleStartCpp;
	/** if use loop, this will call every time after tween complete in every cycle */
	FSimpleMulticastDelegate onCycleCompleteCpp;
	/** call every frame after animation starts */
	FDreamTweenUpdateMulticastDelegate onUpdateCpp;
	/** call once when animation starts */
	FSimpleMulticastDelegate onStartCpp;
	/**
	 * call once when this tween is killed, whether or not it had finished.
	 *
	 * Separate from onComplete because the two answer different questions: "did the animation reach
	 * its end" and "is this tween over". Kill(true) fires both, Kill(false) fires only this one, and a
	 * natural completion fires only the other (an auto-killed tween is retired by the manager without
	 * ever passing through Kill). DOTween splits them the same way.
	 */
	FSimpleMulticastDelegate onKillCpp;
public:
	/**
	 * Set animation curve type.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetEase(EDreamTweenEase easetype);
	/**
	 * The curve for an ease type as a callable, so code that only needs to EVALUATE a curve can do
	 * so without constructing a tweener. SetEase uses the same mapping, which is the point: a second
	 * copy of the switch would drift, and a curve that means one thing to a tween and another to its
	 * caller is worse than having no curve choice at all.
	 * Returns an unbound delegate for CurveFloat, which carries its curve elsewhere.
	 */
	static FDreamTweenFunction GetEaseFunction(EDreamTweenEase easetype);
	/**
	 * Set delay time before start animation.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual UDreamTweener* SetDelay(float newDelay);

	/**
	 * Set loop of tween.
	 * Has no effect if the Tween has already started.
	 * @param newLoopType	loop type
	 * @param newLoopCount	number of cycles to play (-1 for infinite)
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual UDreamTweener* SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount = 1);
	/** curently completed loop cycle count */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		int32 GetLoopCycleCount()const { return loopCycleCount; }

	/** execute when animation complete */
	UDreamTweener* OnComplete(const FSimpleDelegate& newComplete)
	{
		this->onCompleteCpp.Add(newComplete);
		return this;
	}
	/** execute when animation complete */
	UDreamTweener* OnComplete(const TFunction<void()>& newComplete)
	{
		if (newComplete != nullptr)
		{
			this->onCompleteCpp.AddLambda(newComplete);
		}
		return this;
	}
	/** execute when animation complete */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnComplete(const FDreamTweenSimpleDynamicDelegate& newComplete)
	{
		this->onCompleteCpp.AddLambda([newComplete] {
			newComplete.ExecuteIfBound();
		});
		return this;
	}
	
	/** if use loop, this will call every time after tween complete in every cycle */
	UDreamTweener* OnCycleComplete(const FSimpleDelegate& newCycleComplete)
	{
		this->onCycleCompleteCpp.Add(newCycleComplete);
		return this;
	}
	/** if use loop, this will call every time after tween complete in every cycle */
	UDreamTweener* OnCycleComplete(const TFunction<void()>& newCycleComplete)
	{
		if (newCycleComplete != nullptr)
		{
			this->onCycleCompleteCpp.AddLambda(newCycleComplete);
		}
		return this;
	}
	/** if use loop, this will call every time after tween complete in every cycle */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnCycleComplete(const FDreamTweenSimpleDynamicDelegate& newCycleComplete)
	{
		this->onCycleCompleteCpp.AddLambda([newCycleComplete] {
			newCycleComplete.ExecuteIfBound();
			});
		return this;
	}

	/** if use loop, this will call every time when begin tween in every cycle */
	UDreamTweener* OnCycleStart(const FSimpleDelegate& newCycleStart)
	{
		this->onCycleStartCpp.Add(newCycleStart);
		return this;
	}
	/** if use loop, this will call every time when begin tween in every cycle */
	UDreamTweener* OnCycleStart(const TFunction<void()>& newCycleStart)
	{
		if (newCycleStart != nullptr)
		{
			this->onCycleStartCpp.AddLambda(newCycleStart);
		}
		return this;
	}
	/** if use loop, this will call every time when begin tween in every cycle */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnCycleStart(const FDreamTweenSimpleDynamicDelegate& newCycleStart)
	{
		this->onCycleStartCpp.AddLambda([newCycleStart] {
			newCycleStart.ExecuteIfBound();
			});
		return this;
	}

	/** execute every frame if animation is playing */
	UDreamTweener* OnUpdate(const FDreamTweenUpdateDelegate& newUpdate)
	{
		this->onUpdateCpp.Add(newUpdate);
		return this;
	}
	/** execute every frame if animation is playing */
	UDreamTweener* OnUpdate(const TFunction<void(float)>& newUpdate)
	{
		if (newUpdate != nullptr)
		{
			this->onUpdateCpp.AddLambda(newUpdate);
		}
		return this;
	}
	/** execute every frame if animation is playing */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnUpdate(const FDreamTweenFloatDynamicDelegate& newUpdate)
	{
		this->onUpdateCpp.AddLambda([newUpdate](float progress) {
			newUpdate.ExecuteIfBound(progress);
		});
		return this;
	}
	
	/** execute when this tween is killed, whether or not it finished; see onKillCpp */
	UDreamTweener* OnKill(const FSimpleDelegate& newKill)
	{
		this->onKillCpp.Add(newKill);
		return this;
	}
	/** execute when this tween is killed, whether or not it finished; see onKillCpp */
	UDreamTweener* OnKill(const TFunction<void()>& newKill)
	{
		if (newKill != nullptr)
		{
			this->onKillCpp.AddLambda(newKill);
		}
		return this;
	}
	/** execute when this tween is killed, whether or not it finished; see onKillCpp */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnKill(const FDreamTweenSimpleDynamicDelegate& newKill)
	{
		this->onKillCpp.AddLambda([newKill] {
			newKill.ExecuteIfBound();
		});
		return this;
	}

	/** execute when animation start*/
	UDreamTweener* OnStart(const FSimpleDelegate& newStart)
	{
		this->onStartCpp.Add(newStart);
		return this;
	}
	/** execute when animation start, blueprint version*/
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* OnStart(const FDreamTweenSimpleDynamicDelegate& newStart)
	{
		this->onStartCpp.AddLambda([newStart] {
			newStart.ExecuteIfBound();
		});
		return this;
	}
	/** execute when animation start, lambda version*/
	UDreamTweener* OnStart(const TFunction<void()>& newStart)
	{
		if (newStart != nullptr)
		{
			this->onStartCpp.AddLambda(newStart);
		}
		return this;
	}
	/**
	 * Set CurveFloat as animation curve.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetCurveFloat(UCurveFloat* newCurveFloat);
	/**
	 * Set RuntimeFloatCurve as animation curve.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
	UDreamTweener* SetRuntimeFloatCurve(const FRuntimeFloatCurve& Value);
	/**
	 * @return false: the tween is complete and need to be killed. true: the tween is still processing.
	 */
	virtual bool ToNext(float deltaTime, float unscaledDeltaTime);
	/**
	 * @return false: the tween is complete and need to be killed. true: the tween is still processing.
	 */
	bool ToNextWithElapsedTime(float InElapseTime);
	/** Force stop this animation. if callComplete = true, will call OnComplete after stop*/
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual void Kill(bool callComplete = false);
	/** Force stop this animation at this frame, set value to end, call OnComplete. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual void ForceComplete();
	/**
	 * True once Kill or ForceComplete has marked this tween. The manager drops a marked tween on the
	 * next tick it sees it on -- which is why it has to be askable without ticking: a tween set to
	 * Manual tick is seen only by ManualTick, and a killed one nobody ticks again would never leave.
	 */
	bool IsMarkedToKill()const { return isMarkedToKill; }
	/** Pause this animation. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		void Pause()
	{
		isMarkedPause = true;
	}
	/** Continue play animation if is paused. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		void Resume()
	{
		isMarkedPause = false;
	}
	/** Will this tween be affected when GamePause? Default is true, usually set to false for UI. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		bool GetAffectByGamePause()const { return affectByGamePause; }
	/** Will this tween be affected when GamePause? Default is true, usually set to false for UI. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetAffectByGamePause(bool value);
	/** will this tween use dilated-time or real-time? */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		bool GetAffectByTimeDilation()const { return affectByTimeDilation; }
	/** will this tween use dilated-time or real-time? */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetAffectByTimeDilation(bool value);
	/**
	 * This tween's own speed multiplier, on top of everything else: 2 runs it twice as fast, 0.5 half
	 * as fast, 0 holds it still. Changeable at any time, like the two switches above and unlike the
	 * setters that describe the tween's shape -- changing how fast something is running is the point.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetTimeScale(float value = 1.0f);
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		float GetTimeScale()const { return timeScale; }
	/**
	 * Keep this tween after it finishes, paused at its end, instead of retiring it.
	 *
	 * A kept tween stays in the manager's list and answers IsTweening, and Restart or Goto brings it
	 * back -- which is the point: a hover animation that plays a hundred times need not be a hundred
	 * objects. Kill still ends it at once, whatever this says.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetAutoKill(bool value = true);
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		bool GetAutoKill()const { return bAutoKill; }
	/**
	 * Run backwards from the target to wherever the value is now, instead of towards the target.
	 *
	 * The "fade in from transparent" idiom: authored as a tween TO the value it should end at, then
	 * turned around, so the end state stays written in one place. The swap happens when the tween
	 * starts, because the value it starts from is not known before then.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetFrom(bool value = true);
	/**
	 * Read `duration` as a SPEED, in value units per second, and work the real duration out from how
	 * far the value has to travel. DOTween's SetSpeedBased: what "slide in at 600 units a second"
	 * needs, so that a panel twice as far away takes twice as long instead of moving twice as fast.
	 * Tween types with no measurable distance (Virtual, Update, DelayFrame, Sequence) keep their
	 * duration and say so once.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetSpeedBased(bool value = true);
	/** Start (or resume) a tween that was paused, including one created with SetAutoPlay(false). */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		void Play() { isMarkedPause = false; }
	/**
	 * Whether this tween runs as soon as it is created. Off is a paused tween waiting for Play() --
	 * DOTween builds them that way, and it is what assembling a tween over several frames needs.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetAutoPlay(bool value);
	/**
	 * Restart animation.
	 * Has no effect if the Tween is not started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual void Restart();
	/**
	 * Send the tween to the given position in time.
	 * @param timePoint Time position to reach (if higher than the whole tween duration the tween will simply reach its end).
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual void Goto(float timePoint);
	/** Return progress 0-1 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		virtual float GetProgress()const;
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		float GetElapsedTime()const { return elapseTime; }
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		float GetDuration()const { return duration; }

	/** Return tickType of this tween, default is DuringPhysics. */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		EDreamTweenTickType GetTickType()const { return tickType; }
	/**
	 * Set TickType of this tween.
	 * Has no effect if the Tween has already started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamTween")
		UDreamTweener* SetTickType(EDreamTweenTickType value = EDreamTweenTickType::DuringPhysics);
protected:
	/**
	 * "Still running" as the manager hears it, with SetAutoKill folded in: a finished tween that is
	 * not to be killed reports itself as running and paused at its end, so it stays in the list and
	 * can be restarted. Every ToNext override ends through this rather than returning false directly.
	 */
	bool FinishOrHold(bool bStillRunning)
	{
		if (bStillRunning || bAutoKill)
		{
			return bStillRunning;
		}
		isMarkedPause = true;
		return true;
	}
	/**
	 * Swap the start and end of this tween, for SetFrom. Called once, right after the start value has
	 * been taken from the getter -- "from the target back to here" is only expressible then.
	 * The default does nothing, which is the right answer for a tween with no value (Virtual, Update).
	 */
	virtual void SwapStartAndEndValues() {}
	/**
	 * How far this tween's value travels, in whatever unit that value is measured in, for
	 * SetSpeedBased. Zero means "no measurable distance", and the duration is left alone.
	 */
	virtual float GetValueDistance()const { return 0.0f; }
	/** Turns `duration` from a speed into a duration once the distance is known; see SetSpeedBased. */
	void ApplySpeedBasedDuration();
	/** get value when start. child class must override this, check DreamTweenerFloat for reference */
	virtual void OnStartGetValue() PURE_VIRTUAL(UDreamTweener::OnStartGetValue, );
	/** set value when tweening. child class must override this, check DreamTweenerFloat for reference */
	virtual void TweenAndApplyValue(float currentTime) PURE_VIRTUAL(UDreamTweener::TweenAndApplyValue, );
	/** set start and end value if looptype is Incremental. */
	virtual void SetValueForIncremental() PURE_VIRTUAL(UDreamTweener::SetValueForIncremental, );
	/** set start and end value before the animation wan't to restart */
	virtual void SetOriginValueForRestart() PURE_VIRTUAL(UDreamTweener::SetOriginValueForRestart, );
	virtual void SetValueForYoyo() {};
	virtual void SetValueForRestart() {};
#pragma region tweenFunctions
public:
	/**
	* @param c		Change needed in value.
	* @param b		Starting value.
	* @param t		Current time (in frames or seconds).
	* @param d		Expected eaSing duration (in frames or seconds).
	* @return		The correct value.
	*/
	static float Linear(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return c*t / d + b;
	}
	static float InQuad(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		return c*t*t + b;
	}
	static float OutQuad(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		return -c*t*(t - 2) + b;
	}
	static float InOutQuad(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d * 0.5f;
		if (t < 1)
		{
			return c * 0.5f * t*t + b;
		}
		else
		{
			--t;
			return -c * 0.5f * (t*(t - 2) - 1) + b;
		}
	}
	static float InCubic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		return c*t*t*t + b;
	}
	static float OutCubic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t = t / d - 1.0f;
		return c*(t*t*t + 1) + b;
	}
	static float InOutCubic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d * 0.5f;
		if (t < 1)
		{
			return c * 0.5f * t*t*t + b;
		}
		else
		{
			t -= 2;
			return c * 0.5f * (t*t*t + 2) + b;
		}
	}
	static float InQuart(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		return c*t*t*t*t + b;
	}
	static float OutQuart(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t = t / d - 1.0f;
		return -c * (t*t*t*t - 1) + b;
	}
	static float InOutQuart(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d * 0.5f;
		if (t < 1)
		{
			return c * 0.5f * t*t*t*t + b;
		}
		else
		{
			t -= 2;
			return -c * 0.5f * (t*t*t*t - 2) + b;
		}
	}
	static float InSine(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return -c * FMath::Cos(t / d * HALF_PI) + c + b;
	}
	static float OutSine(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return c * FMath::Sin(t / d * HALF_PI) + b;
	}
	static float InOutSine(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return -c * 0.5f * (FMath::Cos(PI * t / d) - 1) + b;
	}
	static float InExpo(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return (t == 0.0f) ? b : c * FMath::Pow(2.0f, 10.0f * (t / d - 1.0f)) + b - c * 0.001f;
	}
	static float OutExpo(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return (t == d) ? b + c : c * 1.001f * (-FMath::Pow(2.0f, -10.0f * t / d) + 1.0f) + b;
	}
	static float InOutExpo(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t == 0) return b;
		if (t == d) return b + c;
		t /= d * 0.5f;
		if (t < 1.0f)
		{
			return c * 0.5f * FMath::Pow(2.0f, 10.0f * (t - 1.0f)) + b - c * 0.0005f;
		}
		else
		{
			return c * 0.5f * 1.0005f * (-FMath::Pow(2.0f, -10.0f * (t - 1.0f)) + 2.0f) + b;
		}
	}
	static float InCirc(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		return -c * (FMath::Sqrt(1.0f - t*t) - 1.0f) + b;
	}
	static float OutCirc(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t = t / d - 1.0f;
		return c * FMath::Sqrt(1.0f - t*t) + b;
	}
	static float InOutCirc(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d * 0.5f;
		if (t < 1)
		{
			return -c * 0.5f * (FMath::Sqrt(1 - t * t) - 1) + b;
		}
		else
		{
			t -= 2.0f;
			return c * 0.5f * (FMath::Sqrt(1.0f - t*t) + 1.0f) + b;
		}
	}
	static float InElastic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t == 0) return b;
		t /= d;
		if (t == 1) return b + c;
		float p = d*0.3f;
		float s = p / 4;
		float a = c;
		t -= 1.0f;
		return -(a*FMath::Pow(2.0f, 10.0f * t) * FMath::Sin((t*d - s)*(2.0f * PI) / p)) + b;
	}
	static float OutElastic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t == 0) return b;
		t /= d;
		if (t == 1) return b + c;
		float p = d*0.3f;
		float s = p / 4;
		float a = c;
		return (a*FMath::Pow(2.0f, -10.0f * t) * FMath::Sin((t*d - s)*(2.0f * PI) / p) + c + b);
	}
	static float InOutElastic(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t == 0) return b;
		t /= d * 0.5f;
		if (t == 2) return b + c;
		float p = d*0.3f;
		float s = p / 4;
		float a = c;
		if (t < 1.0f)
		{
			t -= 1.0f;
			return -0.5f*(a*FMath::Pow(2.0f, 10.0f * t) * FMath::Sin((t*d - s)*(2.0f * PI) / p)) + b;
		}
		else
		{
			t -= 1.0f;
			return a * FMath::Pow(2.0f, -10.0f * t) * FMath::Sin((t*d - s)*(2.0f * PI) / p)*0.5f + c + b;
		}
	}
	static float InBack(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		static float s = 1.70158f;
		t /= d;
		return c*t*t*((s + 1)*t - s) + b;
	}
	static float OutBack(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		static float s = 1.70158f;
		t = t / d - 1;
		return c*(t*t*((s + 1)*t + s) + 1) + b;
	}
	static float InOutBack(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t < d * 0.5f) return InBack(c, 0, t * 2, d) * .5f + b;
		else return OutBack(c, 0, t * 2 - d, d) * .5f + c * .5f + b;
	}

	static float OutBounce(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		t /= d;
		if (t < (1.0f / 2.75f)) {
			return c*(7.5625f*t*t) + b;
		}
		else if (t < (2.0f / 2.75f)) {
			t -= (1.5f / 2.75f);
			return c*(7.5625f*t*t + .75f) + b;
		}
		else if (t < (2.5f / 2.75f)) {
			t -= (2.25f / 2.75f);
			return c*(7.5625f*t*t + .9375f) + b;
		}
		else {
			t -= (2.625f / 2.75f);
			return c*(7.5625f*t*t + .984375f) + b;
		}
	}
	static float InBounce(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		return c - OutBounce(c, 0, d - t, d) + b;
	}
	static float InOutBounce(float c, float b, float t, float d)
	{
		if (d < KINDA_SMALL_NUMBER)return c + b;
		if (t < d * 0.5f) return InBounce(c, 0, t * 2, d) * .5f + b;
		else return OutBounce(c, 0, t * 2 - d, d) * .5f + c * .5f + b;
	}
#pragma endregion
};