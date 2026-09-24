// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "DreamStandaloneInputEventSystemActor.generated.h"

class UDreamStandaloneInputModule;

/**
 * A ready-to-place event system driven by the engine's legacy key bindings.
 *
 * The preset Blueprint /DreamGUI/Blueprints/DreamEventSystemActor is a data-only subclass of this
 * class that adds nothing of its own. It used to wire the same keys by hand in an event graph on the
 * bare ADreamEventSystemActor; everything that graph did lives here -- the input module component,
 * the BeginPlay registration, and one binding per key -- so the Blueprint now gets exactly what this
 * class binds (DreamGUI.Input.StandalonePreset.* holds it to that).
 *
 * Placing this needs no input setup in the project -- AutoReceiveInput claims player 0 and the keys
 * are bound directly rather than through action mappings, which is what makes the preset useful as a
 * drop-in. A project that already owns its input should use the Enhanced Input variant instead.
 *
 * The actor only watches: no binding consumes its key, so the pawn and the PlayerController keep
 * receiving everything bound here while it sits in the level. Whether the UI swallowed a click is a
 * question the pointer's hit test answers at the moment the click happens, not one a flag set at bind
 * time can answer. A project that really does want the UI to hold every bound key exclusively sets
 * bConsumeBoundInput.
 *
 * It keeps listening while the game is paused, because a pause menu is UI: every binding executes
 * when paused, and whether a paused game's UI still answers is UDreamUISettings::
 * bScreenSpaceUIAffectByGamePause, which each handler reads when its key arrives (see
 * IsInputSuspendedByGamePause). The bindings used to keep the engine's default -- no execution while
 * paused -- so a paused game's menu could not be clicked, navigated or backed out of, whatever the
 * setting said.
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamStandaloneInputEventSystemActor : public ADreamEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamStandaloneInputEventSystemActor();

	/**
	 * Restores the old behaviour: every binding this actor makes consumes its key, so neither the pawn
	 * nor the PlayerController ever sees those keys. Off by default.
	 *
	 * The default matters because a consuming legacy binding is unconditional. UPlayerInput adds a
	 * bound key to the consume list every frame whether or not an event arrived for it, the AnyKey
	 * binding expands to every key the player owns, and AutoReceiveInput has already put this actor
	 * above the pawn on the input stack -- Enhanced Input then skips any action whose key is already
	 * consumed. Leave this off unless the UI really is meant to be the only thing listening.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-Input")
	bool bConsumeBoundInput = false;

	/**
	 * Bind every key this preset listens to. Calls the three halves below.
	 *
	 * Public because BeginPlay is not the only honest moment to call it: an actor spawned with
	 * AutoReceiveInput cleared has no InputComponent until the project calls EnableInput itself, and
	 * then it is the project that knows when the bindings can be made.
	 */
	virtual void BindDreamInput();

