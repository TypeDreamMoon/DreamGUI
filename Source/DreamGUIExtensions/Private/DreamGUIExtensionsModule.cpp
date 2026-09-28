// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"

class FDreamGUIExtensionsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// This module loads with the core, before anything compiles a .dui, so its types are there to be
		// found by a short name from the first compile on.
		DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUIExtensions"));
	}

	virtual void ShutdownModule() override
	{
		// The .dui tags this module declared call into its code.
		FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUIExtensions"));
		DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUIExtensions"));
	}
};

IMPLEMENT_MODULE(FDreamGUIExtensionsModule, DreamGUIExtensions)
