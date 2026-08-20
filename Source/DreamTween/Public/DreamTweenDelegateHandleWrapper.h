// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamTweenDelegateHandleWrapper.generated.h"

/** Just a wrapper for blueprint to store a delegate handle */
USTRUCT(BlueprintType)
struct DREAMTWEEN_API FDreamTweenDelegateHandleWrapper
{
	GENERATED_BODY()
public:
	FDreamTweenDelegateHandleWrapper() {}
	FDreamTweenDelegateHandleWrapper(FDelegateHandle InDelegateHandle)
	{
		DelegateHandle = InDelegateHandle;
	}
	FDelegateHandle DelegateHandle;
};
