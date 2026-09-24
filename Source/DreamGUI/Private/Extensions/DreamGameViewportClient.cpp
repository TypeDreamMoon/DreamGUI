// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/DreamGameViewportClient.h"

#include "Engine/Console.h"
#include "Interaction/UITextInput.h"

bool UDreamGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	/*
	 * The console, then the DreamGUI field being edited, then the base class -- in that order, rather
	 * than "the base class first and the field with whatever it leaves".
	 *
	 * The base class's answer cannot tell the field whether the console took a character.
	 * UGameViewportClient::InputChar offers the character to the console and, when the console declines
	 * and input is not being ignored, falls back to FGameplayViewportClient::InputChar -- which, in a
	 * play-in-editor viewport, answers true for EVERY character, so that game input is not routed on to
	 * the editor's own frame. Returning on the base's true therefore meant never reaching the field in a
	 * play session: typing there fell back to the field's key table, while a packaged game, where that
	 * answer is false, got real characters.
	 *
	 * So the base's own steps are taken one at a time, with the field between the console and the
	 * catch-all:
	 *  1. The console, asked exactly as the base asks it. An open console takes every character, and a
	 *     closed one takes the character its own toggle key produces (UConsole::InputChar answers
	 *     bCaptureKeyInput then); only its InputChar knows which, so it is asked, not inspected.
	 *  2. Nothing more while the client ignores input: the base stops after the console then, and so does
	 *     UGameViewportClient::InputKey, so a field whose keys are shut off does not type either.
	 *  3. The DreamGUI field that owns the keyboard. A character it takes has been consumed, and a consumed
	 *     character goes no further -- the editor's frame included -- which is all the base's absorption
	 *     is for. It is a REAL character, resolved by the platform on the player's own layout, which is
	 *     what makes the field stop guessing one from the key code.
	 *  4. Anything nobody took goes to the base class unchanged, absorption and all. That asks the console
	 *     a second time, which is harmless: a console that declined is closed, and a closed console only
	 *     reads bCaptureKeyInput.
	 */
	FString CharacterString;
	CharacterString += Character;
	if (ViewportConsole != nullptr && ViewportConsole->InputChar(FInputDeviceId::CreateFromInternalId(ControllerId), CharacterString))
	{
		return true;
	}
	if (!IgnoreInput() && UUITextInput::RouteCharacterInputToActiveInput(Character))
	{
		return true;
	}
	return Super::InputChar(InViewport, ControllerId, Character);
}
