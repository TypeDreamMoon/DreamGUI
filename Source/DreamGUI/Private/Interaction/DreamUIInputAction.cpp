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

TSoftObjectPtr<UTexture2D> FDreamUIInputActionData::GetIconForDevice(EDreamUIInputDevice InDevice) const
{
	switch (InDevice)
	{
	case EDreamUIInputDevice::Gamepad:
		return GamepadIcon;
	case EDreamUIInputDevice::Touch:
		return nullptr;
	case EDreamUIInputDevice::MouseAndKeyboard:
	default:
		return KeyboardIcon;
	}
}

bool FDreamUIInputActionData::MatchesKey(const FKey& InKey) const
{
	if (!InKey.IsValid())
	{
		return false;//an action with neither key authored must not swallow every unbound key
	}
	// Both spellings are checked whatever device is in use: the key itself says which device produced
	// it, and a player with a pad plugged in can still reach over and hit the keyboard one.
	return InKey == KeyboardKey || InKey == GamepadKey;
}
