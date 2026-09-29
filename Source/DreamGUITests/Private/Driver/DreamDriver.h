// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"

#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverSequence.h"

/**
 * The front door: find things, act on them, wait for them.
 *
 * Deliberately thin. Everything it can do is done by the pieces underneath -- locators answer where,
 * elements answer what, sequences answer when -- and this exists so a test has one object to hold
 * rather than four. The engine's IAutomationDriver has the same shape and the same reason.
 *
 * Held by shared pointer because elements keep a weak reference back to it: an element outliving its
 * driver has to be able to notice, rather than reach through a dangling pointer into a world that has
 * been destroyed.
 */
class FDreamDriver : public TSharedFromThis<FDreamDriver>
{
public:
	explicit FDreamDriver(FDreamDriverContext& InContext);

	/**
	 * The element this locator names. ALWAYS returns an element, even when nothing matches -- ask it
	 * Exists(). A null return would make every test write a null check before it could assert the
	 * interesting thing, and "the button is not there yet" is an ordinary state, not an error.
	 */
	FDreamElementRef Find(const FDreamLocatorRef& InLocator);

	/**
	 * Every element the locator finds, each pinned to the widget it resolved to. Pinned, so iterating
	 * the result and acting on each one cannot be derailed by an ambiguous locator.
	 */
	TArray<FDreamElementRef> FindAll(const FDreamLocatorRef& InLocator);

	/** A fresh, empty sequence over this driver's context. */
	FDreamDriverSequence Sequence();

	/**
	 * Pump frames until the wait passes, fails or times out. Returns whether it passed, and reports
	 * the failure through the running test either way it did not -- so a test can assert on the
	 * return value and still get a message naming what it was waiting for.
	 */
	bool Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout, const FString& InDescription);
	bool Wait(const FDriverWaitDelegate& InWaitDelegate, FWaitTimeout InTimeout);
	bool Wait(const FDriverWaitDelegate& InWaitDelegate);

	/** Let InFrameCount frames of the headless pump pass, with no input. */
	void PumpFrames(int32 InFrameCount);

	FDreamDriverContext& GetContext() { return *Context; }
	const FDreamDriverContext& GetContext() const { return *Context; }

private:
	/** Owned by the rig, which outlives every driver and element it hands out. */
	FDreamDriverContext* Context = nullptr;
};

using FDreamDriverRef = TSharedRef<FDreamDriver>;
