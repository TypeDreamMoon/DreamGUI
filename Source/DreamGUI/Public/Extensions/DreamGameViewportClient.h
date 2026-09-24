// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "DreamGameViewportClient.generated.h"

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
 *          GameViewportClientClassName=/Script/DreamGUI.DreamGameViewportClient
 *   2. A project that already has its own viewport client: derive from this instead of
 *      UGameViewportClient, or keep its own base and call
 *      UUITextInput::RouteCharacterInputToActiveInput(Character) from its InputChar override --
 *      after its console has had the character and BEFORE calling the base class's InputChar. In a
 *      play-in-editor viewport the engine's base answers true for every character (it absorbs them
 *      so they do not reach the editor's frame), so an override that asks the base first and returns
 *      on its answer never reaches the field there. The routing function is the whole contract --
 *      nothing else about this class is required.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
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
};
