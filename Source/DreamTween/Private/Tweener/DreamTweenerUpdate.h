// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTween.h"
#include "Engine/World.h"
#include "DreamTweenerUpdate.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerUpdate :public UDreamTweener
{
	GENERATED_BODY()
public:
	void SetInitialValue(int newEndValue)
	{
		
	}
protected:
	virtual void OnStartGetValue() override
	{
		
	}
	virtual bool ToNext(float deltaTime, float unscaledDeltaTime) override
	{
		if (auto world = GetWorld())
		{
			if (world->IsPaused() && affectByGamePause)return true;
		}
		if (isMarkedToKill)return false;
		if (isMarkedPause)return true;//no need to tick time if pause
		onUpdateCpp.ExecuteIfBound(affectByTimeDilation ? deltaTime : unscaledDeltaTime);
		return true;
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		
	}
	virtual UDreamTweener* SetDelay(float newDelay)override
	{
		UE_LOG(DreamTween, Error, TEXT("[DreamTweenerFrame::SetDelay]DreamTweenerUpdate does not support delay!"));
		return this;
	}
	virtual UDreamTweener* SetLoop(EDreamTweenLoop newLoopType, int32 newLoopCount)override
	{
		UE_LOG(DreamTween, Error, TEXT("[DreamTweenerFrame::SetLoop]DreamTweenerUpdate does not support loop!"));
		return this;
	}
	virtual void SetValueForIncremental() override
	{
		
	}
	virtual void SetOriginValueForRestart() override
	{
		
	}
};