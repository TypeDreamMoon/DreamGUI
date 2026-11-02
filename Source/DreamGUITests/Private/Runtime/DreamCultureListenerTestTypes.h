// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "DreamCultureListenerTestTypes.generated.h"

/** A culture listener that only counts how many times the manager told it the words may have changed. */
UCLASS()
class UDreamCultureListenerProbe : public UObject, public IDreamUICultureChangedInterface
{
	GENERATED_BODY()

public:
	int32 Calls = 0;

	virtual void OnCultureChanged_Implementation() override
	{
		++Calls;
	}
};
