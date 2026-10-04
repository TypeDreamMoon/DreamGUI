// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamUITabSwitchTarget.h"
#include "DreamKeyRoutingTestTypes.generated.h"

/**
 * A widget the shoulder buttons switch tabs on, standing in for a tab view: it takes every switch and counts it, so a test
 * sees whether a shoulder button reached the tab-switch route or something before it -- a binding -- kept it.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTabSwitchProbeWidget : public UDreamWidget, public IDreamUITabSwitchTarget
{
	GENERATED_BODY()

public:
	int32 SwitchCount = 0;
	int32 LastDelta = 0;
	int32 LastUserIndex = INDEX_NONE;

	virtual bool CanSwitchTab(int32 InUserIndex) const override
	{
		return true;
	}

	virtual bool SwitchTab(int32 InUserIndex, int32 InDelta) override
	{
		++SwitchCount;
		LastDelta = InDelta;
		LastUserIndex = InUserIndex;
		return true;
	}
};
