// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTween.h"
#include "Engine/World.h"
#include "DreamTweenerFrame.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerFrame:public UDreamTweener
{
	GENERATED_BODY()
public:
	uint32 startFrameNumber = 0;//b
	uint32 endFrameNumber = 0;//c

	void SetInitialValue(int newEndValue)
	{
		this->startFrameNumber = GFrameNumber;
		this->endFrameNumber = GFrameNumber + newEndValue;
	}
protected:
	virtual void OnStartGetValue() override
	{
		
	}
	virtual bool ToNext(float deltaTime, float unscaledDeltaTime) override
	{
		// Killed comes before paused, as in UDreamTweener::ToNext: a tween killed while the game is
		// paused is finished, and answering "still running" kept it in the manager's list for good.
		if (isMarkedToKill)return false;
		if (auto world = GetWorld())
		{
			if (world->IsPaused() && affectByGamePause)return true;
		}
		if (isMarkedPause)return true;//no need to tick time if pause
		if (!startToTween)
		{
			startToTween = true;
			onStartCpp.Broadcast();
		}

		if (GFrameNumber >= endFrameNumber)
		{
			onUpdateCpp.Broadcast(1.0f);
			onCompleteCpp.Broadcast();
			// Through FinishOrHold, as every ToNext does: with auto-kill off the tween stays in the
			// manager's list, paused at its end, until something restarts or kills it.
			return FinishOrHold(false);
		}
		else
		{
			onUpdateCpp.Broadcast((float)(GFrameNumber - startFrameNumber) / (endFrameNumber - startFrameNumber));
			return true;
		}
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		
	}
	virtual UDreamTweener* SetDelay(float newDelay)override
	{
		UE_LOG(DreamTween, Error, TEXT("[DreamTweenerFrame::SetDelay]DreamTweenerFrame does not support delay!"));
		return this;
	}
	virtual UDreamTweener* SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount)override
	{
		UE_LOG(DreamTween, Error, TEXT("[DreamTweenerFrame::SetLoop]DreamTweenerFrame does not support loop!"));
		return this;
	}
	virtual void SetValueForIncremental() override
	{
		
	}
	virtual void SetOriginValueForRestart() override
	{
		auto changeValue = endFrameNumber - startFrameNumber;
		startFrameNumber = GFrameNumber;
		endFrameNumber = GFrameNumber + changeValue;
	}
};