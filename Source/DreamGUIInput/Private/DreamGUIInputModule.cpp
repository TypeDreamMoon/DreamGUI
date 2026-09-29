// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"

class FDreamGUIInputModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// This module loads with the core, before anything compiles a .dui or reads the project settings,
		// so its types -- the event system actor class the settings name among them -- are there from the
		// start.
		DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUIInput"));
	}

	virtual void ShutdownModule() override
	{
		FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUIInput"));
		DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUIInput"));
	}
};

IMPLEMENT_MODULE(FDreamGUIInputModule, DreamGUIInput)