protected:
	virtual void BeginPlay() override;

	/**
	 * Make the event system speak for the player this actor listens to.
	 *
	 * AutoReceiveInput and UDreamEventSystem::UserIndex answer the same question -- whose input is
	 * this? -- and used to be set independently, so a second actor placed for Player1 still produced
	 * pointers stamped player 0. Called from BeginPlay; does nothing when AutoReceiveInput is Disabled,
	 * because the project is then driving EnableInput and owns the answer.
	 */
	void SyncEventSystemUserIndexWithAutoReceiveInput();

	/** The module every binding below feeds. Registered with the event system on BeginPlay. */
	UPROPERTY(Category = "DreamGUI", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamStandaloneInputModule> InputModule;

	/**
	 * Mouse buttons, wheel and movement.
	 *
	 * Separate from the navigation half because that is exactly the seam between the two presets: the
	 * Enhanced Input variant replaces this and keeps everything else.
	 */
	virtual void BindMouseInput();

	/** Navigation keys and touch, which both presets bind the same legacy way. */
	virtual void BindNavigationAndTouchInput();

	/**
	 * One AnyKey binding, so a named action can live on any key without the preset knowing which.
	 *
	 * Navigation keys are deliberately skipped here and routed from their own handlers instead. Input
	 * gives no ordering guarantee between an AnyKey binding and a specific one, so a key that both saw
	 * would be offered to the router twice and a bound action would fire twice.
	 */
	virtual void BindActionRouting();

	/** True for a key this preset already binds by name, and which therefore routes itself. */
	static bool IsNavigationKey(const FKey& Key);

	/**
	 * Whether the UI this preset feeds is meant to go on taking input while the game is paused: the
	 * inverse of UDreamUISettings::bScreenSpaceUIAffectByGamePause, read at the moment it is asked.
	 *
	 * Read every time rather than cached at BeginPlay, as the screen-space raycaster reads the same
	 * setting on every query (UDreamScreenSpaceRaycaster::GetAffectByGamePause): the setting can change
	 * while the game runs, and the next key should follow it. The screen-space setting is the one asked
	 * because a pause menu -- the reason any of this matters -- is screen-space UI. While this preset
	 * listens, world-space UI is held back separately by its own raycasters, which the pointer module
	 * skips while paused when bWorldSpaceUIAffectByGamePause says so. The one combination that cannot be
	 * expressed is screen-space UI paused with world-space UI live: the preset then holds everything.
	 */
	static bool ShouldReceiveInputWhilePaused();

	/**
	 * True while the game is paused and the settings say the UI pauses with it -- the one state in
	 * which a bound handler drops what arrives, as the engine would drop a binding that does not
	 * execute while paused.
	 *
	 * The engine's own gate cannot be used for this. It is a flag on each binding
	 * (FInputBinding::bExecuteWhenPaused), UPlayerInput reads it from the binding, and this actor can
	 * set it only when it binds: a setting changed later would never reach it, and the bindings cannot
	 * be told apart afterwards from the ones a Blueprint subclass's input events put on the same
	 * component, bound to the same actor. So every binding executes while paused and the question is
	 * put here, per key, at the moment the setting is read everywhere else. "Paused" is
	 * UWorld::IsPaused, which is what makes the controller run its input frame as a paused one.
	 */
	bool IsInputSuspendedByGamePause() const;

	/**
	 * How fast the right stick scrolls the list under focus, in canvas units per second at full tilt.
	 *
	 * A property rather than a constant because the right answer is a function of how tall a row is,
	 * which is a project's decision and not the framework's.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Input", meta = (ClampMin = "0.0"))
	float GamepadScrollSpeed = 1500.0f;

	/** The widget navigation or selection currently has, which is what page keys scroll around. */
	UDreamWidget* GetFocusedWidget() const;

	/**
	 * Offer Key to the action router for this actor's player.
	 * @return true when a bound action took it, and the preset's own meaning for the key must not run.
	 */
	bool RouteActionKey(const FKey& Key, bool bPressed);

	/**
	 * Which navigation direction a key stands for, or None if it is not a navigation key.
	 *
	 * The preset Blueprints' old event graphs left every InputNavigation call on None, which made press
	 * and release indistinguishable -- UDreamStandaloneInputModule::InputNavigation writes the argument
	 * straight into NavigateDirection on press and None on release, so directional navigation never
	 * moved. Keeping the mapping in one place is what stops that from being expressible again.
	 */
	static EDreamUINavigationDirection GetNavigationDirectionForKey(const FKey& Key);

	/**
	 * What Key means for THIS press, modifiers included: Tab is Next, Shift+Tab is Prev.
	 *
	 * Not static, and not part of the table, because the answer is not a property of the key. A
	 * legacy binding fires for Tab whether or not shift is down and FKey carries no modifier state,
	 * so the only place the distinction exists is the live shift state on UPlayerInput at the moment
	 * the key arrives.
	 */
	virtual EDreamUINavigationDirection ResolveNavigationDirection(const FKey& Key) const;

	/**
	 * Give Key to the virtual cursor when one is up, so the cursor and directional navigation never
	 * act on the same press.
	 *
	 * @return true when the cursor is active and the key is therefore spoken for -- a confirm is
	 * forwarded to it as a click, a direction belongs to the stick it is integrating and is dropped.
	 */
	bool TryHandleWithVirtualCursor(const FKey& Key, bool bPressed);

	/** Current mouse position as the module reports it, as a 3D vector for the pointer API. */
	FVector GetPointerPosition() const;

	/**
	 * Tell the event system which device Key belongs to. Called from every bound handler: the actor is
	 * the only place a real key is still in hand, and everything downstream sees pointer positions and
	 * navigation directions with no trace of what produced them.
	 */
	void ReportDeviceForKey(const FKey& Key);

private:
	void OnMouseButtonPressed(FKey Key);
	void OnMouseButtonReleased(FKey Key);
	void OnMouseMoved(FVector AxisValue);
	void OnMouseWheel(float AxisValue);

	void OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
	void OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);
	void OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);

	void OnAnyKeyPressed(FKey Key);
	void OnAnyKeyReleased(FKey Key);

	void OnScrollKeyPressed(FKey Key);
	void OnGamepadScrollX(float AxisValue);
	void OnGamepadScrollY(float AxisValue);

	void OnNavigationTriggerPressed(FKey Key);
	void OnNavigationTriggerReleased(FKey Key);
	void OnNavigationDirectionPressed(FKey Key);
	void OnNavigationDirectionReleased(FKey Key);
};
