// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
};
