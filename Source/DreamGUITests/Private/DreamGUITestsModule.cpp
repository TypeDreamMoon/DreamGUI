// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

/*
 * Nothing to start up: automation tests register themselves from static initialisers as the DLL
 * loads, so the module only has to exist and be loaded early enough that the registry is populated
 * before a -ExecCmds automation run asks for it. That is what the PostEngineInit loading phase in
 * the .uplugin buys; this class is only here because a module needs one.
 */
class FDreamGUITestsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
	}

	virtual void ShutdownModule() override
	{
	}
};

IMPLEMENT_MODULE(FDreamGUITestsModule, DreamGUITests)
