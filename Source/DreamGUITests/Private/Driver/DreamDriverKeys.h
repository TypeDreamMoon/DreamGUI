// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/GenericApplication.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverTypes.h"

/**
 * What every road a driver key takes needs to know about a chord: which keys the fingers hold around the key, and the
 * modifier state a platform key event carries with it. One place, so the controller's road, the Slate source's and
 * the play session's Slate road press the same keys in the same order.
 */
namespace DreamDriverKeys
{
	/**
	 * The left-hand key of each modifier InModifiers holds, in the order fingers go down: Shift, Ctrl, Alt, Cmd. They come
	 * up in the reverse order.
	 */
	void GetModifierKeys(EDreamDriverModifierKeys InModifiers, TArray<FKey>& OutKeys);

	/** The modifier state a key event carries while InHeldKeys (modifier keys, left or right) are down. Caps Lock is off. */
	FModifierKeysState MakeModifierKeysState(TConstArrayView<FKey> InHeldKeys);
	FModifierKeysState MakeModifierKeysState(EDreamDriverModifierKeys InModifiers);

	/** The platform's key code and character code for InKey, as FSlateApplication::OnKeyDown receives them; 0 for a code the key has none of. */
	void GetKeyCodes(const FKey& InKey, uint32& OutKeyCode, uint32& OutCharacterCode);

	/** "Ctrl+Shift+Tab", for a step's description. */
	FString Describe(const FKey& InKey, EDreamDriverModifierKeys InModifiers);
}
