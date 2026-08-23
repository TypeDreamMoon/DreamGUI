// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "UObject/DreamGUIObjectVersion.h"
#include "Serialization/CustomVersion.h"

const FGuid FDreamGUIObjectVersion::GUID(0x7AE4C85F, 0x2F2C4BD9, 0x979AAB5C, 0x64E2652F);
FCustomVersionRegistration GRegisterDreamGUIObjectVersion(FDreamGUIObjectVersion::GUID, FDreamGUIObjectVersion::LatestVersion, TEXT("DreamGUI"));
