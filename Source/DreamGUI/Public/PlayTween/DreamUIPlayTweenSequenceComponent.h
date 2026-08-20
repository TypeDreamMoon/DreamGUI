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
	FDelegateHandle OnCompleteDelegateHandle;
	DECLARE_EVENT(UDreamUIPlayTweenSequenceComponent, FOnCompleteEvent);

	virtual void Awake()override;
public:
	FOnCompleteEvent OnCompleteCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI", meta=(DisplayName="OnComplete"))
	FOnDreamUIPlayTweenSequenceCompleteDynamicDelegate OnCompleteBP;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void Play();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void Stop();
};
