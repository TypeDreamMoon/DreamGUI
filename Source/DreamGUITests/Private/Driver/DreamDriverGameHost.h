// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Driver/DreamDriverTypes.h"

struct FDreamDriverContext;
class APlayerController;
enum class EDreamUIMouseButtonType : uint8;
enum class EDreamUINavigationDirection : uint8;

/**
 * The last stretch of a game's input path, for a rig that has no game around it.
 *
 * In a game nothing talks to UDreamStandaloneInputModule directly. A key or a mouse button reaches the
 * player controller (UGameViewportClient::InputKey hands it to PlayerController->InputKey), waits in
 * the controller's UPlayerInput until the controller's input tick, and is then dispatched down the
 * controller's input stack -- where the preset input actor's bindings turn it into a module call. A
 * character takes a different road again: UDreamGameViewportClient::InputChar hands it to
 * UUITextInput::RouteCharacterInputToActiveInput. This namespace builds that path headlessly and feeds
 * it: a local player, a controller on the world's list with the local player behind it, and one of
 * the test input actors (Driver/DreamDriverInputActors.h) begun and bound; then every input step
 * enters as the key the preset binds, through APlayerController::InputKey / InputTouch.
 *
 * The pointer position is the one exception, deliberately: there is no mouse, and the user's must
 * never be moved or read, so the position stays on the driver module's override seam -- which is
 * exactly what the preset reads (its GetPointerPosition asks the module).
 *
 * WHICH KEY EACH STEP SENDS (the preset's own tables, ADreamStandaloneInputEventSystemActor.cpp):
 *  - PressMouseButton: LeftMouseButton / RightMouseButton / MiddleMouseButton, pressed or released.
 *    The Enhanced preset receives the same keys through its mapping context.
 *  - Scroll: what FSceneViewport::OnMouseWheel sends for one notch -- MouseScrollDown or MouseScrollUp
 *    pressed and released, then the MouseWheelAxis value. The preset forwards the axis as (v, v).
 *  - Navigate: the left stick's direction keys (Gamepad_LeftStick_Left/Right/Up/Down), Tab for Next,
 *    Shift+Tab for Prev. The stick rather than the arrow keys because a text field being edited binds
 *    and consumes the arrows (they move its caret), and "a navigation direction" has to mean the same
 *    thing whatever has focus -- which is what it means when it goes straight into the module. The
 *    arrows are reachable through TypeKey, where they mean what arrows mean.
 *  - NavigationTrigger: Gamepad_FaceButton_Bottom, for the same reason: Enter is also a text field's
 *    submit key. Enter is reachable through TypeKey.
 *  - TypeKey: the key itself, with the modifier held around it.
 *  - Touch: APlayerController::InputTouch with the finger's index, as FSceneViewport sends it.
 *
 * ONE FRAME PER HALF. Press and release are separate calls for everything but TypeKey, and the steps
 * that make them spend a frame each, so a press is always processed a frame before its release -- as
 * it is when a hand does it. TypeKey is one call for a whole keystroke; it presses now and releases
 * AFTER the controller's next input frame has processed the press (see TypeKey), because a key
 * pressed and released inside one input frame is not the same event: UPlayerInput decides whether a
 * key is down from that frame's presses minus its releases (UPlayerInput::ProcessNonAxesKeys), so a
 * modifier released in the frame it was pressed was never held, and a navigation key released in the
 * frame it was pressed never leaves the module holding a direction.
 */
namespace DreamDriverGameHost
{
	/**
	 * Step 3 of building a rig whose input host is an actor. Needs InContext.World and
	 * InContext.GameInstance. Makes a local player (on the game instance), a player controller for it
	 * (on the world's controller list, SetPlayer run, so its PlayerInput and input component exist),
	 * and the test input actor for InHost, begun and bound; fills EventSystem, InputModule,
	 * PlayerController, LocalPlayer, InputActor and InputHost. False with OutWhyNot saying which link
	 * failed; whatever was made before the failure stays in the context for Teardown to undo.
	 */
	bool Build(FDreamDriverContext& InContext, EDreamRigInputHost InHost, FString& OutWhyNot);

	/**
	 * For a world that already has a player controller with a local player (PIE): only spawn the input
	 * actor for InHost, begin it and fill EventSystem, InputModule, InputActor and InputHost.
	 * PlayerController and LocalPlayer are the caller's to fill. Refuses a world that already has an
	 * event system for player 0, because the UI manager would refuse the second one.
	 */
	bool AttachInputActor(FDreamDriverContext& InContext, APlayerController* InController, EDreamRigInputHost InHost, FString& OutWhyNot);

	/**
	 * Step 3 of the headless pump: the controller's input frame (APlayerController::TickPlayerInput --
	 * exactly that, not the controller's whole tick), then the input actor's own tick when it has one,
	 * then the releases TypeKey owes. Does nothing under the engine pump, where the engine ticks the
	 * controller itself.
	 */
	void TickPlayerInput(FDreamDriverContext& InContext, float InDeltaSeconds);

	/**
	 * First step of tearing a rig down: the input actor destroyed (its EndPlay takes the mapping
	 * context off the local player and the event system out of the UI manager), then the local player
	 * removed from the game instance, which destroys its controller. Every context field this namespace
	 * filled is cleared. Safe on a half-built host.
	 */
	void Teardown(FDreamDriverContext& InContext);

	bool PressMouseButton(FDreamDriverContext& InContext, EDreamUIMouseButtonType InButton, bool bInPressed, FString& OutWhyNot);
	bool Scroll(FDreamDriverContext& InContext, const FVector2D& InAxisValue, FString& OutWhyNot);
	bool Navigate(FDreamDriverContext& InContext, EDreamUINavigationDirection InDirection, bool bInPressed, FString& OutWhyNot);
	bool NavigationTrigger(FDreamDriverContext& InContext, bool bInPressed, FString& OutWhyNot);
	/**
	 * A character by the game's own road: the call UDreamGameViewportClient::InputChar makes,
	 * UUITextInput::RouteCharacterInputToActiveInput, which hands it to whichever field owns the
	 * keyboard. Fails when no field does, or when the one that does belongs to another world. A
	 * character the field refuses is not a failure -- refusing it is the field's decision.
	 */
	bool TypeCharacter(FDreamDriverContext& InContext, TCHAR InCharacter, FString& OutWhyNot);

	/**
	 * One keystroke through the controller's input stack: InModifier (when valid; one of the eight
	 * modifier keys) and InKey pressed now, both released after the controller's next input frame.
	 * Nothing here decides who takes the key -- a field being edited, an armed key selector, the input
	 * actor's navigation, confirm and Back bindings -- the input stack does, as in a game.
	 */
	bool TypeKey(FDreamDriverContext& InContext, const FKey& InKey, const FKey& InModifier, FString& OutWhyNot);

	/**
	 * One phase of one finger, through APlayerController::InputTouch. Also moves the driver module's
	 * cursor the way FSceneViewport moves its cached cursor on every touch -- to the touch, and to
	 * (-1, -1) when the last finger lifts -- because the preset re-reads the "mouse" position every
	 * frame, and on a touch screen that position is the finger's.
	 */
	bool Touch(FDreamDriverContext& InContext, EDreamDriverTouchPhase InPhase, int32 InFingerId, const FVector2D& InPixel, FString& OutWhyNot);
}
