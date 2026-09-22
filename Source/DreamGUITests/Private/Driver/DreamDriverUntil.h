// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "WaitUntil.h"

#include "Driver/DreamDriverElement.h"

/**
 * Conditions to wait for, as the engine's own FDriverWaitDelegate.
 *
 * Named FDreamUntil rather than Until because the test module links AutomationDriver, whose factory
 * for exactly this is a class called Until. The delegate, the response and the timeout wrapper are
 * the engine's types, reused rather than copied: there is nothing DreamGUI-specific about "passed,
 * still waiting, or gave up", and a parallel set of three would be three more things to keep in step.
 *
 * Each factory bakes its timeout in: the delegate reports FAILED once the total wait it is handed has
 * reached it. That is how the engine's Until works and it is what lets a caller pass one of these to
 * a Wait that has a timeout of its own without the two having to agree.
 *
 * The elements are captured by shared reference, so a condition keeps its element alive for as long
 * as the wait does. The element's own reference to the driver stays weak, so nothing here extends the
 * life of a world.
 */
class FDreamUntil
{
public:
	/** A widget answers the element's locator at all. */
	static FDriverWaitDelegate ElementExists(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** It exists and is render-visible all the way up its hierarchy. */
	static FDriverWaitDelegate ElementIsVisible(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** It exists and no longer answers -- for waiting out a teardown. */
	static FDriverWaitDelegate ElementIsGone(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** It exists and input would reach it. */
	static FDriverWaitDelegate ElementIsInteractable(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** The pointer is over it, or over something in its subtree that entered through it. */
	static FDriverWaitDelegate ElementIsHovered(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** The trigger went down on it and has not come up. */
	static FDriverWaitDelegate ElementIsPressed(const FDreamElementRef& InElement, FWaitTimeout InTimeout);
	/** The event system's selection is on it. */
	static FDriverWaitDelegate ElementIsSelected(const FDreamElementRef& InElement, FWaitTimeout InTimeout);

	/** Anything a test can phrase as a predicate. Passes the frame it first returns true. */
	static FDriverWaitDelegate Condition(TFunction<bool()> InCondition, FWaitTimeout InTimeout);

	/**
	 * Full control: the function is handed the total time waited so far and answers with a response
	 * of its own, so it can report FAILED rather than merely never passing. No timeout is applied on
	 * top -- a lambda that never answers is stopped by the Wait's own timeout instead.
	 */
	static FDriverWaitDelegate Lambda(TFunction<FDriverWaitResponse(const FTimespan&)> InLambda);
};
