// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverKeys.h"

void DreamDriverKeys::GetModifierKeys(EDreamDriverModifierKeys InModifiers, TArray<FKey>& OutKeys)
{
	OutKeys.Reset();
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Shift))
	{
		OutKeys.Add(EKeys::LeftShift);
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Ctrl))
	{
		OutKeys.Add(EKeys::LeftControl);
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Alt))
	{
		OutKeys.Add(EKeys::LeftAlt);
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Cmd))
	{
		OutKeys.Add(EKeys::LeftCommand);
	}
}

FModifierKeysState DreamDriverKeys::MakeModifierKeysState(TConstArrayView<FKey> InHeldKeys)
{
	return FModifierKeysState(
		InHeldKeys.Contains(EKeys::LeftShift), InHeldKeys.Contains(EKeys::RightShift),
		InHeldKeys.Contains(EKeys::LeftControl), InHeldKeys.Contains(EKeys::RightControl),
		InHeldKeys.Contains(EKeys::LeftAlt), InHeldKeys.Contains(EKeys::RightAlt),
		InHeldKeys.Contains(EKeys::LeftCommand), InHeldKeys.Contains(EKeys::RightCommand),
		/*bInAreCapsLocked*/ false);
}

FModifierKeysState DreamDriverKeys::MakeModifierKeysState(EDreamDriverModifierKeys InModifiers)
{
	TArray<FKey> Held;
	GetModifierKeys(InModifiers, Held);
	return MakeModifierKeysState(Held);
}

void DreamDriverKeys::GetKeyCodes(const FKey& InKey, uint32& OutKeyCode, uint32& OutCharacterCode)
{
	// The table FSlateApplication::OnKeyDown reads the other way round (GetKeyFromCodes), so an event made here carries
	// the codes the platform would have sent for the same key.
	const uint32* KeyCode = nullptr;
	const uint32* CharacterCode = nullptr;
	FInputKeyManager::Get().GetCodesFromKey(InKey, KeyCode, CharacterCode);
	OutKeyCode = KeyCode != nullptr ? *KeyCode : 0;
	OutCharacterCode = CharacterCode != nullptr ? *CharacterCode : 0;
}

FString DreamDriverKeys::Describe(const FKey& InKey, EDreamDriverModifierKeys InModifiers)
{
	FString Chord;
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Ctrl))
	{
		Chord += TEXT("Ctrl+");
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Alt))
	{
		Chord += TEXT("Alt+");
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Cmd))
	{
		Chord += TEXT("Cmd+");
	}
	if (EnumHasAnyFlags(InModifiers, EDreamDriverModifierKeys::Shift))
	{
		Chord += TEXT("Shift+");
	}
	return Chord + InKey.ToString();
}
