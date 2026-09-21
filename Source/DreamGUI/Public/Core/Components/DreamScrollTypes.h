// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// Every scrolling implementation carries an analog key that stands in for the wheel, and FKey is not
// in CoreMinimal. Included here so all three get it from the one header they already share.
#include "InputCoreTypes.h"
#include "DreamScrollTypes.generated.h"

/**
 * The vocabulary three scrolling implementations have to share.
 *
 * DreamGUI scrolls in three places -- the scroll box CONTROL, the scroll box LAYOUT CONTAINER and the
 * UUIScrollView BEHAVIOUR -- and each of them has to answer the same questions: where does a revealed
 * widget end up, what happens to the wheel event, what does focus landing inside do. The answers used
 * to live in whichever of the three files needed them first, which meant a header could only reach a
 * question by including a whole implementation, and the layout container and the behaviour cannot
 * include each other.
 *
 * So the questions live here and the implementations include this. Nothing in this file is specific to
 * any of the three, and nothing in it is an implementation.
 */

/** When a wheel event is swallowed by a scrolling container instead of being handed to whatever is behind it. */
UENUM(BlueprintType)
enum class EDreamScrollBoxConsumeMouseWheel : uint8
{
	/** Never scroll on the wheel; the event always passes through. */
	Never,
	/** Scroll and consume only while there is somewhere left to scroll -- the nesting-friendly default. */
	WhenScrollingPossible,
	/** Always consume, even at a limit, so the wheel never reaches an outer box. */
	Always,
};

/**
 * Where a revealed widget ends up -- UMG's EDescendantScrollDestination.
 *
 * IntoView is this library's original answer and stays the default: it moves the LEAST distance that
 * reveals the widget, which is the only one that reads well under directional navigation (stepping
 * one row down must not heave the whole list). The others are for the cases that genuinely want a
 * fixed frame -- a menu whose rows all sit in the same place, a carousel that centres its choice.
 */
UENUM(BlueprintType)
enum class EDreamUIScrollDestination : uint8
{
	/** The least movement that brings it fully inside the window. */
	IntoView,
	/** Always parked against the leading edge -- the left of a horizontal view, the top of a vertical one. */
	TopOrLeft,
	/** Always centred in the window. */
	Center,
	/**
	 * Always parked against the TRAILING edge -- the right of a horizontal view, the bottom of a
	 * vertical one. Appended rather than inserted: the three above it are serialized in project assets
	 * by value, so the order of what was already here is not ours to change.
	 */
	BottomOrRight,
	/**
	 * Not a destination: "whichever one this view or control is configured with". An ARGUMENT value.
	 * Every PROPERTY of this type rules it out with InvalidEnumValues -- authoring it on
	 * NavigationDestination itself would be a property whose value is "this property" -- and the
	 * setters refuse it too. It cannot be hidden on the enum instead: a hidden entry may not be a
	 * UFUNCTION's default argument, and being one is the whole of its job.
	 *
	 * It exists so ScrollWidgetIntoView can take a destination the way UMG's does WITHOUT the added
	 * parameter quietly overriding an authored NavigationDestination on every existing caller: the
	 * default argument has to mean "carry on doing what you did", and no real destination can.
	 */
	Configured UMETA(DisplayName = "Configured (the view's own setting)"),
};

/**
 * What a scrolling container does when user focus lands on something inside it.
 *
 * AnimatedScroll is the default because it is what this library already does: directional navigation
 * calls FDreamUINavigationScroll::RevealWidget with animation on, unconditionally, and there was no
 * knob to say otherwise. UMG's own default is NoScroll -- a difference recorded in the parity tables
 * rather than silently adopted, because adopting it would stop every existing gamepad-driven list
 * from following its focus.
 */
UENUM(BlueprintType)
enum class EDreamUIScrollWhenFocusChanges : uint8
{
	/** Never move. Whatever gave the widget focus is responsible for showing it. */
	NoScroll,
	/** Jump straight to the position NavigationDestination asks for. */
	InstantScroll,
	/** Glide to it. */
	AnimatedScroll,
};
