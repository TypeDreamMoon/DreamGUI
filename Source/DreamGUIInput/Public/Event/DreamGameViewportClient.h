// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "DreamGameViewportClient.generated.h"

class APlayerController;

/**
 * The one hook a mesh UI cannot reach any other way: the platform's CHARACTER events.
 *
 * A DreamGUI text field is not a Slate widget, so it is never on the keyboard focus path, and
 * FSlateApplication::ProcessKeyCharEvent only walks that path. The engine's own landing place for a
 * character in a game is UGameViewportClient::InputChar -- which has no delegate to subscribe to,
 * only a virtual to override. So this exists: a viewport client that overrides that one function
 * and hands the character to whichever DreamGUI field currently owns the keyboard.
 *
 * Without it, UUITextInput falls back to its own FKey -> TCHAR table, which is only ever right on a
 * US QWERTY layout -- AZERTY, QWERTZ, Dvorak, Cyrillic, dead keys and AltGr all type the wrong
 * character. Setting this class (or a subclass of it) as the project's game viewport client is what
 * makes typing correct on every layout; the field logs a warning once, the first time it is edited,
 * when it notices the project has not.
 *
 * Two ways to adopt it, whichever fits the project:
 *   1. DefaultEngine.ini
 *          [/Script/Engine.Engine]
 *          GameViewportClientClassName=/Script/DreamGUIInput.DreamGameViewportClient
 *   2. A project that already has its own viewport client: derive from this instead of
 *      UGameViewportClient, or keep its own base and call
 *      DreamUITextInputRouter::RouteViewportCharacter(this, ControllerId, Character) from its InputChar override --
 *      after its console has had the character and BEFORE calling the base class's InputChar. In a
 *      play-in-editor viewport the engine's base answers true for every character (it absorbs them
 *      so they do not reach the editor's frame), so an override that asks the base first and returns
 *      on its answer never reaches the field there. The routing function is the whole contract --
 *      nothing else about this class is required.
 *
 * It is also what lets DreamGUI hear anything in its own UI-only input mode. The engine's UI-only mode
 * makes the viewport client ignore every key, click, axis and touch -- UMG's widgets hear them through
 * Slate instead -- while every piece of DreamGUI's input arrives through the player controller behind
 * the client. So while UDreamUIInputModeLibrary::SetInputModeUIOnly holds the mode, this client routes
 * that input on exactly as it would with the mode off, and holds the player's movement and look input
 * still, since the player controller now hears what DreamGUI hears. A UI-only mode set any other way --
 * UMG's library, APlayerController::SetInputMode -- keeps the engine's meaning. While DreamGUI's mode holds, the cursor
 * is DreamGUI's to show: a pad hides it and the keyboard and mouse bring it back (UDreamGUISettings::bHideCursorOnGamepad).
 *
 * And it keeps Slate's own navigation out of UMG while DreamGUI has the keys (HandleNavigation): Slate turns a Tab
 * nobody handled into a step of its own, which from the bare viewport descends into the viewport's children -- the
 * UMG layers -- and gives the keyboard focus to the first focusable widget there, after which DreamGUI hears no key
 * until the viewport has the focus back. The world's input subsystem guards the same road through the base class's
 * OnNavigationOverride for any other client.
 */
UCLASS(BlueprintType)
class DREAMGUIINPUT_API UDreamGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputAxis(const FInputKeyEventArgs& EventArgs) override;
	virtual bool InputTouch(FViewport* const InViewport, const FTouchId TouchId, const ETouchType::Type Type, const FVector2D& TouchLocation, const float Force, const uint64 Timestamp) override;

	/**
	 * Called by UDreamUIInputModeLibrary as it enters or leaves UI-only mode for InPlayerController.
	 * Entering routes the input the engine ignores in that mode and holds the player's movement and look
	 * input; leaving undoes both.
	 */
	void SetDreamUIOnlyInput(APlayerController* InPlayerController, bool bInDreamUIOnly);
	bool IsDreamUIOnlyInput() const { return bDreamUIOnlyInput; }
	/**
	 * Whether InPlayerController's viewport client is one of these, holding DreamGUI's UI-only mode: the cursor is then
	 * DreamGUI's to hide on a pad and show again on the mouse. False for a controller with no local player.
	 */
	static bool IsDreamUIOnlyInputFor(const APlayerController* InPlayerController);

	/**
	 * Slate's navigation, with its destination known: swallowed while Slate user InUserIndex's keyboard focus is on this
	 * world's bare viewport and DreamGUI has UI up for that player (UDreamUIInputSubsystem::ShouldSwallowSlateNavigation),
	 * else what the base class does -- OnNavigationOverride, then nothing.
	 */
	virtual bool HandleNavigation(const uint32 InUserIndex, TSharedPtr<SWidget> InDestination) override;

	/**
	 * A character the platform resolved, on the player's own keyboard layout.
	 *
	 * The console is asked first, so it keeps its priority: a character typed into an open console
	 * is the console's, not a background field's. Then, unless the client is ignoring input, the
	 * DreamGUI field being edited; then the base class, with whatever neither took. The base is not
	 * asked first because in a play-in-editor viewport it answers true for every character, and the
	 * field would never see one there (the .cpp walks through it).
	 */
	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;

private:
	bool bDreamUIOnlyInput = false;
	/** The player whose movement and look input SetDreamUIOnlyInput holds, so leaving the mode releases exactly that. */
	TWeakObjectPtr<APlayerController> PlayerHeldStill;
};
