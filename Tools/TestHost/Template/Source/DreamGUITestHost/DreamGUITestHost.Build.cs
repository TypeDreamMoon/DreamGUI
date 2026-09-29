// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

using UnrealBuildTool;

/*
 * The test host's only module, and it does nothing.
 *
 * A project needs a primary game module to be a code project, and a code project is what gives the
 * host its own DreamGUITestHostEditor target -- which is what builds the DreamGUI plugin sitting in
 * Plugins/. Everything the tests need comes from the plugin itself, so this module depends on no
 * more than a module has to.
 *
 * In particular it does not depend on EnhancedInput or InputCore the way the Blank template does: a
 * dependency here would be a second, silent way for the plugin's own dependencies to be satisfied,
 * and the host exists partly to prove the plugin declares everything it uses.
 */
public class DreamGUITestHost : ModuleRules
{
	public DreamGUITestHost(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
	}
}
