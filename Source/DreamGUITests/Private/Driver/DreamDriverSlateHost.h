// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Templates/SharedPointer.h"

#include "Driver/DreamDriverTypes.h"

struct FDreamDriverContext;
class FDreamUISlateInputSource;
enum class EDreamUIMouseButtonType : uint8;
enum class EDreamUINavigationDirection : uint8;

/**
 * The last stretch of a game's input path when the world hears its input from Slate (EDreamRigInputHost::SlateSource).
 *
 * With UDreamGUISettings::bUseSlateInputSource a world's input comes from FDreamUISlateInputSource, one of Slate's input
 * pre-processors: Slate offers it every key, mouse and touch event before the game viewport sees it, and it feeds the
 * players' input -- keys down the one road every source takes (DreamUIKeyRouting::RouteKey), pointers as moves,
 * presses and wheel turns queued for the player's next frame. The preset actors stand down while it is on. Characters
 * do not pass through it: Slate routes a typed character to the focused viewport, whose client hands it to
 * UDreamUIInputSubsystem::HandleViewportCharacter.
 *
 * This namespace plays Slate's part for a headless rig. Build turns the world's own source on and gives it the test
 * mappers a world without a game viewport needs -- screen space is the rig's viewport pixel for pixel, Slate user 0
 * is player 0 and its keyboard focus is on the viewport -- and takes it off Slate's own list of pre-processors, so the
 * machine's mouse and keyboard never reach a test: what the source hears is what the driver sends it, as FKeyEvents
 * and FPointerEvents made the way FSlateApplication makes them from platform messages. The rig's input module stops
 * standing a cursor of its own in for the mouse, so the pointer is wherever Slate's mouse is.
 *
 * WHICH EVENTS EACH STEP SENDS:
 *  - MovePointer: a mouse move at the pixel, with the buttons held down.
 *  - PressMouseButton: the button's down or up at the mouse.
 *  - Scroll: one wheel event at the mouse, the wheel value read from the axis as the actor hosts read it.
 *  - Touch: the finger's own pointer events -- down, move, up -- as Slate makes them from touches.
 *  - Navigate: the key events of the keys the actor hosts press (the left stick's directions, Tab, Shift+Tab).
 *  - SendKey: the modifiers' own key events around the key's, each carrying the modifier state of that moment, which is
 *    what a keyboard sends: Shift goes down (its own event says Shift is held), then Tab, then Tab comes up, then Shift.
 *  - TypeCharacter: UDreamUIInputSubsystem::HandleViewportCharacter, the viewport's road for a character.
 */
namespace DreamDriverSlateHost
{
	/**
	 * Step 3 of building a SlateSource rig, after the ModuleOnly pieces (event system and driver module) exist: the world's
	 * Slate source on, off Slate's own list and deaf to the application's activation (which would let go of what a test
	 * holds), given its test mappers for a viewport of InViewportSize, and the module's stand-in cursor put away. False
	 * with OutWhyNot saying which link failed.
	 */
	bool Build(FDreamDriverContext& InContext, const FIntPoint& InViewportSize, FString& OutWhyNot);

	/** The world's Slate source off again. Safe on a half-built host. */
	void Teardown(FDreamDriverContext& InContext);

	/** The world's Slate source, or null with OutWhyNot saying why there is none to send to. */
	TSharedPtr<FDreamUISlateInputSource> FindSource(const FDreamDriverContext& InContext, FString& OutWhyNot);

	/** Where Slate's mouse is, as the driver last moved it, in viewport pixels. */
	FVector2D GetMousePixel(const FDreamDriverContext& InContext);

	bool MovePointer(FDreamDriverContext& InContext, const FVector2D& InPixel, FString& OutWhyNot);
	bool PressMouseButton(FDreamDriverContext& InContext, EDreamUIMouseButtonType InButton, bool bInPressed, FString& OutWhyNot);
	bool Scroll(FDreamDriverContext& InContext, const FVector2D& InAxisValue, FString& OutWhyNot);
	bool Touch(FDreamDriverContext& InContext, EDreamDriverTouchPhase InPhase, int32 InFingerId, const FVector2D& InPixel, FString& OutWhyNot);
	bool Navigate(FDreamDriverContext& InContext, EDreamUINavigationDirection InDirection, bool bInPressed, FString& OutWhyNot);

	/**
	 * InKey's press or release as key events, with InHeldModifierKeys (modifier keys, left or right) held around it: on the
	 * press each modifier goes down first, in order, then the key; on the release the key comes up first, then the
	 * modifiers in reverse. What the source makes of each event -- typed, taken, kept from the game -- is its decision.
	 */
	bool SendKey(FDreamDriverContext& InContext, const FKey& InKey, TConstArrayView<FKey> InHeldModifierKeys, bool bInPressed, FString& OutWhyNot);
	bool SendKey(FDreamDriverContext& InContext, const FKey& InKey, EDreamDriverModifierKeys InModifiers, bool bInPressed, FString& OutWhyNot);

	/**
	 * A character by the viewport's road, UDreamUIInputSubsystem::HandleViewportCharacter. Fails when no field is being
	 * edited, as a character step does under every host; a character the field refuses is not a failure.
	 */
	bool TypeCharacter(FDreamDriverContext& InContext, TCHAR InCharacter, FString& OutWhyNot);
}
