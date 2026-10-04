// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DreamUITabSwitchTarget.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUITabSwitchTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * A widget the shoulder buttons switch tabs on: a tab view (UDreamTabView). The keys are UDreamGUISettings's
 * PreviousTabKeys and NextTabKeys, routed by DreamUIKeyRouting after the bindings had their turn, to the player's active
 * target (DreamUIKeyRouting::FindTabSwitchTarget); the action bar shows their prompts while a player has one.
 */
class DREAMGUIINPUT_API IDreamUITabSwitchTarget
{
	GENERATED_BODY()

public:
	/** Whether player InUserIndex's shoulder buttons may switch tabs here now: it is in play, enabled, drawn, and has another tab to go to. */
	virtual bool CanSwitchTab(int32 InUserIndex) const { return false; }
	/**
	 * Select the tab InDelta away from the active one -- -1 the previous, 1 the next -- skipping the ones that cannot be
	 * selected and going round at the ends, and move player InUserIndex's focus with it where a click on the tab would.
	 * True when the active tab changed.
	 */
	virtual bool SwitchTab(int32 InUserIndex, int32 InDelta) { return false; }
};
