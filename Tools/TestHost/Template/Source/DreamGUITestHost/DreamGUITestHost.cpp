// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamGUITestHost.h"
#include "DreamGUIClickSmoke.h"
#include "DreamGUIPackagedSmoke.h"
#include "Modules/ModuleManager.h"

/**
 * The host project has no behaviour of its own, so anything a test observes comes from the plugin under test and not from
 * here -- with two exceptions, each off unless a game is started with its switch: the packaged text smoke probe
 * (DreamGUIPackagedSmoke.h, -DreamGUITextSmoke=<directory>) and the click smoke probe (DreamGUIClickSmoke.h,
 * -DreamGUIClickSmoke=<directory>), which have to live in a game module because they run in games, packaged ones
 * included, where the plugin's test module does not exist.
 */
class FDreamGUITestHostModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		DreamGUIPackagedSmoke::StartIfAsked();
		DreamGUIClickSmoke::StartIfAsked();
	}

	virtual void ShutdownModule() override
	{
		DreamGUIClickSmoke::Stop();
		DreamGUIPackagedSmoke::Stop();
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FDreamGUITestHostModule, DreamGUITestHost, "DreamGUITestHost");
