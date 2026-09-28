// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamUIStyleSheet.h"
#include "Core/DreamGUISettings.h"

const UDreamUIStyleSheet* UDreamUIStyleSheet::GetProjectSheet()
{
	const UDreamGUISettings* Settings = UDreamGUISettings::Get();
	if (Settings == nullptr || Settings->DefaultStyleSheet.IsNull())
	{
		return nullptr;
	}
	// Get() first: after the first resolve the object is alive and this is a map lookup. The
	// LoadSynchronous is once, at the first control of a session, which is construction time --
	// exactly where a style lookup is supposed to pay its cost.
	if (const UDreamUIStyleSheet* Loaded = Cast<UDreamUIStyleSheet>(Settings->DefaultStyleSheet.Get()))
	{
		return Loaded;
	}
	// A setting that names some other data asset answers "no sheet", the same as an unset one.
	return Cast<UDreamUIStyleSheet>(Settings->DefaultStyleSheet.LoadSynchronous());
}
