// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Event/DreamDelegateDeclaration.h"
#include "DreamUIScrollbarInterface.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUIScrollbarInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * A behaviour that shows a scroll position as a bar and can be dragged to set one: what the core's scroll
 * box layout keeps in step with itself, both ways, when an author links a bar to it (its Scrollbar). The
 * core names the bar through this, not as the control library's UUIScrollbar, which implements it.
 */
class DREAMGUI_API IDreamUIScrollbarInterface
{
	GENERATED_BODY()

public:
	/**
	 * Show InValue, 0..1 from whichever end the bar calls zero, with a handle InSize of the track long.
	 * The change event fires only when bInFireEvent: a scroll box pushing its own position must not hear it
	 * back as a pull.
	 */
	virtual void SetScrollValueAndSize(float InValue, float InSize, bool bInFireEvent) = 0;

	/** Fired with the new value when the bar's value changes: dragged, or set with the event on. */
	virtual FDreamUIMulticastDelegateFloat& GetScrollValueChangedEvent() = 0;
};
