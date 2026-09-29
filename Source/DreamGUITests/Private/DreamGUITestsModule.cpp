// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

#include "Core/DreamUIWidgetRegistry.h"
#include "Lifecycle/DreamLifecycleProbe.h"

/*
 * Automation tests register themselves from static initialisers as the DLL loads, so the module only
 * has to be loaded early enough that the registry is populated before a -ExecCmds automation run asks
 * for it; that is what the PostEngineInit loading phase in the .uplugin buys. What it does start is the
 * watch that checks, after every DreamGUI test, the probes that must hold between any two tests.
 */
class FDreamGUITestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		InvariantWatch.Start();
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		InvariantWatch.Stop();
#endif
		// The fixtures declare .dui tags of their own, and their class getters are code in this module.
		FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUITests"));
	}

#if WITH_EDITOR
private:
	DreamTests::Lifecycle::FSuiteInvariantWatch InvariantWatch;
#endif
};

IMPLEMENT_MODULE(FDreamGUITestsModule, DreamGUITests)
