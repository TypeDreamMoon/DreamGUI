// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIEachAdapter.h"
#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"

class FDreamGUIControlsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// This module loads with the core, before anything compiles a .dui, so its types are there to be
		// found by a short name from the first compile on.
		DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUIControls"));
		// The core reaches the list views an `each` fills through this, and a .dui can compile as soon as
		// the engine is up.
		DreamUIEachAdapter::RegisterEachBindingHandler();
	}

	virtual void ShutdownModule() override
	{
		// The .dui tags this module declared, and the each handler, call into its code.
		DreamUIEachAdapter::UnregisterEachBindingHandler();
		FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUIControls"));
		DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUIControls"));
	}
};

IMPLEMENT_MODULE(FDreamGUIControlsModule, DreamGUIControls)
