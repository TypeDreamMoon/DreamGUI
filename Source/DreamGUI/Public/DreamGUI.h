// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "Stats/Stats.h"
#include "Modules/ModuleInterface.h"

DREAMGUI_API DECLARE_LOG_CATEGORY_EXTERN(DreamGUI, Log, All);
// The plugin's stat group is declared with the renderer, the lowest module that counts into it.
#include "DreamUIRender/DreamUIRendererLogging.h"

class FDreamGUIModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
