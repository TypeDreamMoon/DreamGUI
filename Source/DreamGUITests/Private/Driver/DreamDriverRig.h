// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Templates/SharedPointer.h"
#include "Templates/SubclassOf.h"
#include "Templates/UniquePtr.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

class AActor;
class FAutomationTestBase;
class UDreamCanvas;
class UDreamEventSystem;
class UDreamDriverInputModule;
class UDreamScreenSpaceRaycaster;
class UDreamWidget;
class UGameInstance;
class UWorld;

namespace DreamTests
{
	struct FScopedGameWorld;
	struct FScopedGameInstanceWorld;
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
 * THE WORLD belongs to a UGameInstance by default (FDreamRigOptions::bWithGameInstance), built the way
 * a standalone game builds one, because GameInstance subsystems -- the tween manager above all -- only
 * exist in such a world: in a bare UWorld::CreateWorld world every tween is refused and every animated
 * control either snaps or never moves. The bare world is still one option away, and is what the
 * designer previews in. Tearing the rig down shuts the game instance down and takes its world context
 * off the engine's list, so the next test does not find it.
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

	/**
	 * A headless rig built to InOptions. Headless(FIntPoint) is this with only the viewport size set,
	 * which means it, too, builds a world that belongs to a GameInstance -- see FDreamRigOptions.
	 */
	static FDreamDriverRig Headless(const FDreamRigOptions& InOptions);

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

	/** What this rig was built from. */
	const FDreamRigOptions& GetOptions() const;
	/** The GameInstance the world belongs to; null when bWithGameInstance was false. */
	UGameInstance* GetGameInstance() const;

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

	/**
	 * A control -- a user widget such as UDreamButton or UDreamSlider -- built the way the runtime
	 * builds one, under InParent (the root when null), sized and placed like MakeWidget.
	 *
	 * It goes through CreateDreamWidget, the class model's own factory: instance the class, Initialize
	 * (which realizes the control's parts and wires its behaviours), parent it without firing attach
	 * events, then RegisterDreamWidgetHierarchy, which registers the WHOLE subtree -- the control and
	 * every part inside it -- and hands the subtree the parent's render canvas, visibility and
	 * interactability. That last step is what a hand-rolled NewObject + OnRegister would miss: a
	 * control's parts are its children, and OnRegister registers only the widget it is called on.
	 *
	 * The name and size are written before the hierarchy registers, so the first layout pass that
	 * sees the control already sees it at its size; the anchored position is written after, the same
	 * as MakeWidget, because anchoring is resolved against a parent the widget is registered under.
	 *
	 * Nothing is pumped here. A test calls PumpFrames when it wants the control laid out.
	 *
	 * The control's parts -- a button's face, a slider's handle, a list's rows -- are reached through
	 * the control's own API and aimed at with FDreamBy::Widget(part).
	 */
	template<class T>
	T* MakeControl(const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUserWidget>::Value,
			"MakeControl builds user widgets; a plain UDreamWidget is MakeWidget's job");
		return Cast<T>(MakeControl(T::StaticClass(), InDisplayName, InParent, InSize, InAnchoredPosition));
	}

	/** MakeControl for a class only known at run time -- a Blueprint subclass, a class picked by a parameter. */
	UDreamWidget* MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InAnchoredPosition = FVector2D::ZeroVector);

	/**
	 * Give the rig the two things a game world has, a bare fixture lacks, and a CONTROL's input path
	 * assumes. MakeControl calls it; a test building a control by hand calls it before the first click.
	 *
	 * 1. The event system registered with the UI manager, which is what UDreamEventSystem::BeginPlay
	 *    does and what a world that never began play never gets. Everything that asks "which event
	 *    system is this player's" goes through that registration -- UUISelectable taking the selection
	 *    on a press, UUITextInput selecting itself when an edit begins, UDreamUINavigationStack::HandleBack
	 *    finding the field Escape should cancel -- so without it a click focuses nothing.
	 * 2. A player controller, on the world's controller list and with its input system up.
	 *    UUITextInput::ActivateInput binds the field's keys on an actor whose InputComponent only
	 *    exists once the world's player 0 has enabled its input, and binds them through that
	 *    component without asking whether it exists. With no findable player controller, clicking a
	 *    text field dereferences null -- in every control that carries one, a spin box included.
	 *    Spawning is not enough in a world nobody initialized for play; see the definition.
	 *
	 * Both are idempotent. Neither is done by the rig's constructor, so a test that never asks keeps
	 * exactly the rig it had before this existed.
	 */
	void EnsureGameInputHost();

	/** Let frames pass with no input, for a test that wants the tree to settle. */
	void PumpFrames(int32 InFrameCount);

private:
	explicit FDreamDriverRig(const FDreamRigOptions& InOptions);

	/**
	 * The world beginning play, as far as DreamUI can tell -- once, after the world, the event system
	 * and the root canvas exist and before any control is made, which is the ordinary order in a game:
	 * the level starts, then screens are built at runtime.
	 *
	 * Without it nothing in the rig ever begins play: the UI manager reports HasBegunPlay false, so
	 * RegisterDreamWidgetHierarchy registers controls without beginning them and no behaviour ever
	 * reaches Awake, OnEnable, Start or Tick. Pointer events still arrive, which is what made the
	 * difference silent -- a scroll bar that places its handle in Start and a scroll view whose
	 * inertia runs in Tick were being tested in a state no game is ever in.
	 */
	void OpenBeginPlayGate();

	/** Torn down last, because everything below lives inside it. Exactly one of the two exists, chosen by bWithGameInstance. */
	TUniquePtr<DreamTests::FScopedGameWorld> ScopedWorld;
	TUniquePtr<DreamTests::FScopedGameInstanceWorld> ScopedGameInstanceWorld;
	/** By pointer so its address survives anything that happens to this object. */
	TUniquePtr<FDreamDriverContext> DriverContext;
	TSharedPtr<FDreamDriver> DriverInstance;
	AActor* Host = nullptr;

	FDreamRigOptions Options;
};
