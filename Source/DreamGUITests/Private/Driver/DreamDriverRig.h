// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverSequence.h"

class AActor;
class FAutomationTestBase;
class UDreamCanvas;
class UDreamEventSystem;
class UDreamDriverInputModule;
class UDreamScreenSpaceRaycaster;
class UDreamWidget;
class UWorld;

namespace DreamTests
{
	struct FScopedGameWorld;
}

/**
 * Everything a pointer needs in order to exist, built and torn down together.
 *
 * A world, an actor to hang the input pieces on, an event system, a driver input module bound to it,
 * a screen-space raycaster, and a root widget carrying a ScreenSpaceOverlay canvas with a substituted
 * viewport size. Every one of those is built the way the runtime builds it -- the raycaster enrols
 * itself with the UI manager's raycaster list, the canvas enrols itself with its canvas list -- so a
 * trace here walks the same list a trace in a game would. Nothing is handed a result.
 *
 * THE VIEWPORT. A game world with no player controller answers GetViewportSize with a 2x2 fallback,
 * which makes the canvas 2 units across and the projection matrix describe a 2x2 screen, and a ray
 * cast through that lands nowhere. UDreamCanvas::SetViewportSizeOverride is what this rig was the
 * reason for; without it none of the coordinate arithmetic means anything.
 *
 * Not copyable and not movable: the driver and every element it hands out point at the context that
 * lives inside this object, so a rig that could be moved would leave them pointing at the old one.
 * Constructing one from Headless() is still fine -- C++17 builds it straight into the caller's
 * variable rather than moving it there.
 */
class FDreamDriverRig
{
public:
	/**
	 * A complete headless rig with a substituted viewport of InViewportSize.
	 *
	 * Build it into a local and keep it for the whole test:
	 *     FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	 */
	static FDreamDriverRig Headless(FIntPoint InViewportSize);

	~FDreamDriverRig();

	FDreamDriverRig(const FDreamDriverRig&) = delete;
	FDreamDriverRig& operator=(const FDreamDriverRig&) = delete;
	FDreamDriverRig(FDreamDriverRig&&) = delete;
	FDreamDriverRig& operator=(FDreamDriverRig&&) = delete;

	/** Whether every piece came up. A test should say so and stop rather than act on half a rig. */
	bool IsUsable() const;

	UWorld* GetWorld() const;
	UDreamWidget* Root() const;
	UDreamCanvas* RootCanvas() const;
	UDreamEventSystem* EventSystem() const;
	UDreamDriverInputModule* InputModule() const;
	UDreamScreenSpaceRaycaster* Raycaster() const;

	FDreamDriverContext& Context() const;
	FDreamDriverRef Driver() const;

	/** Tell the rig which test is running, so a failing step reports against it. */
	void BindTest(FAutomationTestBase* InTest);

	/**
	 * A hit-testable child widget under InParent (the root when null), sized and placed.
	 *
	 * The order is not arbitrary: register, then parent, then give it its visual. A visual is only
	 * enrolled with its widget's render canvas if there is one by the time it is created, and a visual
	 * that was never enrolled is one the raycaster never walks over -- the widget would be there,
	 * laid out, drawn in no draw call and unclickable.
	 *
	 * InAnchoredPosition is in canvas units with Y UPWARD, because that is what the anchor system
	 * speaks; it is the projection, not the authoring, that flips Y.
	 */
	UDreamWidget* MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/** Let frames pass with no input, for a test that wants the tree to settle. */
	void PumpFrames(int32 InFrameCount);

private:
	explicit FDreamDriverRig(const FIntPoint& InViewportSize);

	/** Torn down last, because everything below lives inside it. */
	TUniquePtr<DreamTests::FScopedGameWorld> ScopedWorld;
	/** By pointer so its address survives anything that happens to this object. */
	TUniquePtr<FDreamDriverContext> DriverContext;
	TSharedPtr<FDreamDriver> DriverInstance;
	AActor* Host = nullptr;
};
