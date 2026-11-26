// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Text/DreamUISourceSettings.h"

UDreamUISourceSettings::UDreamUISourceSettings()
{
	SourceDirectory.Path = GetDefaultSourceDirectory();
}

const TCHAR* UDreamUISourceSettings::GetDefaultSourceDirectory()
{
	return TEXT("DreamSource/UI");
}
