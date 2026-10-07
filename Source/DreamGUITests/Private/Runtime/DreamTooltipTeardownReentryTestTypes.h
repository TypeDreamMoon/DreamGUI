// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Core/DreamUserWidget.h"
#include "DreamTooltipTeardownReentryTestTypes.generated.h"

/** Runs the same lifetime callback available to an authored custom tooltip. */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTooltipTeardownReentryFixture : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	TFunction<void()> DestructCallback;

	virtual void NativeOnDestruct() override
	{
		Super::NativeOnDestruct();
		TFunction<void()> Callback = MoveTemp(DestructCallback);
		if (Callback)Callback();
	}
};
