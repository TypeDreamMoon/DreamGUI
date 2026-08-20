// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "DreamUIDelegateHandleWrapper.generated.h"

/**
 *Just a wrapper for blueprint to store a delegate handle
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIDelegateHandleWrapper
{
	GENERATED_BODY()
public:
	FDreamUIDelegateHandleWrapper() {}
	FDreamUIDelegateHandleWrapper(FDelegateHandle InDelegateHandle)
	{
		DelegateHandle = InDelegateHandle;
	}
	FDelegateHandle DelegateHandle;
};
