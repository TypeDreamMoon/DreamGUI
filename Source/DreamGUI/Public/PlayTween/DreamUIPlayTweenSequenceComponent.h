// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIPlayTween.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIPlayTweenSequenceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDreamUIPlayTweenSequenceCompleteDynamicDelegate);

//play tween array sequentially, one after one.
UCLASS(ClassGroup = (DreamGUI), meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamUIPlayTweenSequenceComponent : public UDreamUIBehaviour
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bPlayOnStart = true;
	/**
	 * Play next tween when tween cycle complete, or wait until all loop complete (which could stuck at single tween if the tween's loop is infinite).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bPlayNextWhenCycleComplete = false;
	/** Play tween array sequentially, one after one. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", Instanced)
		TArray<TObjectPtr<class UDreamUIPlayTween>> PlayTweenArray;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		FDreamUIEventDelegate OnComplete = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Empty);

	bool bIsPlaying = false;
	int CurrentTweenPlayIndex = 0;
	void OnTweenComplete();
	/**
	 * Which of a play tween's two completion events drives this sequence is one authored flag, but the
	 * subscribe and the unsubscribe live three call sites apart. They are a pair of functions rather
	 * than four inline branches because the unsubscribe used to read the cycle event whichever way the
	 * flag pointed: a sequence chained on OnComplete therefore never let go, and every replay left
	 * another live subscription behind to push the index along the next time that tween was started by
	 * anyone else.
	 */
	void SubscribeToTween(class UDreamUIPlayTween* InPlayTween);
	void UnsubscribeFromTween(class UDreamUIPlayTween* InPlayTween);
	/**
	 * The first playable tween at or after InStartIndex, or Num() when there is none left. An Instanced
	 * array on a details panel gains its rows before it gains its contents, so a hole in the middle of
	 * the list is an ordinary authoring state and not an error to report.
	 */
	int32 FindNextPlayableTweenIndex(int32 InStartIndex)const;
	FDelegateHandle OnCompleteDelegateHandle;
	DECLARE_EVENT(UDreamUIPlayTweenSequenceComponent, FOnCompleteEvent);

	virtual void Awake()override;
	virtual void OnDestroy()override;
public:
	FOnCompleteEvent OnCompleteCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnComplete"))
	FOnDreamUIPlayTweenSequenceCompleteDynamicDelegate OnCompleteBP;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void Play();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void Stop();
};
