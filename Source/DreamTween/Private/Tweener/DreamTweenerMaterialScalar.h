// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTween.h"
#include "DreamTweenerMaterialScalar.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerMaterialScalar:public UDreamTweener
{
	GENERATED_BODY()
public:
	float startValue = 0.0f;//b
	float changeValue = 0.0f;//c
	int32 parameterIndex;
	float endValue = 0.0f;
	FDreamTweenMaterialScalarGetterFunction getter;
	FDreamTweenMaterialScalarSetterFunction setter;

	float originStartValue = 0.0f;

	void SetInitialValue(const FDreamTweenMaterialScalarGetterFunction& newGetter, const FDreamTweenMaterialScalarSetterFunction& newSetter, float newEndValue, float newDuration, int32 newParameterIndex)
	{
		this->duration = newDuration;
		this->getter = newGetter;
		this->setter = newSetter;
		this->endValue = newEndValue;
		this->parameterIndex = newParameterIndex;
	}
protected:
	virtual void OnStartGetValue() override
	{
		if (getter.IsBound())
		{
			if (getter.Execute(startValue))
			{
				this->changeValue = endValue - startValue;
				this->originStartValue = this->startValue;
			}
			else
			{
				UE_LOG(DreamTween, Error, TEXT("[UDreamTweenerMaterialScalar/OnStartGetValue]Get paramter value error!"));
			}
		}
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		auto value = tweenFunc.Execute(changeValue, startValue, currentTime, duration);
		if (setter.IsBound())
			if (setter.Execute(parameterIndex, value) == false)
			{
				UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenerMaterialScalar/TweenAndApplyValue]Set paramter value error!"));
			}
	}
	virtual void SetValueForIncremental() override
	{
		startValue = endValue;
		endValue += changeValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		startValue = originStartValue;
		endValue = originStartValue + changeValue;
	}
};