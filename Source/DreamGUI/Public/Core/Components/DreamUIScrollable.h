// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Core/Components/DreamScrollTypes.h"
#include "DreamUIScrollable.generated.h"

class UDreamWidget;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUIScrollable : public UInterface
{
	GENERATED_BODY()
};

/**
 * A behaviour that scrolls its widget's content, as keyboard and gamepad navigation sees it: bring the
 * focused widget into view, scroll by the stick, a page or to either end.
 *
 * The navigation code in the core asks a widget's behaviours for this instead of naming the control
 * library's scroll view, which the core cannot include. UUIScrollView implements it; the core's own
 * scroll box layout is asked directly, as before.
 */
class DREAMGUI_API IDreamUIScrollable
{
	GENERATED_BODY()

public:
	/** The widget whose content scrolls. */
	virtual UDreamWidget* GetScrollableWidget() const = 0;

	/** Whether InWidget sits inside this and a scroll would bring more of it into sight. */
	virtual bool CanScrollToReveal(UDreamWidget* InWidget) = 0;

	/** Scroll so that InWidget is in view, by this container's own rules. True when the content moved. */
	virtual bool ScrollToReveal(UDreamWidget* InWidget, bool bInAnimate) = 0;

	/** Whether focus moving inside should scroll this at all, and how. */
	virtual EDreamUIScrollWhenFocusChanges GetFocusScrollRule() const = 0;

	/** Focus moved to InWidget, somewhere inside. Told whether or not anything scrolled. */
	virtual void NotifyFocusMovedInside(UDreamWidget* InWidget) = 0;

	/** Whether a gamepad stick scrolls this at all. */
	virtual bool AcceptsGamepadScrolling() const = 0;

	/** The analog key that scrolls this the way a wheel does; unset means whichever the input preset routes. */
	virtual FKey GetAnalogScrollKey() const = 0;

	/** Whether the content can scroll along the horizontal axis (true) or the vertical one (false). */
	virtual bool CanScrollAlong(bool bInHorizontalAxis) const = 0;

	/** Where the content is scrolled to. */
	virtual FVector2D GetScrollPosition() const = 0;

	/** Scroll the content by InDelta. */
	virtual void ScrollContentBy(const FVector2D& InDelta) = 0;

	/** Scroll the content all the way to its start, or to its end. */
	virtual void ScrollContentToExtent(bool bInToStart) = 0;
};
