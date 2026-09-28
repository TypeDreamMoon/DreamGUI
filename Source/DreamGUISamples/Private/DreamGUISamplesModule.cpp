// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIScriptPackages.h"
#include "Modules/ModuleManager.h"

class FDreamGUISamplesModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUISamples"));
	}

	virtual void ShutdownModule() override
	{
		DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUISamples"));
	}
};

IMPLEMENT_MODULE(FDreamGUISamplesModule, DreamGUISamples)
