// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"

DREAMGUI_API DECLARE_LOG_CATEGORY_EXTERN(DreamGUI, Log, All);
DECLARE_STATS_GROUP(TEXT("DreamGUI"), STATGROUP_DreamGUI, STATCAT_Advanced);

class FDreamGUIModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
