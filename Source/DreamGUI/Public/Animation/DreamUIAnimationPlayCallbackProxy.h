// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "UObject/Object.h"
#include "Containers/Ticker.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "DreamUIAnimationPlayCallbackProxy.generated.h"

class UDreamUserWidget;
class UDreamWidgetAnimationPlayer;
class UMovieSceneSequence;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDreamUIAnimationResult);

/**
 * The object behind the "Play Animation with Finished event" node: plays through the user widget
 * and fires Finished one frame after the instance ends, naturally or by Stop. Mirrors UMG's
 * UWidgetAnimationPlayCallbackProxy; the K2 node that spawns it lives in DreamGUIK2Nodes.
 */
UCLASS()
class DREAMGUI_API UDreamUIAnimationPlayCallbackProxy : public UObject
{
	GENERATED_BODY()

public:
	/** The instance ended. Fired the frame after, so a graph can safely play the next animation. */
	UPROPERTY(BlueprintAssignable)
	FDreamUIAnimationResult Finished;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation", meta = (
		BlueprintInternalUseOnly = "true",
		DisplayName = "Play Animation with Finished event",
		ShortToolTip = "Play Animation and trigger event on Finished",
		ToolTip = "Play an animation on the widget and trigger the Finished event when the instance is done."))
	static UDreamUIAnimationPlayCallbackProxy* NewPlayAnimationProxyObject(
		FDreamUIAnimationHandle& Result,
		UDreamUserWidget* Widget,
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Animation", meta = (
		BlueprintInternalUseOnly = "true",
		DisplayName = "Play Animation Time Range with Finished event",
		ShortToolTip = "Play Animation Time Range and trigger event on Finished",
		ToolTip = "Play a time range of an animation on the widget and trigger the Finished event when the instance is done."))
	static UDreamUIAnimationPlayCallbackProxy* NewPlayAnimationTimeRangeProxyObject(
		FDreamUIAnimationHandle& Result,
		UDreamUserWidget* Widget,
		UMovieSceneSequence* Animation,
		float StartAtTime = 0.0f,
		float EndAtTime = 0.0f,
		int32 NumLoopsToPlay = 1,
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward,
		float PlaybackSpeed = 1.0f);

private:
	void Execute(UDreamUserWidget* Widget, UMovieSceneSequence* Animation, float StartAtTime, TOptional<float> EndAtTime, int32 NumLoopsToPlay, EDreamUIAnimationPlayMode PlayMode, float PlaybackSpeed, FDreamUIAnimationHandle& OutHandle);
	void OnInstanceFinished(const FDreamUIAnimationHandle& InHandle);
	void ScheduleFinished();
	bool BroadcastFinished(float DeltaTime);

	TWeakObjectPtr<UDreamWidgetAnimationComponent> Component;
	TWeakObjectPtr<UDreamWidgetAnimationPlayer> Player;
	FDelegateHandle FinishedHandle;
	FTSTicker::FDelegateHandle TickerHandle;
};
