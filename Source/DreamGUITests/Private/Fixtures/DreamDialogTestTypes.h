// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DreamDialogTestTypes.generated.h"

/**
 * Somewhere for a dialog's result to land.
 *
 * A dynamic delegate can only bind a UFUNCTION on a UObject, and nothing in the module stores an
 * FName it was handed -- so asserting WHICH result a click answered with needs a real object to hang
 * that function off, exactly as UDreamActionCallCounter does for the action router. The count is
 * here too: "fired once with the right name" and "fired twice, the second time correctly" are
 * different claims.
 */
UCLASS()
class UDreamDialogResultProbe : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void Record(FName Result)
	{
		LastResult = Result;
		++CallCount;
	}

	FName LastResult = NAME_None;
	int32 CallCount = 0;
};
