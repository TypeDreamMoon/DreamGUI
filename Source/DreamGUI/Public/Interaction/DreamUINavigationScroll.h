// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class UDreamWidget;

/**
 * Bridges directional navigation to the scrolling containers a widget lives inside.
 *
 * Navigation used to stop dead at the edge of a clipped view: FindSelectable dropped every candidate
 * whose centre was clipped away, which is right for something hidden behind a mask and wrong for the
 * next row of a list that one scroll would reveal. The result was a gamepad that could only reach the
 * rows already on screen. These two calls draw that distinction -- IsReachableByScrolling answers
 * whether a clipped candidate is merely scrolled off, and RevealWidget does the scrolling once
 * navigation has committed to it.
 *
 * Both kinds of scrolling container count: the scroll box layout and the legacy scroll view component.
 * A project can be built on either, and a list inside a page can be using both at once.
 */
class DREAMGUI_API FDreamUINavigationScroll
{
public:
	/**
	 * True when some scrolling ancestor of InWidget could move to bring more of it into sight. False
	 * for a widget that is off-screen for any other reason, which must stay unreachable.
	 */
	static bool IsReachableByScrolling(const UDreamWidget* InWidget);
	/**
	 * Scroll every scrolling ancestor of InWidget the least distance that reveals it, innermost first
	 * -- an outer container has to be positioned against where the inner one ended up, not where it
	 * started. Returns true when anything moved.
	 */
	static bool RevealWidget(UDreamWidget* InWidget, bool bAnimate = true);

	/**
	 * Scroll the innermost scrolling ancestor of InWidget by InPages screenfuls; negative goes back
	 * towards the start. What PageUp and PageDown do, and what the right stick does in fractions.
	 *
	 * A screenful is the container's own visible extent along the axis it scrolls, which is the only
	 * definition that stays right when the rows are not all the same height -- paging by a row count
	 * would step past a tall entry and stop short of a run of short ones.
	 *
	 * @return true when something actually moved. False at a limit, and for a widget with no
	 *         scrolling ancestor at all -- which the caller must not treat as an error: most widgets
	 *         are not in a list, and a page key pressed over one simply does nothing.
	 */
	static bool ScrollByPages(UDreamWidget* InWidget, float InPages, bool bAnimate = true);
	/** Jump the innermost scrolling ancestor of InWidget to its start or its end. Home and End. */
	static bool ScrollToExtent(UDreamWidget* InWidget, bool bToStart);
	/**
	 * Scroll the innermost scrolling ancestor by a raw local-space delta, without animation.
	 *
	 * For an analog stick, which is already a continuous per-frame value: routing one through the
	 * animated path would restart an interpolation every frame and never arrive.
	 */
	static bool ScrollByDelta(UDreamWidget* InWidget, const FVector2D& InDelta);
	/**
	 * ScrollByDelta, but only when the innermost scrolling ancestor accepts THIS analog key as its
	 * virtual wheel -- the gate behind UMG's AnalogMouseWheelKey.
	 *
	 * A container that names no key takes whatever the input preset sends, which is what every one of
	 * them did before the property existed. A container that names one takes that axis and no other,
	 * so two lists on the same screen can be driven by different sticks.
	 */
	static bool ScrollByAnalogAxis(UDreamWidget* InWidget, const FKey& InAxisKey, const FVector2D& InDelta);
	/** True when InWidget has a scrolling ancestor that could move at all. */
	static bool HasScrollableAncestor(const UDreamWidget* InWidget);
};
