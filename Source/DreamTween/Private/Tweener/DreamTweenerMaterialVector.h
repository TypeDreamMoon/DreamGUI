// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "DreamTweener.h"
#include "DreamTween.h"
#include "DreamTweenerMaterialVector.generated.h"

UCLASS(NotBlueprintType)
class DREAMTWEEN_API UDreamTweenerMaterialVector :public UDreamTweener
{
	GENERATED_BODY()
public:
	float startFloat = 0.0f;//b
	float changeFloat = 1.0f;//c
	FLinearColor startValue;
	FLinearColor endValue;
	int32 parameterIndex;

	FDreamTweenMaterialVectorGetterFunction getter;
	FDreamTweenMaterialVectorSetterFunction setter;

	FLinearColor originStartValue;

	void SetInitialValue(const FDreamTweenMaterialVectorGetterFunction& newGetter, const FDreamTweenMaterialVectorSetterFunction& newSetter, const FLinearColor& newEndValue, float newDuration, int32 newParameterIndex)
	{
		this->duration = newDuration;
		this->getter = newGetter;
		this->setter = newSetter;
		this->parameterIndex = newParameterIndex;
		this->endValue = newEndValue;

		this->startFloat = 0.0f;
		this->changeFloat = 1.0f;
	}
protected:
	virtual void OnStartGetValue() override
	{
		if (getter.IsBound())
		{
			if (getter.Execute(startValue))
			{
				this->originStartValue = startValue;
			}
			else
			{
				UE_LOG(DreamTween, Error, TEXT("[UDreamTweenerMaterialVector/OnStartGetValue]Get paramter value error!"));
			}
		}
	}
	virtual void TweenAndApplyValue(float currentTime) override
	{
		float lerpValue = tweenFunc.Execute(changeFloat, startFloat, currentTime, duration);
		FLinearColor value;
		value.R = FMath::Lerp(startValue.R, endValue.R, lerpValue);
		value.G = FMath::Lerp(startValue.G, endValue.G, lerpValue);
		value.B = FMath::Lerp(startValue.B, endValue.B, lerpValue);
		value.A = FMath::Lerp(startValue.A, endValue.A, lerpValue);
		if (setter.IsBound()) 
			if (setter.Execute(parameterIndex, value) == false)
			{
				UE_LOG(DreamTween, Warning, TEXT("[UDreamTweenerMaterialScalar/TweenAndApplyValue]Set paramter value error!"));
			}
	}
	virtual void SetValueForIncremental() override
	{
		auto diffValue = endValue - startValue;
		startValue = endValue;
		endValue += diffValue;
	}
	virtual void SetOriginValueForRestart() override
	{
		auto diffValue = endValue - startValue;
		startValue = originStartValue;
		endValue = originStartValue + diffValue;
	}
};