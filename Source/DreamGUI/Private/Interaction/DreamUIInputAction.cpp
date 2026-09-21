// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamUIInputAction.h"

FKey FDreamUIInputActionData::GetKeyForDevice(EDreamUIInputDevice InDevice) const
{
	switch (InDevice)
	{
	case EDreamUIInputDevice::Gamepad:
		return GamepadKey;
	case EDreamUIInputDevice::Touch:
		// A touchscreen has no keys. Returning the keyboard one would put "press Enter" in front of a
		// phone player, which is worse than showing nothing.
		return FKey();
	case EDreamUIInputDevice::MouseAndKeyboard:
	default:
		return KeyboardKey;
	}
}

TSoftObjectPtr<UTexture2D> FDreamUIInputActionData::GetIconForDevice(EDreamUIInputDevice InDevice, EDreamUIGamepadModel InGamepadModel) const
{
	switch (InDevice)
	{
	case EDreamUIInputDevice::Gamepad:
		// The per-model glyph when the row ships one for THIS pad, and the plain pad glyph otherwise.
		// An entry that is authored but empty counts as "no glyph for this model" rather than as a
		// deliberate blank: a prompt with no icon falls back to the key's name, which is still useful.
		if (const TSoftObjectPtr<UTexture2D>* ModelIcon = GamepadModelIcons.Find(InGamepadModel))
		{
			if (!ModelIcon->IsNull())
			{
				return *ModelIcon;
			}
		}
		return GamepadIcon;
	case EDreamUIInputDevice::Touch:
		return nullptr;
	case EDreamUIInputDevice::MouseAndKeyboard:
	default:
		return KeyboardIcon;
	}
}

bool FDreamUIInputActionData::MatchesKey(const FKey& InKey, bool bShiftDown, bool bCtrlDown, bool bAltDown, bool bCmdDown) const
{
	if (!InKey.IsValid())
	{
		return false;//an action with neither key authored must not swallow every unbound key
	}
	// Both spellings are checked whatever device is in use: the key itself says which device produced
	// it, and a player with a pad plugged in can still reach over and hit the keyboard one.
	const bool bMatchesGamepad = InKey == GamepadKey;
	const bool bMatchesKeyboard = InKey == KeyboardKey;
	if (!bMatchesKeyboard && !bMatchesGamepad)
	{
		return false;
	}
	if (bMatchesGamepad)
	{
		return true;//a pad has no modifiers to hold, so the pad spelling never asks for any
	}
	// Every modifier the action asks for has to be down. Extra ones are tolerated -- see the header for
	// why that is what keeps existing bindings working -- and specificity is settled by the router.
	return (!bRequiresShift || bShiftDown)
		&& (!bRequiresCtrl || bCtrlDown)
		&& (!bRequiresAlt || bAltDown)
		&& (!bRequiresCmd || bCmdDown);
}

int32 FDreamUIInputActionData::CountRequiredModifiers() const
{
	return (bRequiresShift ? 1 : 0) + (bRequiresCtrl ? 1 : 0) + (bRequiresAlt ? 1 : 0) + (bRequiresCmd ? 1 : 0);
}
