// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTweenerVirtual.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerVirtual:public UDreamTweener
{
	GENERATED_BODY()
public:

	void SetInitialValue(float newDuration)
	{
		this->duration = newDuration;
	}
protected:
	virtual void OnStartGetValue() override
	{
		
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		
	}
	virtual void SetValueForIncremental() override
	{
		
	}
	virtual void SetOriginValueForRestart() override
	{
		
	}
};