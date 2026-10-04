// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The test host's only module.
 *
 * A project needs a primary game module to be a code project, and a code project is what gives the
 * host its own DreamGUITestHostEditor target -- which is what builds the DreamGUI plugin sitting in
 * Plugins/. Everything the tests need comes from the plugin itself.
 *
 * The one thing here is the packaged text smoke probe (DreamGUIPackagedSmoke.cpp), off unless a game
 * is started with -DreamGUITextSmoke=<directory>. It runs in packaged builds, where the plugin's test
 * module does not exist, so it has to be a game module's; it reads DreamGUI's texts and fonts, asks
 * Slate for the platform's safe zone and writes JSON, hence the private dependencies. Private, so
 * nothing that depends on this module sees them.
 *
 * It does not depend on EnhancedInput or InputCore the way the Blank template does: a dependency
 * here would be a second, silent way for the plugin's own dependencies to be satisfied, and the host
 * exists partly to prove the plugin declares everything it uses.
 */
public class DreamGUITestHost : ModuleRules
{
	public DreamGUITestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "DreamGUI", "Json", "Slate", "SlateCore" });
	}
}
