// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Extensions/DreamGameViewportClient.h"

#include "Interaction/UITextInput.h"

bool UDreamGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	// Console first, through the base class. UGameViewportClient::InputChar routes to the console
	// before anything else and returns whether it took the character; a field that is being edited
	// behind an open console must not steal what the player is typing into the console.
	if (Super::InputChar(InViewport, ControllerId, Character))
	{
		return true;
	}
	// Nothing else wanted it: if a DreamGUI field owns the keyboard, this is its character -- and it
	// is a REAL character, resolved by the platform on the player's own layout, which is what makes
	// the field stop guessing one from the key code.
	return UUITextInput::RouteCharacterInputToActiveInput(Character);
}
