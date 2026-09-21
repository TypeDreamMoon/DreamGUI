// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "Event/DreamUIEventDelegate.h"
#include "DreamUIPlayTween.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDreamUIPlayTweenCompleteDynamicDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDreamUIPlayTweenCycleCompleteDynamicDelegate, int32, InCycleCompleteCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDreamUIPlayTweenStartDynamicDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDreamUIPlayTweenUpdateProgressDynamicDelegate, float, InProgress);

UCLASS(BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamUIPlayTween : public UObject
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, Category = "Property")
		EDreamTweenLoop LoopType = EDreamTweenLoop::Once;
	/**
	 * Number of cycles to play; -1 for infinite.
	 *
	 * One, not infinite. The default used to be -1, which is invisible until the author picks a loop
	 * type -- and then the tween never ends: it is never taken out of the manager, it holds this play
	 * tween and the widget behind it for the rest of the run, a sequence component waiting on its
	 * OnComplete never advances past it, and a sequence refuses it outright
	 * (UDreamTweenerSequence::Insert). "Loop forever" is a thing to ask for, not a thing to get by
	 * not asking. Assets saved before this change that left the field alone come back as 1.
	 */
	UPROPERTY(EditAnywhere, Category = "Property", meta = (EditCondition = "LoopType != EDreamTweenLoop::Once"))
		int32 LoopCount = 1;
	UPROPERTY(EditAnywhere, Category = "Property")
		EDreamTweenEase EaseType = EDreamTweenEase::Linear;
	/** only valid if easeType=CurveFloat */
	UPROPERTY(EditAnywhere, Category = "Property", meta=(EditCondition = "EaseType == EDreamTweenEase::CurveFloat"))
		TObjectPtr<UCurveFloat> EaseCurve;
	UPROPERTY(EditAnywhere, Category = "Property")
		float Duration = 0.5f;
	UPROPERTY(EditAnywhere, Category = "Property")
		float StartDelay = 0.0f;
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnStart = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);
	/** parameter float is the progress in range 0-1, not affected by ease type (linear on time) */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnUpdateProgress = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Float);
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnComplete = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);
	/** if LoopType is not Once, then this will be called every time when the cycle end, with parameter "cycle complete count". */
	UPROPERTY(EditAnywhere, Category = "Event")
		FDreamUIEventDelegate OnCycleComplete = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Int32);
	UPROPERTY(EditAnywhere, Category = "Property")
		bool bAffectByGamePause = false;
	UPROPERTY(EditAnywhere, Category = "Property")
		bool bAffectByTimeDilation = false;
	UPROPERTY(Transient)
		TObjectPtr<UDreamTweener> Tweener;
	DECLARE_EVENT(UDreamUIPlayTween, FOnStartEvent);
	DECLARE_EVENT_OneParam(UDreamUIPlayTween, FOnUpdateProgressEvent, float);
	DECLARE_EVENT(UDreamUIPlayTween, FOnCompleteEvent);
	DECLARE_EVENT_OneParam(UDreamUIPlayTween, FOnCycleCompleteEvent, int32);
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Start();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void Stop();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamTweener* GetTweener()const { return Tweener; }

	FOnStartEvent OnStartCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnStart"))
	FOnDreamUIPlayTweenStartDynamicDelegate OnStartBP;

	FOnUpdateProgressEvent OnUpdateProgressCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnUpdateProgress"))
	FOnDreamUIPlayTweenUpdateProgressDynamicDelegate OnUpdateProgressBP;
	
	FOnCompleteEvent OnCompleteCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnComplete"))
	FOnDreamUIPlayTweenCompleteDynamicDelegate OnCompleteBP;
	
	FOnCycleCompleteEvent OnCycleCompleteCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnCycleComplete"))
	FOnDreamUIPlayTweenCycleCompleteDynamicDelegate OnCycleCompleteBP;


	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamTweenLoop GetLoopType()const { return LoopType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		int GetLoopCount()const { return LoopCount; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		EDreamTweenEase GetEaseType()const { return EaseType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UCurveFloat* GetEaseCurve()const { return EaseCurve; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetDuration()const { return Duration; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetStartDelay()const { return StartDelay; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetAffectByGamePause()const { return bAffectByGamePause; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetAffectByTimeDilation()const { return bAffectByTimeDilation; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetLoopType(EDreamTweenLoop Value){ LoopType = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetLoopCount(int Value) { LoopCount = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEaseType(EDreamTweenEase Value) { EaseType = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetEaseCurve(UCurveFloat* Value) { EaseCurve = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetDuration(float Value) { Duration = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetStartDelay(float Value) { StartDelay = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetAffectByGamePause(bool Value) { bAffectByGamePause = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetAffectByTimeDilation(bool Value) { bAffectByTimeDilation = Value; }
protected:
	virtual void OnUpdate(float progress)PURE_VIRTUAL(UDreamGUIPlayTween::OnUpdate, );
};
