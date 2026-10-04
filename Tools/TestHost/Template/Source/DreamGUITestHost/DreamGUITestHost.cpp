// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamGUITestHost.h"
#include "DreamGUIPackagedSmoke.h"
#include "Modules/ModuleManager.h"

/**
 * The host project has no behaviour of its own, so anything a test observes comes from the plugin under test and not from
 * here -- with one exception, off unless a game is started with -DreamGUITextSmoke=<directory>: the packaged text smoke
 * probe (DreamGUIPackagedSmoke.h), which has to live in a game module because it runs in packaged builds, where the
 * plugin's test module does not exist.
 */
class FDreamGUITestHostModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		DreamGUIPackagedSmoke::StartIfAsked();
	}

	virtual void ShutdownModule() override
	{
		DreamGUIPackagedSmoke::Stop();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDreamGUITestHostModule, DreamGUITestHost, "DreamGUITestHost");
