// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIScriptPackages.h"
#include "Core/DreamUIWidgetRegistry.h"
#include "Modules/ModuleManager.h"

class FDreamGUINiagaraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// A .dui names the visual by its short name (NiagaraSystem) once this module is loaded.
		DreamUI::RegisterRuntimeScriptPackage(TEXT("/Script/DreamGUINiagara"));
	}

	virtual void ShutdownModule() override
	{
		// The .dui tag this module declared calls into its code.
		FDreamUIWidgetRegistry::UnregisterModule(TEXT("DreamGUINiagara"));
		DreamUI::UnregisterRuntimeScriptPackage(TEXT("/Script/DreamGUINiagara"));
	}
};

IMPLEMENT_MODULE(FDreamGUINiagaraModule, DreamGUINiagara)
