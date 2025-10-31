// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexUIDelegateHandleWrapper.generated.h"

/**
 *Just a wrapper for blueprint to store a delegate handle
 */
USTRUCT(BlueprintType)
struct LGUI_API FLexUIDelegateHandleWrapper
{
	GENERATED_BODY()
public:
	FLexUIDelegateHandleWrapper() {}
	FLexUIDelegateHandleWrapper(FDelegateHandle InDelegateHandle)
	{
		DelegateHandle = InDelegateHandle;
	}
	FDelegateHandle DelegateHandle;
};
